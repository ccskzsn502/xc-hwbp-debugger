#pragma once

#include <cstdint>
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

inline std::string helloResponseJson(std::uint64_t id) {
    return "{\"id\":" + std::to_string(id)
        + ",\"ok\":true,\"name\":\"xc-hwbp-agent\",\"protocol\":\""
        + std::string(kProtocolName)
        + "\",\"version\":" + std::to_string(kProtocolVersion) + "}";
}

} // namespace xc
