#include "eui_neo.h"
#include "xc/protocol.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace app {
namespace {

using eui::Color;

constexpr Color kWindow{0.12f, 0.12f, 0.14f, 1.0f};
constexpr Color kTitlebar{0.95f, 0.95f, 0.95f, 1.0f};
constexpr Color kTopbar{0.10f, 0.10f, 0.12f, 1.0f};
constexpr Color kToolbar{0.13f, 0.13f, 0.15f, 1.0f};
constexpr Color kPanel{0.11f, 0.11f, 0.13f, 1.0f};
constexpr Color kHeader{0.15f, 0.15f, 0.20f, 1.0f};
constexpr Color kLine{0.28f, 0.29f, 0.34f, 1.0f};
constexpr Color kSelect{0.00f, 0.47f, 0.84f, 1.0f};
constexpr Color kText{0.82f, 0.85f, 0.89f, 1.0f};
constexpr Color kMuted{0.50f, 0.53f, 0.58f, 1.0f};
constexpr Color kCyan{0.25f, 0.84f, 1.00f, 1.0f};
constexpr Color kYellow{0.90f, 0.83f, 0.42f, 1.0f};
constexpr Color kRed{1.00f, 0.42f, 0.42f, 1.0f};
constexpr Color kButton{0.15f, 0.15f, 0.20f, 1.0f};
constexpr Color kInput{0.08f, 0.08f, 0.10f, 1.0f};

float clampWidth(float width) { return std::max(width, 980.0f); }
float clampHeight(float height) { return std::max(height, 680.0f); }

void rect(eui::Ui& ui, const std::string& id, float x, float y, float w, float h, Color color, Color border = kLine) {
    ui.rect(id).position(x, y).size(w, h).color(color).border(1.0f, border).build();
}

void fillRect(eui::Ui& ui, const std::string& id, float x, float y, float w, float h, Color color) {
    ui.rect(id).position(x, y).size(w, h).color(color).build();
}

void text(eui::Ui& ui, const std::string& id, float x, float y, const std::string& value, Color color = kText, float size = 14.0f) {
    ui.text(id).position(x, y).text(value).fontSize(size).color(color).build();
}

void mono(eui::Ui& ui, const std::string& id, float x, float y, const std::string& value, Color color = kText, float size = 13.0f) {
    ui.text(id).position(x, y).text(value).fontSize(size).fontFamily("Consolas").color(color).build();
}

struct ClientState {
    bool connected = false;
    bool helloOk = false;
    xc::HelloResponse hello;
    xc::DriverStatusResponse driver;
    xc::BreakpointSetResponse breakpoints[4];
    std::string endpoint = "192.168.1.10";
    std::string target = "com.tencent.tmgp.sgame";
    std::uint64_t breakpointAddress = 0x78919CFF84ULL;
    std::string breakpointType = "execute";
    std::uint32_t breakpointSize = 4;
    std::string lastError = "未连接";
    std::string lastAction = "就绪: 等待连接手机 agent";
    std::uint64_t requestId = 1;
};

ClientState& state() {
    static ClientState value;
    return value;
}

std::mutex& stateMutex() {
    static std::mutex value;
    return value;
}

std::uint64_t nextRequestId() {
    std::lock_guard<std::mutex> lock(stateMutex());
    return state().requestId++;
}

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void closeSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void closeSocket(SocketHandle socket) { ::close(socket); }
#endif

bool ensureSocketRuntime(std::string& error) {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            error = "WSAStartup 初始化失败";
            return false;
        }
        initialized = true;
    }
#else
    (void)error;
#endif
    return true;
}

