#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    std::string module;
    std::uint64_t offset = 0;
    std::string type;
    std::uint32_t size = 0;
    std::string target;
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

struct BreakpointRemoveRequest {
    std::uint64_t id = 0;
    std::uint32_t slot = 0;
    std::string target;
};

struct BreakpointRemoveResponse {
    bool ok = false;
    std::uint32_t slot = 0;
    bool enabled = false;
    std::string message;
    std::string error;
};

struct HitRecord {
    std::uint64_t hitCount = 0;
    std::uint64_t pc = 0;
    std::uint64_t lr = 0;
    std::uint64_t sp = 0;
    std::uint64_t origX0 = 0;
    std::uint64_t syscallno = 0;
    std::uint64_t pstate = 0;
    std::uint32_t fpsr = 0;
    std::uint32_t fpcr = 0;
    std::uint64_t x[30]{};
};

struct ModuleMapEntry {
    std::string module;
    std::string path;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    std::uint64_t loadBase = 0;
};

struct RecordsGetResponse {
    bool ok = false;
    std::uint32_t slot = 0;
    std::uint64_t address = 0;
    std::uint32_t type = 0;
    std::uint32_t size = 0;
    std::uint32_t recordCount = 0;
    std::uint32_t returned = 0;
    std::vector<HitRecord> records;
    std::vector<ModuleMapEntry> modules;
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

inline std::string jsonArrayValue(std::string_view json, std::string_view key) {
    const std::string prefix = "\"" + std::string(key) + "\":[";
    const std::size_t start = json.find(prefix);
    if (start == std::string_view::npos) {
        return {};
    }
    std::size_t pos = start + prefix.size() - 1;
    int arrayDepth = 0;
    int objectDepth = 0;
    for (std::size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '[') {
            ++arrayDepth;
        } else if (json[i] == ']') {
            --arrayDepth;
            if (arrayDepth == 0 && objectDepth == 0) {
                return std::string(json.substr(pos, i - pos + 1));
            }
        } else if (json[i] == '{') {
            ++objectDepth;
        } else if (json[i] == '}') {
            --objectDepth;
        }
    }
    return {};
}

inline std::vector<std::string> jsonObjectArrayItems(std::string_view arrayJson) {
    std::vector<std::string> items;
    std::size_t pos = 0;
    while (pos < arrayJson.size()) {
        const std::size_t objectStart = arrayJson.find('{', pos);
        if (objectStart == std::string_view::npos) {
            break;
        }
        int depth = 0;
        std::size_t objectEnd = objectStart;
        for (; objectEnd < arrayJson.size(); ++objectEnd) {
            if (arrayJson[objectEnd] == '{') {
                ++depth;
            } else if (arrayJson[objectEnd] == '}') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (objectEnd >= arrayJson.size()) {
            break;
        }
        items.emplace_back(arrayJson.substr(objectStart, objectEnd - objectStart + 1));
        pos = objectEnd + 1;
    }
    return items;
}

inline std::string hexAddress(std::uint64_t address) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << address;
    return out.str();
}

