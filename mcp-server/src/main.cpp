#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "xc/protocol.hpp"

namespace {

[[maybe_unused]] constexpr std::string_view kMcpProtocolContract = R"("jsonrpc":"2.0" "initialize" "tools/list" "tools/call")";

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void closeSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void closeSocket(SocketHandle socket) { ::close(socket); }
#endif

struct McpState {
    SocketHandle agentSocket = kInvalidSocket;
    std::string endpoint = "192.168.1.10";
    std::string target = "com.tencent.tmgp.sgame";
    std::uint64_t requestId = 1;
    xc::HelloResponse hello;
    bool helloOk = false;
};

std::string jsonEscape(std::string_view value) {
    return xc::escapeJson(value);
}

std::string jsonString(std::string_view value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string jsonField(std::string_view key, std::string_view value) {
    return "\"" + std::string(key) + "\":" + jsonString(value);
}

std::string textResult(std::uint64_t id, std::string_view text) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id)
        + ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":" + jsonString(text) + "}]}}";
}

std::string jsonResult(std::uint64_t id, std::string_view resultJson) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":" + std::string(resultJson) + "}";
}

std::string errorResult(std::uint64_t id, int code, std::string_view message) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id)
        + ",\"error\":{\"code\":" + std::to_string(code)
        + ",\"message\":" + jsonString(message) + "}}";
}

std::uint64_t jsonRpcId(std::string_view json) {
    return xc::jsonUint64Value(json, "id");
}

std::string jsonObjectValue(std::string_view json, std::string_view key) {
    const std::string prefix = "\"" + std::string(key) + "\":{";
    const std::size_t start = json.find(prefix);
    if (start == std::string_view::npos) {
        return {};
    }
    std::size_t pos = start + prefix.size() - 1;
    int depth = 0;
    for (std::size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '{') {
            ++depth;
        } else if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                return std::string(json.substr(pos, i - pos + 1));
            }
        }
    }
    return {};
}

std::string methodName(std::string_view request) {
    return xc::jsonStringValue(request, "method");
}

std::string toolName(std::string_view request) {
    const std::string params = jsonObjectValue(request, "params");
    return xc::jsonStringValue(params, "name");
}

std::string toolArguments(std::string_view request) {
    const std::string params = jsonObjectValue(request, "params");
    return jsonObjectValue(params, "arguments");
}

std::string stringArg(std::string_view args, std::string_view key, std::string_view fallback = {}) {
    std::string value = xc::jsonStringValue(args, key);
    if (value.empty()) {
        value = std::string(fallback);
    }
    return value;
}

std::uint32_t uint32Arg(std::string_view args, std::string_view key, std::uint32_t fallback = 0) {
    const std::uint32_t value = xc::jsonUint32Value(args, key);
    return value == 0 ? fallback : value;
}

std::uint64_t uint64Arg(std::string_view args, std::string_view key, std::uint64_t fallback = 0) {
    const std::uint64_t value = xc::jsonUint64Value(args, key);
    return value == 0 ? fallback : value;
}

std::string trimAscii(std::string value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

bool parseUnsigned64(const std::string& text, std::uint64_t& value) {
    const std::string input = trimAscii(text);
    if (input.empty()) {
        return false;
    }
    int base = 10;
    std::size_t i = 0;
    if (input.size() > 2 && input[0] == '0' && (input[1] == 'x' || input[1] == 'X')) {
        base = 16;
        i = 2;
    }
    if (i >= input.size()) {
        return false;
    }
    value = 0;
    for (; i < input.size(); ++i) {
        const char c = input[i];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned>(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned>(10 + c - 'a');
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned>(10 + c - 'A');
        } else {
            return false;
        }
        value = value * static_cast<std::uint64_t>(base) + digit;
    }
    return true;
}

bool ensureSocketRuntime(std::string& error) {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            error = "WSAStartup failed";
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
            error = line.empty() ? "connection closed before response" : "connection closed mid-response";
            return {};
        }
        if (ch[0] == '\n') {
            return line;
        }
        if (ch[0] != '\r') {
            line.push_back(ch[0]);
        }
    }
    error = "response exceeded 64KB";
    return {};
}