bool sendAll(SocketHandle socket, const std::string& data) {
    const char* ptr = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
#ifdef _WIN32
        const int sent = ::send(socket, ptr, static_cast<int>(remaining), 0);
#else
        const ssize_t sent = ::send(socket, ptr, remaining, 0);
#endif
        if (sent <= 0) {
            return false;
        }
        ptr += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool sendLine(SocketHandle socket, const std::string& line) {
    return sendAll(socket, line + "\n");
}

std::string receiveLine(SocketHandle socket, std::string& error) {
    std::string line;
    std::array<char, 1> ch{};
    while (line.size() < 64 * 1024) {
#ifdef _WIN32
        const int received = ::recv(socket, ch.data(), 1, 0);
#else
        const ssize_t received = ::recv(socket, ch.data(), 1, 0);
#endif
        if (received <= 0) {
            error = line.empty() ? "连接已关闭，未收到响应" : "连接已关闭，响应不完整";
            return {};
        }
        if (ch[0] == '\n') {
            return line;
        }
        if (ch[0] != '\r') {
            line.push_back(ch[0]);
        }
    }
    error = "响应超过 64KB，协议异常";
    return {};
}

SocketHandle connectSocket(const std::string& host, std::uint16_t port, std::string& error) {
    if (!ensureSocketRuntime(error)) {
        return kInvalidSocket;
    }

    SocketHandle socketFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd == kInvalidSocket) {
        error = "创建 socket 失败";
        return kInvalidSocket;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        error = "IP 地址格式无效: " + host;
        closeSocket(socketFd);
        return kInvalidSocket;
    }

    if (::connect(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "连接失败: " + host + ":" + std::to_string(port);
        closeSocket(socketFd);
        return kInvalidSocket;
    }

    return socketFd;
}

void connectAndProbeAgent() {
    ClientState& s = state();
    s.lastAction = "正在连接 " + s.endpoint + ":" + std::to_string(xc::kDefaultAgentPort);
    s.connected = false;
    s.helloOk = false;
    s.driver = {};
    s.hello = {};

    std::string error;
    const SocketHandle socket = connectSocket(s.endpoint, xc::kDefaultAgentPort, error);
    if (socket == kInvalidSocket) {
        s.lastError = error;
        s.lastAction = "连接失败: " + error;
        return;
    }

    const std::string helloLine = receiveLine(socket, error);
    if (helloLine.empty()) {
        s.lastError = error;
        s.lastAction = "连接失败: " + error;
        closeSocket(socket);
        return;
    }

    s.hello = xc::parseHelloResponse(helloLine);
    s.helloOk = s.hello.ok && s.hello.protocol == xc::kProtocolName && s.hello.version == xc::kProtocolVersion;
    if (!s.helloOk) {
        s.lastError = "Agent hello 不匹配: " + helloLine;
        s.lastAction = "协议握手失败";
        closeSocket(socket);
        return;
    }

    if (!sendLine(socket, xc::driverStatusRequestJson(nextRequestId()))) {
        s.lastError = "发送 driver.status 失败";
        s.lastAction = "驱动检测请求失败";
        closeSocket(socket);
        return;
    }

    const std::string statusLine = receiveLine(socket, error);
    if (statusLine.empty()) {
        s.lastError = error;
        s.lastAction = "驱动检测失败: " + error;
        closeSocket(socket);
        return;
    }

    s.driver = xc::parseDriverStatusResponse(statusLine);
    s.connected = true;
    s.lastError.clear();
    s.lastAction = s.driver.ok ? "已连接 Agent，驱动状态已刷新" : "已连接 Agent，但驱动状态返回错误";
    closeSocket(socket);
}

void setBreakpointSlot(std::uint32_t slot) {
    ClientState& s = state();
    s.lastAction = "正在写入硬件断点 slot" + std::to_string(slot);

    std::string error;
    const SocketHandle socket = connectSocket(s.endpoint, xc::kDefaultAgentPort, error);
    if (socket == kInvalidSocket) {
        s.lastError = error;
        s.lastAction = "下断失败: " + error;
        return;
    }

    const std::string helloLine = receiveLine(socket, error);
    const xc::HelloResponse hello = xc::parseHelloResponse(helloLine);
    if (!hello.ok || hello.protocol != xc::kProtocolName || hello.version != xc::kProtocolVersion) {
        s.lastError = helloLine.empty() ? error : "Agent hello 不匹配: " + helloLine;
        s.lastAction = "下断失败: 协议握手失败";
        closeSocket(socket);
        return;
    }

    const std::uint64_t requestId = nextRequestId();
    const std::string request = xc::breakpointSetRequestJson(requestId, slot, s.breakpointAddress, s.breakpointType, s.breakpointSize, s.target);
    if (!sendLine(socket, request)) {
        s.lastError = "发送 breakpoint.set 失败";
        s.lastAction = "下断请求发送失败";
        closeSocket(socket);
        return;
    }

    const std::string responseLine = receiveLine(socket, error);
    if (responseLine.empty()) {
        s.lastError = error;
        s.lastAction = "下断失败: " + error;
        closeSocket(socket);
        return;
    }

    const xc::BreakpointSetResponse response = xc::parseBreakpointSetResponse(responseLine);
    if (slot < 4) {
        s.breakpoints[slot] = response;
    }
    s.connected = true;
    s.hello = hello;
    s.helloOk = true;
    s.lastError = response.ok ? "" : response.error;
    s.lastAction = response.ok ? response.message : "下断失败: " + response.error;
    closeSocket(socket);
}

void removeBreakpointSlot(std::uint32_t slot) {
    ClientState& s = state();
    s.lastAction = "正在删除硬件断点 slot" + std::to_string(slot);

    std::string error;
    const SocketHandle socket = connectSocket(s.endpoint, xc::kDefaultAgentPort, error);
    if (socket == kInvalidSocket) {
        s.lastError = error;
        s.lastAction = "删断失败: " + error;
        return;
    }

    const std::string helloLine = receiveLine(socket, error);
    const xc::HelloResponse hello = xc::parseHelloResponse(helloLine);
    if (!hello.ok || hello.protocol != xc::kProtocolName || hello.version != xc::kProtocolVersion) {
        s.lastError = helloLine.empty() ? error : "Agent hello 不匹配: " + helloLine;
        s.lastAction = "删断失败: 协议握手失败";
        closeSocket(socket);
        return;
    }

    const std::uint64_t requestId = nextRequestId();
    const std::string request = xc::breakpointRemoveRequestJson(requestId, slot, s.target);
    if (!sendLine(socket, request)) {
        s.lastError = "发送 breakpoint.remove 失败";
        s.lastAction = "删断请求发送失败";
        closeSocket(socket);
        return;
    }

    const std::string responseLine = receiveLine(socket, error);
    if (responseLine.empty()) {
        s.lastError = error;
        s.lastAction = "删断失败: " + error;
        closeSocket(socket);
        return;
    }

    const xc::BreakpointRemoveResponse response = xc::parseBreakpointRemoveResponse(responseLine);
    if (response.ok && slot < 4) {
        s.breakpoints[slot] = {};
    }
    s.connected = true;
    s.hello = hello;
    s.helloOk = true;
    s.lastError = response.ok ? "" : response.error;
    s.lastAction = response.ok ? response.message : "删断失败: " + response.error;
    closeSocket(socket);
}

std::string jsonEscape(const std::string& value);

std::string queryBreakpointInfoRaw() {
    ClientState& s = state();
    s.lastAction = "正在查询硬件断点信息";

    std::string error;
    const SocketHandle socket = connectSocket(s.endpoint, xc::kDefaultAgentPort, error);
    if (socket == kInvalidSocket) {
        s.lastError = error;
        s.lastAction = "查询断点失败: " + error;
        return "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}";
    }

    const std::string helloLine = receiveLine(socket, error);
    const xc::HelloResponse hello = xc::parseHelloResponse(helloLine);
    if (!hello.ok || hello.protocol != xc::kProtocolName || hello.version != xc::kProtocolVersion) {
        const std::string message = helloLine.empty() ? error : "Agent hello 不匹配: " + helloLine;
        s.lastError = message;
        s.lastAction = "查询断点失败: 协议握手失败";
        closeSocket(socket);
        return "{\"ok\":false,\"error\":\"" + jsonEscape(message) + "\"}";
    }

    if (!sendLine(socket, xc::breakpointInfoRequestJson(nextRequestId()))) {
        s.lastError = "发送 breakpoint.info 失败";
        s.lastAction = "查询断点请求发送失败";
        closeSocket(socket);
        return "{\"ok\":false,\"error\":\"发送 breakpoint.info 失败\"}";
    }

    const std::string responseLine = receiveLine(socket, error);
    closeSocket(socket);
    if (responseLine.empty()) {
        s.lastError = error;
        s.lastAction = "查询断点失败: " + error;
        return "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}";
    }

    s.connected = true;
    s.hello = hello;
    s.helloOk = true;
    s.lastError.clear();
    s.lastAction = "硬件断点信息已刷新";
    return responseLine;
}


std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string boolJson(bool value) {
    return value ? "true" : "false";
}

std::string stateJson() {
    ClientState snapshot;
    {
        std::lock_guard<std::mutex> lock(stateMutex());
        snapshot = state();
    }

    std::ostringstream out;
    out << "{\"connected\":" << boolJson(snapshot.connected)
        << ",\"hello_ok\":" << boolJson(snapshot.helloOk)
        << ",\"endpoint\":\"" << jsonEscape(snapshot.endpoint) << "\""
        << ",\"target\":\"" << jsonEscape(snapshot.target) << "\""
        << ",\"breakpoint_address\":\"" << xc::hexAddress(snapshot.breakpointAddress) << "\""
        << ",\"breakpoint_type\":\"" << jsonEscape(snapshot.breakpointType) << "\""
        << ",\"breakpoint_size\":" << snapshot.breakpointSize
        << ",\"last_action\":\"" << jsonEscape(snapshot.lastAction) << "\""
        << ",\"last_error\":\"" << jsonEscape(snapshot.lastError) << "\""
        << ",\"driver\":{\"module_loaded\":" << boolJson(snapshot.driver.moduleLoaded)
        << ",\"proc_modules_readable\":" << boolJson(snapshot.driver.procModulesReadable)
        << ",\"kernel_release\":\"" << jsonEscape(snapshot.driver.kernelRelease) << "\""
        << ",\"message\":\"" << jsonEscape(snapshot.driver.message) << "\"}"
        << ",\"breakpoints\":[";
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& bp = snapshot.breakpoints[i];
        if (i != 0) {
            out << ",";
        }
        out << "{\"slot\":" << i
            << ",\"ok\":" << boolJson(bp.ok)
            << ",\"enabled\":" << boolJson(bp.enabled)
            << ",\"address\":\"" << xc::hexAddress(bp.address) << "\""
            << ",\"type\":\"" << jsonEscape(bp.type) << "\""
            << ",\"size\":" << bp.size
            << ",\"message\":\"" << jsonEscape(bp.message) << "\""
            << ",\"error\":\"" << jsonEscape(bp.error) << "\"}";
    }
    out << "]}";
    return out.str();
}

