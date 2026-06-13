#include "xc/protocol.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

constexpr std::uintptr_t kLsdriverSharedAddress = 0x2025827000ULL;
constexpr std::size_t kLsdriverCommitTimeoutMs = 3000;
constexpr std::size_t kLsdriverConnectTimeoutMs = 6000;
constexpr std::uint32_t kMaxBreakpointSlots = 4;
constexpr std::size_t kProcNameLen = 256;

struct DriverProbe {
    bool moduleLoaded = false;
    bool procModulesReadable = false;
    bool sharedMemoryOnline = false;
    std::uint64_t numBrps = 0;
    std::uint64_t numWrps = 0;
    std::string kernelRelease;
    std::string message;
};

struct BreakpointSlot {
    bool enabled = false;
    std::uint64_t address = 0;
    std::string type;
    std::uint32_t size = 0;
};

enum hwbp_type {
    HWBP_BREAKPOINT_EMPTY = 0,
    HWBP_BREAKPOINT_R = 1,
    HWBP_BREAKPOINT_W = 2,
    HWBP_BREAKPOINT_RW = HWBP_BREAKPOINT_R | HWBP_BREAKPOINT_W,
    HWBP_BREAKPOINT_X = 4,
    HWBP_BREAKPOINT_INVALID = HWBP_BREAKPOINT_RW | HWBP_BREAKPOINT_X,
};

enum hwbp_len {
    HWBP_BREAKPOINT_LEN_1 = 1,
    HWBP_BREAKPOINT_LEN_2 = 2,
    HWBP_BREAKPOINT_LEN_3 = 3,
    HWBP_BREAKPOINT_LEN_4 = 4,
    HWBP_BREAKPOINT_LEN_5 = 5,
    HWBP_BREAKPOINT_LEN_6 = 6,
    HWBP_BREAKPOINT_LEN_7 = 7,
    HWBP_BREAKPOINT_LEN_8 = 8,
};

enum hwbp_scope {
    SCOPE_MAIN_THREAD,
    SCOPE_OTHER_THREADS,
    SCOPE_ALL_THREADS,
};

struct hwbp_record {
    std::uint8_t mask[18];
    std::uint64_t hit_count;
    std::uint64_t pc;
    std::uint64_t lr;
    std::uint64_t sp;
    std::uint64_t orig_x0;
    std::uint64_t syscallno;
    std::uint64_t pstate;
    std::uint64_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9;
    std::uint64_t x10, x11, x12, x13, x14, x15, x16, x17, x18, x19;
    std::uint64_t x20, x21, x22, x23, x24, x25, x26, x27, x28, x29;
    std::uint32_t fpsr;
    std::uint32_t fpcr;
    unsigned __int128 q0, q1, q2, q3, q4, q5, q6, q7, q8, q9;
    unsigned __int128 q10, q11, q12, q13, q14, q15, q16, q17, q18, q19;
    unsigned __int128 q20, q21, q22, q23, q24, q25, q26, q27, q28, q29;
    unsigned __int128 q30, q31;
};

struct hwbp_point {
    hwbp_type bt;
    hwbp_len bl;
    hwbp_scope bs;
    std::uint64_t hit_addr;
    int record_count;
    hwbp_record records[0x100];
};

struct hwbp_info {
    std::uint64_t num_brps;
    std::uint64_t num_wrps;
    hwbp_point points[16];
};

struct segment_info {
    short index;
    std::uint8_t prot;
    std::uint64_t start;
    std::uint64_t end;
};

struct module_info {
    char name[256];
    int seg_count;
    segment_info segs[10];
};

struct region_info {
    std::uint64_t start;
    std::uint64_t end;
};

struct memory_info {
    int module_count;
    module_info modules[1024];
    int region_count;
    region_info regions[16534];
};

struct virtual_input {
    int POSITION_X, POSITION_Y;
    int slot;
    int x, y;
};

struct memory_rw {
    std::uint64_t rw_addr;
    std::uint8_t user_buffer[0x1000];
    int size;
};

struct process_select_info {
    char name[kProcNameLen];
    int selected_pid;
    std::uint64_t selected_rss_kb;
};

enum sm_req_op {
    op_o,
    op_r,
    op_w,
    op_m,
    op_down,
    op_move,
    op_up,
    op_init_touch,
    op_brps_weps_info,
    op_find_process_by_name,
    op_set_process_hwbp,
    op_remove_process_hwbp,
    op_kexit,
};

struct req_obj {
    bool kernel;
    bool user;
    sm_req_op op;
    int status;
    int pid;
    process_select_info proc_info;
    memory_rw rw_info;
    memory_info mem_info;
    virtual_input vinput_info;
    hwbp_info bp_info;
};

