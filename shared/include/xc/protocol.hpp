#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace xc {

constexpr std::uint16_t kDefaultAgentPort = 23946;
constexpr std::string_view kProtocolName = "xc-hwbp-jsonl";
constexpr std::uint32_t kProtocolVersion = 1;

enum class Command {
    Hello,
    DriverStatus,
    BreakpointInfo,
    BreakpointSet,
    BreakpointRemove,
    RecordsGet,
    RegisterWrite,
    RegisterReadOnly,
    Unknown,
};

inline std::string_view toString(Command command) {
    switch (command) {
        case Command::Hello: return "hello";
        case Command::DriverStatus: return "driver.status";
        case Command::BreakpointInfo: return "breakpoint.info";
        case Command::BreakpointSet: return "breakpoint.set";
        case Command::BreakpointRemove: return "breakpoint.remove";
        case Command::RecordsGet: return "records.get";
        case Command::RegisterWrite: return "register.write";
        case Command::RegisterReadOnly: return "register.readonly";
        case Command::Unknown: return "unknown";
    }
    return "unknown";
}

inline Command commandFromString(std::string_view value) {
    if (value == "hello") return Command::Hello;
    if (value == "driver.status") return Command::DriverStatus;
    if (value == "breakpoint.info") return Command::BreakpointInfo;
    if (value == "breakpoint.set") return Command::BreakpointSet;
    if (value == "breakpoint.remove") return Command::BreakpointRemove;
    if (value == "records.get") return Command::RecordsGet;
    if (value == "register.write") return Command::RegisterWrite;
    if (value == "register.readonly") return Command::RegisterReadOnly;
    return Command::Unknown;
}

struct HelloResponse {
    bool ok = false;
    std::string name;
    std::string protocol;
    std::uint32_t version = 0;
    std::string error;
};

struct DriverStatusResponse {
    bool ok = false;
    std::string name;
    bool moduleLoaded = false;
    bool procModulesReadable = false;
    std::string kernelRelease;
    std::string message;
    std::string error;
};

struct BreakpointSetRequest {
    std::uint64_t id = 0;
    std::uint32_t slot = 0;
    std::uint64_t address = 0;
    std::string type;
    std::uint32_t size = 0;
};

struct BreakpointSetResponse {
    bool ok = false;
    std::uint32_t slot = 0;
    std::uint64_t address = 0;
    std::string type;
    std::uint32_t size = 0;
    bool enabled = false;
    std::string message;
    std::string error;
};

inline bool containsJsonBool(std::string_view json, std::string_view key, bool expected) {
    const std::string pattern = "\"" + std::string(key) + "\":" + (expected ? "true" : "false");
    return json.find(pattern) != std::string_view::npos;
}

inline bool jsonBoolOrFalse(std::string_view json, std::string_view key) {
    return containsJsonBool(json, key, true);
}

inline std::string jsonStringValue(std::string_view json, std::string_view key) {
    const std::string prefix = "\"" + std::string(key) + "\":\"";
    const std::size_t start = json.find(prefix);
    if (start == std::string_view::npos) {
        return {};
    }
    std::string out;
    std::size_t i = start + prefix.size();
    while (i < json.size()) {
        const char c = json[i++];
        if (c == '\\' && i < json.size()) {
            const char esc = json[i++];
            switch (esc) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                default: out.push_back(esc); break;
            }
            continue;
        }
        if (c == '"') {
            break;
        }
        out.push_back(c);
    }
    return out;
}

inline std::uint32_t jsonUint32Value(std::string_view json, std::string_view key) {
    const std::string prefix = "\"" + std::string(key) + "\":";
    const std::size_t start = json.find(prefix);
    if (start == std::string_view::npos) {
        return 0;
    }
    std::uint32_t value = 0;
    std::size_t i = start + prefix.size();
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
        value = value * 10U + static_cast<std::uint32_t>(json[i] - '0');
        ++i;
    }
    return value;
}