SocketHandle connectSocket(const std::string& host, std::uint16_t port, std::string& error) {
    if (!ensureSocketRuntime(error)) {
        return kInvalidSocket;
    }
    const SocketHandle socketFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd == kInvalidSocket) {
        error = "create socket failed";
        return kInvalidSocket;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        error = "invalid IPv4 address: " + host;
        closeSocket(socketFd);
        return kInvalidSocket;
    }
    if (::connect(socketFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "connect failed: " + host + ":" + std::to_string(port);
        closeSocket(socketFd);
        return kInvalidSocket;
    }
    return socketFd;
}

bool protocolOk(const xc::HelloResponse& hello) {
    return hello.ok && hello.protocol == std::string(xc::kProtocolName) && hello.version == xc::kProtocolVersion;
}

void closeAgentSession(McpState& state) {
    if (state.agentSocket != kInvalidSocket) {
        closeSocket(state.agentSocket);
        state.agentSocket = kInvalidSocket;
    }
    state.helloOk = false;
}

bool ensureAgentSession(McpState& state, std::string& error) {
    if (state.agentSocket != kInvalidSocket && state.helloOk) {
        return true;
    }
    closeAgentSession(state);
    state.agentSocket = connectSocket(state.endpoint, xc::kDefaultAgentPort, error);
    if (state.agentSocket == kInvalidSocket) {
        return false;
    }
    const std::string helloLine = receiveLine(state.agentSocket, error);
    state.hello = xc::parseHelloResponse(helloLine);
    if (helloLine.empty() || !protocolOk(state.hello)) {
        error = helloLine.empty() ? error : "agent hello mismatch: " + helloLine;
        closeAgentSession(state);
        return false;
    }
    state.helloOk = true;
    return true;
}

std::string sendAgentRequest(McpState& state, const std::string& request, std::string& error) {
    if (!ensureAgentSession(state, error)) {
        return {};
    }
    if (!sendLine(state.agentSocket, request)) {
        error = "send request failed";
        closeAgentSession(state);
        return {};
    }
    const std::string response = receiveLine(state.agentSocket, error);
    if (response.empty()) {
        closeAgentSession(state);
    }
    return response;
}

std::string formatHitSnapshot(const xc::RecordsGetResponse& records, std::uint32_t index) {
    if (!records.ok) {
        return "records.get failed: " + records.error;
    }
    if (records.records.empty()) {
        return "no hit records for slot " + std::to_string(records.slot);
    }
    if (index >= records.records.size()) {
        index = static_cast<std::uint32_t>(records.records.size() - 1);
    }
    const auto& hit = records.records[index];
    std::ostringstream out;
    out << "slot " << records.slot << " hit #" << xc::visibleHitNumber(records, index)
        << " raw_hit_count " << hit.hitCount
        << " record_count " << records.recordCount
        << " returned " << records.returned << "\n";
    out << "PC " << xc::resolveAddressWithModules(hit.pc, records.modules) << "\n";
    out << "LR " << xc::resolveAddressWithModules(hit.lr, records.modules) << "\n";
    out << "SP " << xc::resolveAddressWithModules(hit.sp, records.modules) << "\n";
    out << "PSTATE " << xc::hexAddress(hit.pstate) << "\n";
    out << "SYSCALL " << xc::hexAddress(hit.syscallno) << "\n";
    out << "FPSR " << hit.fpsr << "\n";
    out << "FPCR " << hit.fpcr << "\n";
    for (int i = 0; i < 30; ++i) {
        out << "X" << i << " " << xc::resolveAddressWithModules(hit.x[i], records.modules) << "\n";
    }
    return out.str();
}