inline std::string escapeJson(std::string_view value) {
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
    request.module = jsonStringValue(json, "module");
    request.offset = jsonUint64Value(json, "offset");
    request.type = jsonStringValue(json, "type");
    request.size = jsonUint32Value(json, "size");
    request.target = jsonStringValue(json, "target");
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

inline BreakpointRemoveRequest parseBreakpointRemoveRequest(std::string_view json) {
    BreakpointRemoveRequest request;
    request.id = jsonUint64Value(json, "id");
    request.slot = jsonUint32Value(json, "slot");
    request.target = jsonStringValue(json, "target");
    return request;
}

inline BreakpointRemoveResponse parseBreakpointRemoveResponse(std::string_view json) {
    BreakpointRemoveResponse response;
    response.ok = jsonBoolOrFalse(json, "ok");
    response.slot = jsonUint32Value(json, "slot");
    response.enabled = jsonBoolOrFalse(json, "enabled");
    response.message = jsonStringValue(json, "message");
    response.error = jsonStringValue(json, "error");
    return response;
}

inline RecordsGetResponse parseRecordsGetResponse(std::string_view json) {
    RecordsGetResponse response;
    response.ok = jsonBoolOrFalse(json, "ok");
    response.slot = jsonUint32Value(json, "slot");
    response.address = jsonUint64Value(json, "address");
    response.type = jsonUint32Value(json, "type");
    response.size = jsonUint32Value(json, "size");
    response.recordCount = jsonUint32Value(json, "record_count");
    response.returned = jsonUint32Value(json, "returned");
    response.error = jsonStringValue(json, "error");
    if (!response.ok) {
        return response;
    }

    for (const std::string& recordObject : jsonObjectArrayItems(jsonArrayValue(json, "records"))) {
        const std::string_view recordJson = recordObject;
        HitRecord record;
        record.hitCount = jsonUint64Value(recordJson, "hit_count");
        record.pc = jsonUint64Value(recordJson, "pc");
        record.lr = jsonUint64Value(recordJson, "lr");
        record.sp = jsonUint64Value(recordJson, "sp");
        record.origX0 = jsonUint64Value(recordJson, "orig_x0");
        record.syscallno = jsonUint64Value(recordJson, "syscallno");
        record.pstate = jsonUint64Value(recordJson, "pstate");
        record.fpsr = jsonUint32Value(recordJson, "fpsr");
        record.fpcr = jsonUint32Value(recordJson, "fpcr");
        for (std::uint32_t i = 0; i < 30; ++i) {
            record.x[i] = jsonUint64Value(recordJson, "x" + std::to_string(i));
        }
        response.records.push_back(record);
    }
    for (const std::string& moduleObject : jsonObjectArrayItems(jsonArrayValue(json, "modules"))) {
        const std::string_view moduleJson = moduleObject;
        ModuleMapEntry module;
        module.module = jsonStringValue(moduleJson, "module");
        module.path = jsonStringValue(moduleJson, "path");
        module.start = jsonUint64Value(moduleJson, "start");
        module.end = jsonUint64Value(moduleJson, "end");
        module.loadBase = jsonUint64Value(moduleJson, "load_base");
        if (module.end > module.start) {
            response.modules.push_back(module);
        }
    }
    return response;
}

inline std::uint32_t visibleHitNumber(const RecordsGetResponse& records, std::size_t visibleIndex) {
    if (records.records.empty()) {
        return 0;
    }
    const std::uint32_t returned = records.returned != 0 ? records.returned : static_cast<std::uint32_t>(records.records.size());
    const std::uint32_t recordCount = records.recordCount != 0 ? records.recordCount : returned;
    const std::uint32_t first = recordCount > returned ? recordCount - returned + 1 : 1;
    return first + static_cast<std::uint32_t>(visibleIndex);
}

inline std::string resolveAddressWithModules(std::uint64_t address, const std::vector<ModuleMapEntry>& modules) {
    if (address == 0) {
        return "-";
    }
    const std::string absolute = hexAddress(address);
    for (const auto& module : modules) {
        if (address < module.start || address >= module.end) {
            continue;
        }
        const std::uint64_t base = module.loadBase != 0 ? module.loadBase : module.start;
        const std::string name = !module.module.empty() ? module.module : module.path;
        if (!name.empty() && address >= base) {
            return absolute + "  " + name + "+" + hexAddress(address - base);
        }
    }
    return absolute;
}

inline std::string helloRequestJson(std::uint64_t id) {
    return "{\"id\":" + std::to_string(id) + ",\"cmd\":\"hello\"}";
}

inline std::string driverStatusRequestJson(std::uint64_t id) {
    return "{\"id\":" + std::to_string(id) + ",\"cmd\":\"driver.status\"}";
}

inline std::string breakpointInfoRequestJson(std::uint64_t id) {
    return "{\"id\":" + std::to_string(id) + ",\"cmd\":\"breakpoint.info\"}";
}

inline std::string recordsGetRequestJson(std::uint64_t id, std::uint32_t slot, std::string_view target);

inline std::string recordsGetRequestJson(std::uint64_t id, std::uint32_t slot) {
    return recordsGetRequestJson(id, slot, {});
}

inline std::string recordsGetRequestJson(std::uint64_t id, std::uint32_t slot, std::string_view target) {
    std::string out = "{\"id\":" + std::to_string(id)
        + ",\"cmd\":\"records.get\",\"slot\":" + std::to_string(slot);
    if (!target.empty()) {
        out += ",\"target\":\"" + escapeJson(target) + "\"";
    }
    out += "}";
    return out;
}

inline std::string breakpointRemoveRequestJson(std::uint64_t id, std::uint32_t slot, std::string_view target = {}) {
    std::string out = "{\"id\":" + std::to_string(id)
        + ",\"cmd\":\"breakpoint.remove\",\"slot\":" + std::to_string(slot);
    if (!target.empty()) {
        out += ",\"target\":\"" + std::string(target) + "\"";
    }
    out += "}";
    return out;
}

inline std::string breakpointSetRequestJson(std::uint64_t id, std::uint32_t slot, std::uint64_t address, std::string_view type, std::uint32_t size, std::string_view target = {}) {
    std::string out = "{\"id\":" + std::to_string(id)
        + ",\"cmd\":\"breakpoint.set\",\"slot\":" + std::to_string(slot)
        + ",\"address\":\"" + hexAddress(address)
        + "\",\"type\":\"" + escapeJson(type)
        + "\",\"size\":" + std::to_string(size);
    if (!target.empty()) {
        out += ",\"target\":\"" + escapeJson(target) + "\"";
    }
    out += "}";
    return out;
}

inline std::string breakpointSetModuleRequestJson(std::uint64_t id, std::uint32_t slot, std::string_view module, std::uint64_t offset, std::string_view type, std::uint32_t size, std::string_view target = {}) {
    std::string out = "{\"id\":" + std::to_string(id)
        + ",\"cmd\":\"breakpoint.set\",\"slot\":" + std::to_string(slot)
        + ",\"module\":\"" + escapeJson(module)
        + "\",\"offset\":\"" + hexAddress(offset)
        + "\",\"type\":\"" + escapeJson(type)
        + "\",\"size\":" + std::to_string(size);
    if (!target.empty()) {
        out += ",\"target\":\"" + escapeJson(target) + "\"";
    }
    out += "}";
    return out;
}

inline std::string helloResponseJson(std::uint64_t id) {
    return "{\"id\":" + std::to_string(id)
        + ",\"ok\":true,\"name\":\"xc-hwbp-agent\",\"protocol\":\""
        + std::string(kProtocolName)
        + "\",\"version\":" + std::to_string(kProtocolVersion) + "}";
}

} // namespace xc
