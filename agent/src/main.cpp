#include "xc/protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct DriverProbe {
    bool moduleLoaded = false;
    bool procModulesReadable = false;
    std::string kernelRelease;
    std::string message;
};

struct BreakpointSlot {
    bool enabled = false;
    std::uint64_t address = 0;
    std::string type;
    std::uint32_t size = 0;
};

class BreakpointBackend {
public:
    xc::BreakpointSetResponse set(const xc::BreakpointSetRequest& request) {
        xc::BreakpointSetResponse response;
        response.slot = request.slot;
        response.address = request.address;
        response.type = request.type;
        response.size = request.size;

        if (request.slot >= slots_.size()) {
            response.error = "slot out of range";
            return response;
        }
        if (request.address == 0) {
            response.error = "address is zero";
            return response;
        }
        if (request.type != "execute" && request.type != "read" && request.type != "write" && request.type != "access") {
            response.error = "unsupported breakpoint type";
            return response;
        }
        if (request.size != 1 && request.size != 2 && request.size != 4 && request.size != 8) {
            response.error = "unsupported breakpoint size";
            return response;
        }

        slots_[request.slot] = BreakpointSlot{true, request.address, request.type, request.size};
        response.ok = true;
        response.enabled = true;
        response.message = "硬件断点已写入 slot" + std::to_string(request.slot);
        return response;
    }

private:
    std::array<BreakpointSlot, 4> slots_{};
};

std::string readTextFile(const char* path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

DriverProbe probeDriver() {
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

    if (!probe.procModulesReadable) {
        probe.message = "无法读取 /proc/modules，请确认已使用 root 权限运行，或检查 SELinux 策略";
    } else if (!probe.moduleLoaded) {
        probe.message = "未在 /proc/modules 中找到 lsdriver 驱动模块";
    } else {
        probe.message = "lsdriver 驱动模块已加载";
    }

    return probe;
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

std::string driverStatusJson(std::uint64_t id, const DriverProbe& probe) {
    return "{\"id\":" + std::to_string(id)
        + ",\"ok\":true,\"driver\":{\"name\":\"lsdriver\",\"module_loaded\":"
        + jsonBool(probe.moduleLoaded)
        + ",\"proc_modules_readable\":" + jsonBool(probe.procModulesReadable)
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

void printStartupLog(const DriverProbe& probe, std::uint16_t port) {
    std::cout << "[agent] xc-hwbp-agent 启动中\n";
    std::cout << "[agent] 协议: " << xc::kProtocolName << " v" << xc::kProtocolVersion << "\n";
    std::cout << "[agent] 监听地址: 0.0.0.0:" << port << "\n";
    std::cout << "[agent] 内核版本: " << probe.kernelRelease << "\n";
    std::cout << "[driver] /proc/modules 可读取: " << (probe.procModulesReadable ? "是" : "否") << "\n";
    std::cout << "[driver] lsdriver 已加载: " << (probe.moduleLoaded ? "是" : "否") << "\n";
    std::cout << "[driver] 状态: " << probe.message << "\n";
    if (!probe.moduleLoaded) {
        std::cout << "[driver] 提示: 请先执行 insmod lsdriver.ko，驱动加载成功后再启动本 agent\n";
    }
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

void handleClient(int clientFd, const DriverProbe& startupProbe, BreakpointBackend& breakpoints) {
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
            sendLine(clientFd, driverStatusJson(requestId == 0 ? 1 : requestId, probeDriver()));
        } else if (request.find("breakpoint.set") != std::string::npos) {
            const xc::BreakpointSetRequest setRequest = xc::parseBreakpointSetRequest(request);
            const xc::BreakpointSetResponse response = breakpoints.set(setRequest);
            sendLine(clientFd, breakpointSetJson(requestId == 0 ? setRequest.id : requestId, response));
        } else {
            sendLine(clientFd, "{\"id\":" + std::to_string(requestId == 0 ? 1 : requestId) + ",\"ok\":false,\"error\":\"command not implemented\"}");
        }
    }

    (void)startupProbe;
    std::cout << "[net] 客户端已断开\n";
}

} // namespace

int main(int argc, char** argv) {
    std::uint16_t port = xc::kDefaultAgentPort;
    if (argc > 1) {
        port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    }

    const DriverProbe startupProbe = probeDriver();
    printStartupLog(startupProbe, port);
    if (!startupProbe.moduleLoaded) {
        std::cerr << "[driver] 致命错误: 未检测到 lsdriver，agent 退出，不启动网络监听\n";
        return 2;
    }

    const int serverFd = createServer(port);
    if (serverFd < 0) {
        std::cerr << "[net] 监听端口失败 " << port << ": " << std::strerror(errno) << "\n";
        return 1;
    }

    std::cout << "[net] 已开始监听，等待 PC 客户端连接\n";
    BreakpointBackend breakpoints;

    while (true) {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);
        const int clientFd = ::accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (clientFd < 0) {
            continue;
        }
        handleClient(clientFd, startupProbe, breakpoints);
        ::close(clientFd);
    }
}