static_assert(sizeof(hwbp_record::q0) == 16, "lsdriver ABI requires 128-bit Q registers");

std::string readTextFile(const char* path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

bool isDigits(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (char c : value) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

bool parseHex64(const std::string& text, std::uint64_t& value) {
    value = 0;
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned>(10 + c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned>(10 + c - 'A');
        } else {
            return false;
        }
        value = value * 16ULL + digit;
    }
    return true;
}

bool pathMatchesModule(const std::string& path, const std::string& module) {
    if (path == module) {
        return true;
    }
    if (path.size() < module.size()) {
        return false;
    }
    const std::size_t start = path.size() - module.size();
    return path.compare(start, module.size(), module) == 0 && (start == 0 || path[start - 1] == '/');
}

std::string readProcessName(int pid) {
    std::string cmdline = readTextFile(("/proc/" + std::to_string(pid) + "/cmdline").c_str());
    for (char& c : cmdline) {
        if (c == '\0') {
            c = ' ';
        }
    }
    while (!cmdline.empty() && cmdline.back() == ' ') {
        cmdline.pop_back();
    }
    if (!cmdline.empty()) {
        return cmdline;
    }
    std::string comm = readTextFile(("/proc/" + std::to_string(pid) + "/comm").c_str());
    while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r')) {
        comm.pop_back();
    }
    return comm;
}

bool findPidByName(const std::string& target, int& pid, std::string& error) {
    pid = 0;
    DIR* proc = ::opendir("/proc");
    if (!proc) {
        error = std::string("failed to open /proc: ") + std::strerror(errno);
        return false;
    }
    while (dirent* entry = ::readdir(proc)) {
        const std::string name = entry->d_name;
        if (!isDigits(name)) {
            continue;
        }
        const int candidate = std::stoi(name);
        const std::string processName = readProcessName(candidate);
        if (processName == target || processName.find(target) == 0) {
            pid = candidate;
            ::closedir(proc);
            return true;
        }
    }
    ::closedir(proc);
    error = "target process not found for module breakpoint: " + target;
    return false;
}

bool resolveModuleBaseFromMaps(int pid, const std::string& module, std::uint64_t& base, std::string& error) {
    if (pid <= 0) {
        error = "module+offset breakpoint requires a resolved target pid";
        return false;
    }
    std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
    if (!maps) {
        error = "failed to open /proc/" + std::to_string(pid) + "/maps";
        return false;
    }
    std::string line;
    while (std::getline(maps, line)) {
        std::istringstream parts(line);
        std::string range;
        std::string perms;
        std::string offset;
        std::string dev;
        std::string inode;
        std::string path;
        parts >> range >> perms >> offset >> dev >> inode;
        std::getline(parts, path);
        while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) {
            path.erase(path.begin());
        }
        const std::size_t dash = range.find('-');
        std::uint64_t start = 0;
        if (dash == std::string::npos || !parseHex64(range.substr(0, dash), start)) {
            continue;
        }
        if (pathMatchesModule(path, module) && (perms.find('x') != std::string::npos || base == 0)) {
            base = start;
            if (perms.find('x') != std::string::npos) {
                return true;
            }
        }
    }
    if (base != 0) {
        return true;
    }
    error = "module not found in target maps: " + module;
    return false;
}

std::string jsonBool(bool value) {
    return value ? "true" : "false";
}

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

class LsdriverBackend {
public:
    LsdriverBackend() {
        open();
    }

    ~LsdriverBackend() {
        if (req_ && req_ != MAP_FAILED) {
            munmap(req_, sizeof(req_obj));
        }
    }

    DriverProbe probe() {
        DriverProbe probe;
        utsname uts{};
        if (::uname(&uts) == 0) {
            probe.kernelRelease = uts.release;
        } else {
            probe.kernelRelease = "unknown";
        }

        const std::string modules = readTextFile("/proc/modules");
        probe.procModulesReadable = !modules.empty();
        probe.moduleLoaded = modules.find("lsdriver") != std::string::npos;

        if (!ensureMapped()) {
            probe.message = lastError_;
            return probe;
        }

        if (!waitForDriver(kLsdriverConnectTimeoutMs)) {
            probe.message = "lsdriver shared memory not attached yet; start agent as LS after insmod";
            return probe;
        }

        if (!commit(op_brps_weps_info)) {
            probe.message = lastError_;
            return probe;
        }

        probe.sharedMemoryOnline = true;
        probe.moduleLoaded = true;
        probe.numBrps = req_->bp_info.num_brps;
        probe.numWrps = req_->bp_info.num_wrps;
        probe.message = "lsdriver shared memory online, BRP=" + std::to_string(probe.numBrps)
            + ", WRP=" + std::to_string(probe.numWrps);
        return probe;
    }