void applyMcpArguments(const std::string& json) {
    std::lock_guard<std::mutex> lock(stateMutex());
    ClientState& s = state();
    const std::string endpoint = xc::jsonStringValue(json, "endpoint");
    if (!endpoint.empty()) {
        s.endpoint = endpoint;
    }
    const std::string target = xc::jsonStringValue(json, "target");
    if (!target.empty()) {
        s.target = target;
    }
    const std::uint64_t address = xc::jsonUint64Value(json, "address");
    if (address != 0) {
        s.breakpointAddress = address;
    }
    const std::string type = xc::jsonStringValue(json, "type");
    if (!type.empty()) {
        s.breakpointType = type;
    }
    const std::uint32_t size = xc::jsonUint32Value(json, "size");
    if (size != 0) {
        s.breakpointSize = size;
    }
}

void disconnectAgent() {
    ClientState& s = state();
    s.connected = false;
    s.helloOk = false;
    s.hello = {};
    s.driver = {};
    for (auto& breakpoint : s.breakpoints) {
        breakpoint = {};
    }
    s.lastError = "已断开";
    s.lastAction = "已断开连接";
}

void button(eui::Ui& ui, const std::string& id, float x, float y, float w, const std::string& label, std::function<void()> onClick = {}) {
    auto builder = ui.rect(id + ".bg").position(x, y).size(w, 22.0f).color(kButton).border(1.0f, kLine);
    if (onClick) {
        builder.onClick(std::move(onClick));
    }
    builder.build();
    text(ui, id + ".label", x + 14.0f, y + 3.0f, label, kText, 13.0f);
}