std::string formatHitList(const xc::RecordsGetResponse& records) {
    if (!records.ok) {
        return "records.get failed: " + records.error;
    }
    if (records.records.empty()) {
        return "no hit records for slot " + std::to_string(records.slot);
    }
    std::ostringstream out;
    out << "slot " << records.slot
        << " address " << xc::resolveAddressWithModules(records.address, records.modules)
        << " record_count " << records.recordCount
        << " returned " << records.returned << "\n";
    for (std::size_t i = 0; i < records.records.size(); ++i) {
        const auto& hit = records.records[i];
        out << "#" << xc::visibleHitNumber(records, i)
            << " PC " << xc::resolveAddressWithModules(hit.pc, records.modules)
            << " LR " << xc::resolveAddressWithModules(hit.lr, records.modules)
            << " SP " << xc::resolveAddressWithModules(hit.sp, records.modules)
            << " raw_hit_count " << hit.hitCount << "\n";
    }
    return out.str();
}

std::string initializeResponse(std::uint64_t id) {
    return jsonResult(id,
        "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},"
        "\"serverInfo\":{\"name\":\"xc-hwbp-mcp\",\"version\":\"0.1.0\"}}"
    );
}

std::string toolSchema(std::string_view name, std::string_view description, std::string_view properties, std::string_view required = "[]") {
    return "{\"name\":" + jsonString(name)
        + ",\"description\":" + jsonString(description)
        + ",\"inputSchema\":{\"type\":\"object\",\"properties\":{" + std::string(properties)
        + "},\"required\":" + std::string(required) + "}}";
}

std::string toolsListResponse(std::uint64_t id) {
    const std::string endpointProp = "\"endpoint\":{\"type\":\"string\",\"description\":\"Agent IPv4 address\"}";
    const std::string targetProp = "\"target\":{\"type\":\"string\",\"description\":\"Process name or pid\"}";
    const std::string slotProp = "\"slot\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":3}";
    const std::string recordsProps = slotProp + "," + targetProp;
    const std::string breakpointProps = slotProp + ",\"address\":{\"type\":\"string\",\"description\":\"Absolute address like 0x1234 or module+offset like lib.so+0x1234\"},\"type\":{\"type\":\"string\",\"enum\":[\"execute\",\"read\",\"write\",\"access\"]},\"size\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":8}," + targetProp;
    const std::string tools = "["
        + toolSchema("connect_agent", "Connect to the Android HWBP agent and keep a persistent session", endpointProp, "[\"endpoint\"]") + ","
        + toolSchema("driver_status", "Fetch driver/module status from the agent", "") + ","
        + toolSchema("set_breakpoint", "Set an absolute or module+offset hardware breakpoint", breakpointProps, "[\"slot\",\"address\",\"type\",\"size\"]") + ","
        + toolSchema("remove_breakpoint", "Remove a hardware breakpoint slot", slotProp + "," + targetProp, "[\"slot\"]") + ","
        + toolSchema("breakpoint_info", "Fetch raw breakpoint.info JSON from the agent", "") + ","
        + toolSchema("list_breakpoints", "Alias for breakpoint_info for AI breakpoint inspection", "") + ","
        + toolSchema("get_hit_records", "Fetch raw records.get JSON for a breakpoint slot", recordsProps, "[\"slot\"]") + ","
        + toolSchema("list_hit_records", "Fetch records.get and return an AI-readable hit list with module offsets", recordsProps, "[\"slot\"]") + ","
        + toolSchema("read_hit_snapshot", "Fetch records.get and return an AI-readable selected hit snapshot", recordsProps + ",\"index\":{\"type\":\"integer\",\"minimum\":0}", "[\"slot\"]")
        + "]";
    return jsonResult(id, "{\"tools\":" + tools + "}");
}

std::string connectAgent(McpState& state, std::uint64_t id, std::string_view args) {
    state.endpoint = stringArg(args, "endpoint", state.endpoint);
    closeAgentSession(state);
    std::string error;
    if (!ensureAgentSession(state, error)) {
        return textResult(id, "connect_agent failed: " + error);
    }
    return textResult(id, "connected to " + state.endpoint + ":" + std::to_string(xc::kDefaultAgentPort));
}

std::string rawAgentTool(McpState& state, std::uint64_t id, const std::string& request) {
    std::string error;
    const std::string response = sendAgentRequest(state, request, error);
    if (response.empty()) {
        return textResult(id, "agent request failed: " + error);
    }
    return textResult(id, response);
}

