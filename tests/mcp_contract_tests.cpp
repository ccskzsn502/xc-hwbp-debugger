#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "FAIL: missing file " << path.string() << "\n";
        std::exit(1);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "mcp contract test requires repository root argument");
    const std::filesystem::path root = argv[1];
    const std::string rootCMake = readFile(root / "CMakeLists.txt");
    const std::string testsCMake = readFile(root / "tests" / "CMakeLists.txt");
    const std::string mcpCMake = readFile(root / "mcp-server" / "CMakeLists.txt");
    const std::string mainCpp = readFile(root / "mcp-server" / "src" / "main.cpp");

    require(contains(rootCMake, "XC_BUILD_MCP_SERVER") && contains(rootCMake, "add_subdirectory(mcp-server)"),
            "root CMake should expose an optional MCP server build target");
    require(contains(mcpCMake, "add_executable(xc-hwbp-mcp") && contains(mcpCMake, "target_link_libraries(xc-hwbp-mcp PRIVATE xc_shared"),
            "MCP server should build as standalone xc-hwbp-mcp and reuse shared protocol helpers");
    require(contains(testsCMake, "xc-mcp-contract-tests"),
            "test suite should include MCP contract tests");

    require(contains(mainCpp, "\"jsonrpc\":\"2.0\"") && contains(mainCpp, "\"initialize\"") && contains(mainCpp, "\"tools/list\"") && contains(mainCpp, "\"tools/call\""),
            "MCP server should implement stdio JSON-RPC initialize, tools/list and tools/call");
    require(contains(mainCpp, "connect_agent") && contains(mainCpp, "driver_status") && contains(mainCpp, "list_breakpoints"),
            "MCP tools should let AI connect and inspect driver/breakpoint state");
    require(contains(mainCpp, "set_breakpoint") && contains(mainCpp, "remove_breakpoint") && contains(mainCpp, "get_hit_records"),
            "MCP tools should let AI manage breakpoints and fetch hit records");
    require(contains(mainCpp, "list_hit_records") && contains(mainCpp, "formatHitList"),
            "MCP server should expose an AI-readable hit list with stable cumulative hit numbers");
    require(contains(mainCpp, "breakpoint_info") && contains(mainCpp, "read_hit_snapshot"),
            "MCP tools should expose raw breakpoint info and selected hit snapshots");
    require(contains(mainCpp, "breakpointSetModuleRequestJson") && contains(mainCpp, "breakpointSetRequestJson") && contains(mainCpp, "recordsGetRequestJson"),
            "MCP server should support both absolute and module+offset breakpoints plus records.get");
    require(contains(mainCpp, "SocketHandle") && contains(mainCpp, "sendAgentRequest") && contains(mainCpp, "closeAgentSession"),
            "MCP server should own a persistent Agent TCP session like the GUI");
    require(contains(mainCpp, "formatHitSnapshot") && contains(mainCpp, "hit_count") && contains(mainCpp, "PC") && contains(mainCpp, "LR") && contains(mainCpp, "SP"),
            "MCP server should return AI-readable hit snapshot text with PC/LR/SP and hit counts");
    require(contains(mainCpp, "visibleHitNumber") && contains(mainCpp, "resolveAddressWithModules") && contains(mainCpp, "record_count") && contains(mainCpp, "returned"),
            "MCP hit output should use shared cumulative hit numbering and agent map address resolution");

    std::cout << "mcp contract tests passed\n";
    return 0;
}