void panel(eui::Ui& ui, const std::string& id, float x, float y, float w, float h, const std::string& title) {
    rect(ui, id, x, y, w, h, kPanel, kLine);
    fillRect(ui, id + ".header", x, y, w, 26.0f, kHeader);
    text(ui, id + ".title", x + 10.0f, y + 4.0f, title, kText, 15.0f);
}

std::string breakpointSlotLine(std::uint32_t slot, const xc::BreakpointSetResponse& bp) {
    if (!bp.ok) {
        return std::to_string(slot) + "  -     -           空";
    }
    return std::to_string(slot) + "  " + bp.type + "  " + xc::hexAddress(bp.address) + "  开启";
}

std::vector<std::string> registerLines() {
    return {
        "x0   0000000000000058   DEC:88           HEX:0x58",
        "x1   0000007A42869674   DEC:525102126708 HEX:0x7A42869674   [stack]+0xF7674",
        "x2   0000000000000000   DEC:0            HEX:0x0",
        "x3   0000000000000000   DEC:0            HEX:0x0",
        "x4   0000000000000000   DEC:0            HEX:0x0",
        "x5   0000000000006123   DEC:24867        HEX:0x6123",
        "x6   00000000000001AA   DEC:426          HEX:0x1AA",
        "x7   0000000000006123   DEC:24867        HEX:0x6123",
        "x8   164610C9BA59984E   DEC:866414860366 HEX:0x164610C9BA59984E",
        "x9   0000000000000000   DEC:0            HEX:0x0",
        "x10  00000000EE7E1856   DEC:4001241174   HEX:0xEE7E1856",
        "x11  0000007891D4ED30   DEC:517842726192 HEX:0x7891D4ED30   libdemo.so+0x53BD30",
        "x12  00000000102D7F70   DEC:27141720     HEX:0x102D7F70",
        "x13  00000000000004E7   DEC:1255         HEX:0x4E7",
        "x14  00000007C0000360   DEC:532575945568 HEX:0x7C0000360    [cfi shadow]+0x6984D360",
        "x15  000000000CAA098E   DEC:3298859199   HEX:0xCAA098E",
        "x16  0000007891D2FD20   DEC:517842599200 HEX:0x7891D2FD20   libdemo.so+0x51CD20",
        "x17  00000078919CFF84   DEC:517839060868 HEX:0x78919CFF84   libdemo.so+0x1BCF84",
        "x18  00000077FDD4C000   DEC:515359686656 HEX:0x77FDD4C000",
        "x19  0000007A42869A80   DEC:525102127744 HEX:0x7A42869A80   [stack]+0xF7A80",
        "x20  0000007A42869730   DEC:525102126896 HEX:0x7A42869730   [stack]+0xF7730",
        "x21  0000007A42869730   DEC:525102126896 HEX:0x7A42869730   [stack]+0xF7730",
        "x22  00000000000071C5   DEC:29125        HEX:0x71C5",
        "x23  0000000000007731   DEC:30513        HEX:0x7731",
        "x24  0000007A42869730   DEC:525102126896 HEX:0x7A42869730   [stack]+0xF7730",
        "x25  0000007A42869730   DEC:525102126896 HEX:0x7A42869730   [stack]+0xF7730",
        "x26  0000007A42869A70   DEC:525102127728 HEX:0x7A42869A70   [stack]+0xF7A70",
        "x27  00000000000FC000   DEC:1032192      HEX:0xFC000",
        "x28  0000007A42771000   DEC:525101103248 HEX:0x7A42771000   [stack]+0x0",
        "x29  0000007A42869690   DEC:525102126736 HEX:0x7A42869690   [stack]+0xF7690",
        "x30  0000007891B08068   DEC:517840339048 HEX:0x7891B08068   libdemo.so+0x2F5068",
        "SP   0000007A42869670   DEC:525102126704 HEX:0x7A42869670   [stack]+0xF7670",
        "PC   00000078919CFF84   DEC:517839060868 HEX:0x78919CFF84   libdemo.so+0x1BCF84"
    };
}