std::string setBreakpoint(McpState& state, std::uint64_t id, std::string_view args) {
    const std::uint32_t slot = uint32Arg(args, "slot", 0);
    const std::string addressText = stringArg(args, "address");
    const std::string type = stringArg(args, "type", "execute");
    const std::uint32_t size = uint32Arg(args, "size", 4);
    state.target = stringArg(args, "target", state.target);
    const std::size_t plus = addressText.find('+');
    std::string request;
    if (plus != std::string::npos) {
        const std::string module = trimAscii(addressText.substr(0, plus));
        std::uint64_t offset = 0;
        if (module.empty() || !parseUnsigned64(addressText.substr(plus + 1), offset)) {
            return textResult(id, "invalid module+offset address: " + addressText);
        }
        request = xc::breakpointSetModuleRequestJson(state.requestId++, slot, module, offset, type, size, state.target);
    } else {
        std::uint64_t address = 0;
        if (!parseUnsigned64(addressText, address) || address == 0) {
            return textResult(id, "invalid absolute address: " + addressText);
        }
        request = xc::breakpointSetRequestJson(state.requestId++, slot, address, type, size, state.target);
    }
    return rawAgentTool(state, id, request);
}

std::string callTool(McpState& state, std::uint64_t id, std::string_view request) {
    const std::string name = toolName(request);
    const std::string args = toolArguments(request);
    if (name == "connect_agent") {
        return connectAgent(state, id, args);
    }
    if (name == "driver_status") {
        return rawAgentTool(state, id, xc::driverStatusRequestJson(state.requestId++));
    }
    if (name == "set_breakpoint") {
        return setBreakpoint(state, id, args);
    }
    if (name == "remove_breakpoint") {
        const std::uint32_t slot = uint32Arg(args, "slot", 0);
        state.target = stringArg(args, "target", state.target);
        return rawAgentTool(state, id, xc::breakpointRemoveRequestJson(state.requestId++, slot, state.target));
    }
    if (name == "breakpoint_info" || name == "list_breakpoints") {
        return rawAgentTool(state, id, xc::breakpointInfoRequestJson(state.requestId++));
    }
    if (name == "get_hit_records") {
        const std::uint32_t slot = uint32Arg(args, "slot", 0);
        state.target = stringArg(args, "target", state.target);
        return rawAgentTool(state, id, xc::recordsGetRequestJson(state.requestId++, slot, state.target));
    }
    if (name == "list_hit_records") {
        const std::uint32_t slot = uint32Arg(args, "slot", 0);
        state.target = stringArg(args, "target", state.target);
        std::string error;
        const std::string response = sendAgentRequest(state, xc::recordsGetRequestJson(state.requestId++, slot, state.target), error);
        if (response.empty()) {
            return textResult(id, "records.get failed: " + error);
        }
        return textResult(id, formatHitList(xc::parseRecordsGetResponse(response)));
    }
    if (name == "read_hit_snapshot") {
        const std::uint32_t slot = uint32Arg(args, "slot", 0);
        const std::uint32_t index = uint32Arg(args, "index", 0);
        state.target = stringArg(args, "target", state.target);
        std::string error;
        const std::string response = sendAgentRequest(state, xc::recordsGetRequestJson(state.requestId++, slot, state.target), error);
        if (response.empty()) {
            return textResult(id, "records.get failed: " + error);
        }
        return textResult(id, formatHitSnapshot(xc::parseRecordsGetResponse(response), index));
    }
    return errorResult(id, -32601, "unknown tool: " + name);
}

std::string handleRequest(McpState& state, std::string_view request) {
    const std::uint64_t id = jsonRpcId(request);
    const std::string method = methodName(request);
    if (method == "initialize") {
        return initializeResponse(id);
    }
    if (method == "tools/list") {
        return toolsListResponse(id);
    }
    if (method == "tools/call") {
        return callTool(state, id, request);
    }
    if (method == "notifications/initialized") {
        return {};
    }
    return errorResult(id, -32601, "unknown method: " + method);
}

} // namespace

int main() {
    McpState state;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        const std::string response = handleRequest(state, line);
        if (!response.empty()) {
            std::cout << response << "\n";
            std::cout.flush();
        }
    }
    closeAgentSession(state);
    return 0;
}