    xc::BreakpointSetResponse set(const xc::BreakpointSetRequest& request) {
        xc::BreakpointSetResponse response;
        response.slot = request.slot;
        response.address = request.address;
        response.type = request.type;
        response.size = request.size;

        const std::string error = validate(request);
        if (!error.empty()) {
            response.error = error;
            return response;
        }
        if (!prepareTarget(request.target, response.error)) {
            return response;
        }
        if (!request.module.empty()) {
            std::uint64_t moduleBase = 0;
            int mapsPid = req_->pid;
            if (mapsPid <= 0 && !findPidByName(request.target, mapsPid, response.error)) {
                return response;
            }
            if (!resolveModuleBaseFromMaps(mapsPid, request.module, moduleBase, response.error)) {
                return response;
            }
            response.address = moduleBase + request.offset;
        }

        bpInfo_.points[request.slot].bt = toDriverType(request.type);
        bpInfo_.points[request.slot].bl = static_cast<hwbp_len>(request.size);
        bpInfo_.points[request.slot].bs = SCOPE_ALL_THREADS;
        bpInfo_.points[request.slot].hit_addr = response.address;
        bpInfo_.points[request.slot].record_count = 0;
        slots_[request.slot] = BreakpointSlot{true, response.address, request.type, request.size};
        req_->bp_info = bpInfo_;

        if (!commit(op_set_process_hwbp)) {
            response.error = lastError_;
            return response;
        }
        if (req_->status != 0) {
            response.error = "lsdriver op_set_process_hwbp failed: " + std::to_string(req_->status);
            return response;
        }

        bpInfo_ = req_->bp_info;
        response.ok = true;
        response.enabled = true;
        response.message = "lsdriver 已写入硬件断点 slot" + std::to_string(request.slot)
            + " target=" + request.target;
        return response;
    }

    xc::BreakpointRemoveResponse remove(const xc::BreakpointRemoveRequest& request) {
        xc::BreakpointRemoveResponse response;
        response.slot = request.slot;
        if (request.slot >= kMaxBreakpointSlots) {
            response.error = "slot out of range";
            return response;
        }
        if (request.target.empty()) {
            response.error = "target pid or process name is empty";
            return response;
        }
        if (!prepareTarget(request.target, response.error)) {
            return response;
        }

        bpInfo_.points[request.slot] = {};
        bpInfo_.points[request.slot].bt = HWBP_BREAKPOINT_EMPTY;
        slots_[request.slot] = {};
        req_->bp_info = bpInfo_;

        if (!commit(op_remove_process_hwbp)) {
            response.error = lastError_;
            return response;
        }
        if (req_->status != 0) {
            response.error = "lsdriver op_remove_process_hwbp failed: " + std::to_string(req_->status);
            return response;
        }

        bpInfo_ = req_->bp_info;
        response.ok = true;
        response.enabled = false;
        response.message = "lsdriver 已删除硬件断点 slot" + std::to_string(request.slot)
            + " target=" + request.target;
        return response;
    }

    hwbp_info info(std::string& error) {
        error.clear();
        if (!ensureMapped()) {
            error = lastError_;
            return {};
        }
        if (!waitForDriver(kLsdriverConnectTimeoutMs)) {
            error = "lsdriver did not attach shared memory at 0x2025827000";
            return {};
        }
        if (!commit(op_brps_weps_info)) {
            error = lastError_;
            return {};
        }
        if (req_->status != 0) {
            error = "lsdriver op_brps_weps_info failed: " + std::to_string(req_->status);
            return {};
        }
        bpInfo_ = req_->bp_info;
        return bpInfo_;
    }

private:
    bool ensureMapped() {
        if (req_ && req_ != MAP_FAILED) {
            return true;
        }
        return open();
    }

    bool open() {
        ::prctl(PR_SET_NAME, "LS", 0, 0, 0);
        void* mapped = ::mmap(reinterpret_cast<void*>(kLsdriverSharedAddress), sizeof(req_obj),
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (mapped == MAP_FAILED) {
            lastError_ = std::string("mmap 0x2025827000 failed: ") + std::strerror(errno);
            req_ = nullptr;
            return false;
        }
        req_ = static_cast<req_obj*>(mapped);
        std::memset(req_, 0, sizeof(req_obj));
        return true;
    }

    bool waitForDriver(std::size_t timeoutMs) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            if (req_->user) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }

    bool commit(sm_req_op op) {
        req_->op = op;
        req_->status = 0;
        req_->user = false;
        __sync_synchronize();
        req_->kernel = true;
        __sync_synchronize();

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kLsdriverCommitTimeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            __sync_synchronize();
            if (req_->user) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        lastError_ = "lsdriver request timed out";
        return false;
    }

    bool prepareTarget(const std::string& target, std::string& error) {
        if (!ensureMapped()) {
            error = lastError_;
            return false;
        }
        if (!waitForDriver(kLsdriverConnectTimeoutMs)) {
            error = "lsdriver did not attach shared memory at 0x2025827000";
            return false;
        }
        req_->pid = 0;
        std::memset(&req_->proc_info, 0, sizeof(req_->proc_info));
        if (isDigits(target)) {
            req_->pid = std::stoi(target);
        } else {
            std::snprintf(req_->proc_info.name, sizeof(req_->proc_info.name), "%s", target.c_str());
        }
        return true;
    }

    std::string validate(const xc::BreakpointSetRequest& request) const {
        if (request.slot >= kMaxBreakpointSlots) {
            return "slot out of range";
        }
        if (request.module.empty() && request.address == 0) {
            return "address is zero";
        }
        if (request.type != "execute" && request.type != "read" && request.type != "write" && request.type != "access") {
            return "unsupported breakpoint type";
        }
        if (request.size < 1 || request.size > 8) {
            return "unsupported breakpoint size";
        }
        if (request.target.empty()) {
            return "target pid or process name is empty";
        }
        return {};
    }

    static hwbp_type toDriverType(const std::string& type) {
        if (type == "execute") {
            return HWBP_BREAKPOINT_X;
        }
        if (type == "read") {
            return HWBP_BREAKPOINT_R;
        }
        if (type == "write") {
            return HWBP_BREAKPOINT_W;
        }
        return HWBP_BREAKPOINT_RW;
    }

    req_obj* req_ = nullptr;
    hwbp_info bpInfo_{};
    std::array<BreakpointSlot, kMaxBreakpointSlots> slots_{};
    std::string lastError_;
};

std::string driverStatusJson(std::uint64_t id, const DriverProbe& probe) {
    return "{\"id\":" + std::to_string(id)
        + ",\"ok\":true,\"driver\":{\"name\":\"lsdriver\",\"module_loaded\":"
        + jsonBool(probe.moduleLoaded)
        + ",\"proc_modules_readable\":" + jsonBool(probe.procModulesReadable)
        + ",\"shared_memory_online\":" + jsonBool(probe.sharedMemoryOnline)
        + ",\"num_brps\":" + std::to_string(probe.numBrps)
        + ",\"num_wrps\":" + std::to_string(probe.numWrps)
        + ",\"kernel_release\":\"" + escapeJson(probe.kernelRelease)
        + "\",\"message\":\"" + escapeJson(probe.message) + "\"}}";
}
std::string breakpointSetJson(std::uint64_t id, const xc::BreakpointSetResponse& response) {
    if (!response.ok) {
        return "{\"id\":" + std::to_string(id)
            + ",\"ok\":false,\"error\":\"" + escapeJson(response.error) + "\"}";
    }
    return "{\"id\":" + std::to_string(id)
        + ",\"ok\":true,\"breakpoint\":{\"slot\":" + std::to_string(response.slot)
        + ",\"address\":\"" + xc::hexAddress(response.address)
        + "\",\"type\":\"" + escapeJson(response.type)
        + "\",\"size\":" + std::to_string(response.size)
        + ",\"enabled\":" + jsonBool(response.enabled)
        + ",\"message\":\"" + escapeJson(response.message) + "\"}}";
}

std::string breakpointRemoveJson(std::uint64_t id, const xc::BreakpointRemoveResponse& response) {
    if (!response.ok) {
        return "{\"id\":" + std::to_string(id)
            + ",\"ok\":false,\"error\":\"" + escapeJson(response.error) + "\"}";
    }
    return "{\"id\":" + std::to_string(id)
        + ",\"ok\":true,\"breakpoint\":{\"slot\":" + std::to_string(response.slot)
        + ",\"enabled\":" + jsonBool(response.enabled)
        + ",\"message\":\"" + escapeJson(response.message) + "\"}}";
}