std::string mcpToolsListJson(std::uint64_t id) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id)
        + ",\"result\":{\"tools\":["
        "{\"name\":\"get_state\",\"description\":\"Read PC client, agent and breakpoint state\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
        "{\"name\":\"connect_agent\",\"description\":\"Connect to Android agent and refresh driver status\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"endpoint\":{\"type\":\"string\"}}}},"
        "{\"name\":\"driver_status\",\"description\":\"Refresh lsdriver status through agent\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"endpoint\":{\"type\":\"string\"}}}},"
        "{\"name\":\"breakpoint_set\",\"description\":\"Set hardware breakpoint through lsdriver\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\"},\"address\":{\"type\":\"string\"},\"target\":{\"type\":\"string\"},\"type\":{\"type\":\"string\"},\"size\":{\"type\":\"integer\"}}}},"
        "{\"name\":\"breakpoint_remove\",\"description\":\"Remove hardware breakpoint through lsdriver\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\"},\"target\":{\"type\":\"string\"}}}},"
        "{\"name\":\"breakpoint_info\",\"description\":\"Read raw breakpoint/register-hit info from agent\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"endpoint\":{\"type\":\"string\"}}}}]}}";
}

std::string mcpResultJson(std::uint64_t id, const std::string& text) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id)
        + ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\""
        + jsonEscape(text) + "\"}]}}";
}