inline std::uint64_t jsonUint64Value(std::string_view json, std::string_view key) {
    const std::string quotedPrefix = "\"" + std::string(key) + "\":\"";
    std::size_t start = json.find(quotedPrefix);
    if (start != std::string_view::npos) {
        start += quotedPrefix.size();
        if (start + 2 <= json.size() && json[start] == '0' && (json[start + 1] == 'x' || json[start + 1] == 'X')) {
            start += 2;
        }
        std::uint64_t value = 0;
        while (start < json.size()) {
            const char c = json[start++];
            if (c >= '0' && c <= '9') { value = value * 16ULL + static_cast<std::uint64_t>(c - '0'); continue; }
            if (c >= 'a' && c <= 'f') { value = value * 16ULL + static_cast<std::uint64_t>(10 + c - 'a'); continue; }
            if (c >= 'A' && c <= 'F') { value = value * 16ULL + static_cast<std::uint64_t>(10 + c - 'A'); continue; }
            break;
        }
        return value;
    }

    const std::string prefix = "\"" + std::string(key) + "\":";
    start = json.find(prefix);
    if (start == std::string_view::npos) {
        return 0;
    }
    std::uint64_t value = 0;
    std::size_t i = start + prefix.size();
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
        value = value * 10ULL + static_cast<std::uint64_t>(json[i] - '0');
        ++i;
    }
    return value;
}

inline std::string hexAddress(std::uint64_t address) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << address;
    return out.str();
}

inline HelloResponse parseHelloResponse(std::string_view json) {
    HelloResponse response;
    response.ok = jsonBoolOrFalse(json, "ok");
    response.name = jsonStringValue(json, "name");
    response.protocol = jsonStringValue(json, "protocol");
    response.version = jsonUint32Value(json, "version");
    response.error = jsonStringValue(json, "error");
    return response;
}

inline DriverStatusResponse parseDriverStatusResponse(std::string_view json) {
    DriverStatusResponse response;
    response.ok = jsonBoolOrFalse(json, "ok");
    response.name = jsonStringValue(json, "name");
    response.moduleLoaded = jsonBoolOrFalse(json, "module_loaded");
    response.procModulesReadable = jsonBoolOrFalse(json, "proc_modules_readable");
    response.kernelRelease = jsonStringValue(json, "kernel_release");
    response.message = jsonStringValue(json, "message");
    response.error = jsonStringValue(json, "error");
    return response;
}

inline BreakpointSetRequest parseBreakpointSetRequest(std::string_view json) {
    BreakpointSetRequest request;
    request.id = jsonUint64Value(json, "id");
    request.slot = jsonUint32Value(json, "slot");
    request.address = jsonUint64Value(json, "address");
    request.type = jsonStringValue(json, "type");
    request.size = jsonUint32Value(json, "size");
    return request;
}

inline BreakpointSetResponse parseBreakpointSetResponse(std::string_view json) {
    BreakpointSetResponse response;
    response.ok = jsonBoolOrFalse(json, "ok");
    response.slot = jsonUint32Value(json, "slot");
    response.address = jsonUint64Value(json, "address");
    response.type = jsonStringValue(json, "type");
    response.size = jsonUint32Value(json, "size");
    response.enabled = jsonBoolOrFalse(json, "enabled");
    response.message = jsonStringValue(json, "message");
    response.error = jsonStringValue(json, "error");
    return response;
}

inline std::string helloRequestJson(std::uint64_t id) {
    return "{\"id\":" + std::to_string(id) + ",\"cmd\":\"hello\"}";
}

inline std::string driverStatusRequestJson(std::uint64_t id) {
    return "{\"id\":" + std::to_string(id) + ",\"cmd\":\"driver.status\"}";
}

inline std::string breakpointSetRequestJson(std::uint64_t id, std::uint32_t slot, std::uint64_t address, std::string_view type, std::uint32_t size) {
    return "{\"id\":" + std::to_string(id)
        + ",\"cmd\":\"breakpoint.set\",\"slot\":" + std::to_string(slot)
        + ",\"address\":\"" + hexAddress(address)
        + "\",\"type\":\"" + std::string(type)
        + "\",\"size\":" + std::to_string(size) + "}";
}

inline std::string helloResponseJson(std::uint64_t id) {
    return "{\"id\":" + std::to_string(id)
        + ",\"ok\":true,\"name\":\"xc-hwbp-agent\",\"protocol\":\""
        + std::string(kProtocolName)
        + "\",\"version\":" + std::to_string(kProtocolVersion) + "}";
}

} // namespace xc
