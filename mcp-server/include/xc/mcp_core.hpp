#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace xc::mcp {

struct McpState;

McpState* createState();
void destroyState(McpState* state);
void setDefaults(McpState& state, std::string_view endpoint, std::string_view target);
void closeAgentSession(McpState& state);
std::string handleRequest(McpState& state, std::string_view request);
std::string serverName();
std::uint16_t defaultHttpPort();

} // namespace xc::mcp