std::string mcpErrorJson(std::uint64_t id, int code, const std::string& message) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id)
        + ",\"error\":{\"code\":" + std::to_string(code)
        + ",\"message\":\"" + jsonEscape(message) + "\"}}";
}

std::string handleMcpRequest(const std::string& line) {
    const std::uint64_t id = xc::jsonUint64Value(line, "id");
    const std::string method = xc::jsonStringValue(line, "method");
    if (method == "initialize") {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id)
            + ",\"result\":{\"protocolVersion\":\"2024-11-05\",\"serverInfo\":{\"name\":\"xc-hwbp-debugger-pc\",\"version\":\"0.1.0\"},\"capabilities\":{\"tools\":{}}}}";
    }
    if (method == "notifications/initialized") {
        return {};
    }
    if (method == "tools/list") {
        return mcpToolsListJson(id);
    }
    if (method != "tools/call") {
        return mcpErrorJson(id, -32601, "unknown MCP method: " + method);
    }

    const std::string name = xc::jsonStringValue(line, "name");
    if (name == "get_state") {
        return mcpResultJson(id, stateJson());
    }
    if (name == "connect_agent" || name == "driver_status") {
        applyMcpArguments(line);
        connectAndProbeAgent();
        return mcpResultJson(id, stateJson());
    }
    if (name == "breakpoint_set") {
        applyMcpArguments(line);
        const std::uint32_t slot = xc::jsonUint32Value(line, "slot");
        setBreakpointSlot(slot);
        return mcpResultJson(id, stateJson());
    }
    if (name == "breakpoint_remove") {
        applyMcpArguments(line);
        const std::uint32_t slot = xc::jsonUint32Value(line, "slot");
        removeBreakpointSlot(slot);
        return mcpResultJson(id, stateJson());
    }
    if (name == "breakpoint_info") {
        applyMcpArguments(line);
        return mcpResultJson(id, queryBreakpointInfoRaw());
    }
    return mcpErrorJson(id, -32602, "unknown tool: " + name);
}

void mcpClientLoop(SocketHandle client) {
    std::string error;
    for (;;) {
        const std::string line = receiveLine(client, error);
        if (line.empty()) {
            break;
        }
        const std::string response = handleMcpRequest(line);
        if (!response.empty() && !sendLine(client, response)) {
            break;
        }
    }
    closeSocket(client);
}

void mcpServerLoop() {
    std::string error;
    if (!ensureSocketRuntime(error)) {
        return;
    }
    const SocketHandle server = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server == kInvalidSocket) {
        return;
    }
    int one = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(23947);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closeSocket(server);
        return;
    }
    if (::listen(server, 8) != 0) {
        closeSocket(server);
        return;
    }
    for (;;) {
        sockaddr_in clientAddress{};
#ifdef _WIN32
        int len = sizeof(clientAddress);
#else
        socklen_t len = sizeof(clientAddress);
#endif
        const SocketHandle client = ::accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &len);
        if (client == kInvalidSocket) {
            continue;
        }
        std::thread(mcpClientLoop, client).detach();
    }
}

void startMcpServerOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread(mcpServerLoop).detach();
    });
}

} // namespace

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("XC 硬件断点调试器")
        .pageId("xc_hwbp_debugger")
        .windowSize(1360, 760);
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    startMcpServerOnce();
    const float width = clampWidth(screen.width);
    const float height = clampHeight(screen.height);
    const float leftWidth = 240.0f;
    const float margin = 8.0f;
    const float mainX = margin + leftWidth + 6.0f;
    const float mainW = width - mainX - margin;
    const float mainH = height - 142.0f;

    fillRect(ui, "window.bg", 0.0f, 0.0f, width, height, kWindow);
    fillRect(ui, "titlebar", 0.0f, 0.0f, width, 28.0f, kTitlebar);
    text(ui, "titlebar.name", 12.0f, 5.0f, "XC 硬件断点调试器", {0.05f, 0.05f, 0.06f, 1.0f}, 14.0f);
    text(ui, "titlebar.window", width - 78.0f, 5.0f, "—  □  ×", {0.05f, 0.05f, 0.06f, 1.0f}, 14.0f);

    rect(ui, "topbar", 0.0f, 28.0f, width, 34.0f, kTopbar, kLine);
    rect(ui, "endpoint.input", 18.0f, 35.0f, 180.0f, 20.0f, kInput, kLine);
    ClientState& client = state();
    text(ui, "endpoint.input.text", 28.0f, 38.0f, client.endpoint, kText, 13.0f);
    button(ui, "connect", 220.0f, 34.0f, 62.0f, "连接", [] { connectAndProbeAgent(); });
    button(ui, "disconnect", 288.0f, 34.0f, 62.0f, "断开", [] { disconnectAgent(); });
    button(ui, "probe", 356.0f, 34.0f, 86.0f, "检测驱动", [] { connectAndProbeAgent(); });
    button(ui, "bp.set0", 448.0f, 34.0f, 76.0f, "下断S0", [] { setBreakpointSlot(0); });
    button(ui, "bp.remove0", 530.0f, 34.0f, 76.0f, "删断S0", [] { removeBreakpointSlot(0); });
    text(ui, "server.status", 626.0f, 38.0f, std::string("服务器状态: ") + (client.connected ? "已连接" : "未连接") + "  端口: " + std::to_string(xc::kDefaultAgentPort), client.connected ? kCyan : kRed, 13.0f);
    text(ui, "data.count", width - 110.0f, 38.0f, "数据: 0条", kMuted, 13.0f);

    rect(ui, "sessionbar", 0.0f, 62.0f, width, 32.0f, kToolbar, kLine);
    text(ui, "session.info", 18.0f, 70.0f, client.connected ? "会话: 已连接手机 Agent，未附加目标进程" : "会话: 未附加目标进程", client.connected ? kText : kMuted, 13.0f);
    const std::string driverLine = "驱动: " + std::string(client.driver.moduleLoaded ? "已加载" : "未加载")
        + "  /proc/modules: " + std::string(client.driver.procModulesReadable ? "可读" : "未确认")
        + "  Agent: " + std::string(client.connected ? "在线" : "离线");
    text(ui, "driver.info", 230.0f, 70.0f, driverLine, client.connected && client.driver.moduleLoaded ? kCyan : kYellow, 13.0f);
    text(ui, "protocol.info", 570.0f, 70.0f, "协议: " + std::string(xc::kProtocolName) + " v" + std::to_string(xc::kProtocolVersion), kCyan, 13.0f);

    panel(ui, "watch", margin, 102.0f, leftWidth, 140.0f, "监视断点");
    fillRect(ui, "watch.selected", margin + 2.0f, 130.0f, leftWidth - 4.0f, 20.0f, kSelect);
    mono(ui, "watch.empty", 16.0f, 133.0f, "暂无命中断点", kText, 13.0f);
    mono(ui, "watch.hint1", 16.0f, 164.0f, "连接 agent 后显示", kMuted, 13.0f);
    mono(ui, "watch.hint2", 16.0f, 186.0f, "断点命中分组", kMuted, 13.0f);
    mono(ui, "watch.hint3", 16.0f, 208.0f, "#0 / TID / 栈签名", kMuted, 13.0f);

    panel(ui, "slots", margin, 250.0f, leftWidth, 220.0f, "硬件断点槽");
    mono(ui, "slots.header", 16.0f, 284.0f, "#  类型  地址        状态", kMuted, 12.0f);
    fillRect(ui, "slots.sel", margin + 2.0f, 296.0f, leftWidth - 4.0f, 20.0f, kSelect);
    mono(ui, "slots.0", 16.0f, 299.0f, breakpointSlotLine(0, client.breakpoints[0]), client.breakpoints[0].ok ? kCyan : kText, 13.0f);
    mono(ui, "slots.1", 16.0f, 323.0f, breakpointSlotLine(1, client.breakpoints[1]), client.breakpoints[1].ok ? kCyan : kMuted, 13.0f);
    mono(ui, "slots.2", 16.0f, 347.0f, breakpointSlotLine(2, client.breakpoints[2]), client.breakpoints[2].ok ? kCyan : kMuted, 13.0f);
    mono(ui, "slots.3", 16.0f, 371.0f, breakpointSlotLine(3, client.breakpoints[3]), client.breakpoints[3].ok ? kCyan : kMuted, 13.0f);
    mono(ui, "slots.note", 16.0f, 414.0f, "下断地址: " + xc::hexAddress(client.breakpointAddress), kYellow, 12.0f);
    mono(ui, "slots.note2", 16.0f, 436.0f, "目标: " + client.target, kYellow, 12.0f);

    panel(ui, "driver", margin, 478.0f, leftWidth, 150.0f, "驱动状态");
    text(ui, "driver.loaded", 16.0f, 514.0f, std::string("lsdriver: ") + (client.driver.moduleLoaded ? "已加载" : "未加载"), client.driver.moduleLoaded ? kCyan : kYellow, 13.0f);
    text(ui, "driver.modules", 16.0f, 540.0f, std::string("模块表: ") + (client.driver.procModulesReadable ? "可读取" : "未确认"), client.driver.procModulesReadable ? kText : kMuted, 13.0f);
    text(ui, "driver.agent", 16.0f, 566.0f, std::string("Agent: ") + (client.connected ? "在线" : "未连接"), client.connected ? kCyan : kRed, 13.0f);
    mono(ui, "driver.rule", 16.0f, 598.0f, "策略: 共享内存握手判断在线", kYellow, 12.0f);

    panel(ui, "main", mainX, 102.0f, mainW, mainH, "数据视图");
    mono(ui, "main.thread", mainX + 14.0f, 132.0f, client.connected ? "Tid : - | Pid : - | Agent 已连接，等待真实断点命中数据" : "Tid : - | Pid : - | 当前没有连接到手机 agent", kText, 14.0f);
    mono(ui, "main.note", mainX + 14.0f, 154.0f, client.connected ? ("Agent: " + client.hello.name + " | 内核: " + client.driver.kernelRelease + " | " + client.driver.message) : "这里显示断点命中后的寄存器、DEC/HEX、模块偏移、堆栈。当前是静态预览数据。", client.connected ? kCyan : kMuted, 13.0f);

    const auto lines = registerLines();
    float y = 182.0f;
    int idx = 0;
    for (const std::string& line : lines) {
        const Color color = line.rfind("PC", 0) == 0 || line.rfind("SP", 0) == 0 ? kCyan : (idx % 2 == 0 ? kText : Color{0.74f, 0.77f, 0.82f, 1.0f});
        mono(ui, "reg." + std::to_string(idx), mainX + 14.0f, y, line, color, 13.0f);
        y += 19.0f;
        ++idx;
        if (y > height - 116.0f) { break; }
    }

    rect(ui, "stack.panel", mainX, height - 104.0f, mainW, 66.0f, kPanel, kLine);
    text(ui, "stack.title", mainX + 10.0f, height - 96.0f, "堆栈", kText, 14.0f);
    mono(ui, "stack.0", mainX + 14.0f, height - 72.0f, "#0: 暂无真实命中；连接 agent 后显示 PC/LR 调用链", kMuted, 13.0f);
    mono(ui, "stack.1", mainX + 14.0f, height - 50.0f, "#1: 后续按 断点ID / TID / 调用栈签名 自动归类", kMuted, 13.0f);

    rect(ui, "statusbar", 0.0f, height - 30.0f, width, 30.0f, kToolbar, kLine);
    mono(ui, "status.left", 12.0f, height - 21.0f, client.lastAction, client.connected ? kCyan : kYellow, 13.0f);
    text(ui, "status.right", width - 330.0f, height - 21.0f, "Windows GUI | Android agent | MCP 127.0.0.1:23947", kMuted, 13.0f);
}

} // namespace app