std::string breakpointInfoJson(std::uint64_t id, const hwbp_info& info) {
    std::string out = "{\"id\":" + std::to_string(id)
        + ",\"ok\":true,\"breakpoints\":{\"num_brps\":" + std::to_string(info.num_brps)
        + ",\"num_wrps\":" + std::to_string(info.num_wrps)
        + ",\"points\":[";
    for (std::size_t i = 0; i < 16; ++i) {
        if (i != 0) {
            out += ",";
        }
        const hwbp_point& point = info.points[i];
        out += "{\"slot\":" + std::to_string(i)
            + ",\"type\":" + std::to_string(static_cast<int>(point.bt))
            + ",\"size\":" + std::to_string(static_cast<int>(point.bl))
            + ",\"address\":\"" + xc::hexAddress(point.hit_addr)
            + "\",\"records\":" + std::to_string(point.record_count)
            + "}";
    }
    out += "]}}";
    return out;
}

void printStartupLog(const DriverProbe& probe, std::uint16_t port) {
    std::cout << "[agent] xc-hwbp-agent 启动中\n";
    std::cout << "[agent] 协议: " << xc::kProtocolName << " v" << xc::kProtocolVersion << "\n";
    std::cout << "[agent] 进程名: LS\n";
    std::cout << "[agent] 共享内存: 0x2025827000\n";
    std::cout << "[agent] 监听地址: 0.0.0.0:" << port << "\n";
    std::cout << "[agent] 内核版本: " << probe.kernelRelease << "\n";
    std::cout << "[driver] 状态: " << probe.message << "\n";
}

int createServer(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int enabled = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 4) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void sendLine(int fd, const std::string& line) {
    std::string data = line + "\n";
    const char* ptr = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t written = ::send(fd, ptr, remaining, 0);
        if (written <= 0) {
            return;
        }
        ptr += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

void handleClient(int clientFd, LsdriverBackend& breakpoints) {
    std::cout << "[net] 客户端已连接\n";
    sendLine(clientFd, xc::helloResponseJson(0));

    char buffer[2048];
    while (true) {
        const ssize_t readCount = ::recv(clientFd, buffer, sizeof(buffer) - 1, 0);
        if (readCount <= 0) {
            break;
        }
        buffer[readCount] = '\0';
        const std::string request(buffer);
        const std::uint64_t requestId = xc::jsonUint64Value(request, "id");

        if (request.find("hello") != std::string::npos) {
            sendLine(clientFd, xc::helloResponseJson(requestId == 0 ? 1 : requestId));
        } else if (request.find("driver.status") != std::string::npos) {
            sendLine(clientFd, driverStatusJson(requestId == 0 ? 1 : requestId, breakpoints.probe()));
        } else if (request.find("breakpoint.info") != std::string::npos) {
            std::string error;
            const hwbp_info info = breakpoints.info(error);
            if (error.empty()) {
                sendLine(clientFd, breakpointInfoJson(requestId == 0 ? 1 : requestId, info));
            } else {
                sendLine(clientFd, "{\"id\":" + std::to_string(requestId == 0 ? 1 : requestId) + ",\"ok\":false,\"error\":\"" + escapeJson(error) + "\"}");
            }
        } else if (request.find("breakpoint.set") != std::string::npos) {
            const xc::BreakpointSetRequest setRequest = xc::parseBreakpointSetRequest(request);
            const xc::BreakpointSetResponse response = breakpoints.set(setRequest);
            sendLine(clientFd, breakpointSetJson(requestId == 0 ? setRequest.id : requestId, response));
        } else if (request.find("breakpoint.remove") != std::string::npos) {
            const xc::BreakpointRemoveRequest removeRequest = xc::parseBreakpointRemoveRequest(request);
            const xc::BreakpointRemoveResponse response = breakpoints.remove(removeRequest);
            sendLine(clientFd, breakpointRemoveJson(requestId == 0 ? removeRequest.id : requestId, response));
        } else {
            sendLine(clientFd, "{\"id\":" + std::to_string(requestId == 0 ? 1 : requestId) + ",\"ok\":false,\"error\":\"command not implemented\"}");
        }
    }
    std::cout << "[net] 客户端已断开\n";
}

} // namespace

int main(int argc, char** argv) {
    std::uint16_t port = xc::kDefaultAgentPort;
    if (argc > 1) {
        port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    }

    LsdriverBackend breakpoints;
    const DriverProbe startupProbe = breakpoints.probe();
    printStartupLog(startupProbe, port);

    const int serverFd = createServer(port);
    if (serverFd < 0) {
        std::cerr << "[net] 监听端口失败 " << port << ": " << std::strerror(errno) << "\n";
        return 1;
    }

    std::cout << "[net] 已开始监听，等待 PC 客户端连接\n";
    while (true) {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);
        const int clientFd = ::accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (clientFd < 0) {
            continue;
        }
        handleClient(clientFd, breakpoints);
        ::close(clientFd);
    }
}
