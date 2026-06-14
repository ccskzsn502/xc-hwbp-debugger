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
    const std::string coreCpp = readFile(root / "mcp-server" / "src" / "mcp_core.cpp");
    const std::string guiCpp = readFile(root / "pc-client" / "src" / "main.cpp");
    const std::string pcCMake = readFile(root / "pc-client" / "CMakeLists.txt");

    require(contains(rootCMake, "XC_BUILD_MCP_SERVER") && contains(rootCMake, "add_subdirectory(mcp-server)"),
            "root CMake should expose MCP server/core targets");
    require(contains(mcpCMake, "add_library(xc_mcp_core") && contains(mcpCMake, "target_link_libraries(xc_mcp_core PUBLIC xc_shared"),
            "MCP core should build as a reusable library over shared protocol helpers");
    require(contains(mcpCMake, "if(XC_BUILD_MCP_SERVER)") && contains(mcpCMake, "add_executable(xc-hwbp-mcp"),
            "standalone stdio MCP executable should be optional");
    require(contains(pcCMake, "Qt6 REQUIRED COMPONENTS Network Widgets") && contains(pcCMake, "xc_mcp_core"),
            "PC debugger should link Qt Network and embedded MCP core");
    require(contains(testsCMake, "xc-mcp-contract-tests"),
            "test suite should include MCP contract tests");

    require(contains(mainCpp, "xc::mcp::handleRequest") && contains(coreCpp, "\"jsonrpc\":\"2.0\"") && contains(coreCpp, "\"initialize\"") && contains(coreCpp, "\"tools/list\"") && contains(coreCpp, "\"tools/call\""),
            "MCP core should implement JSON-RPC initialize, tools/list and tools/call while stdio main stays thin");
    require(contains(guiCpp, "QTcpServer") && contains(guiCpp, "127.0.0.1") && contains(guiCpp, "/mcp") && contains(guiCpp, "xc::mcp::handleRequest"),
            "PC debugger should expose an embedded localhost HTTP MCP endpoint");
    require(contains(coreCpp, "connect_agent") && contains(coreCpp, "driver_status") && contains(coreCpp, "list_breakpoints"),
            "MCP tools should let AI connect and inspect driver/breakpoint state");
    require(contains(coreCpp, "set_breakpoint") && contains(coreCpp, "remove_breakpoint") && contains(coreCpp, "get_hit_records"),
            "MCP tools should let AI manage breakpoints and fetch hit records");
    require(contains(coreCpp, "list_hit_records") && contains(coreCpp, "formatHitList"),
            "MCP server should expose an AI-readable hit list with stable cumulative hit numbers");
    require(contains(coreCpp, "breakpoint_info") && contains(coreCpp, "read_hit_snapshot"),
            "MCP tools should expose raw breakpoint info and selected hit snapshots");
    require(contains(coreCpp, "breakpointSetModuleRequestJson") && contains(coreCpp, "breakpointSetRequestJson") && contains(coreCpp, "recordsGetRequestJson"),
            "MCP server should support both absolute and module+offset breakpoints plus records.get");
    require(contains(coreCpp, "SocketHandle") && contains(coreCpp, "sendAgentRequest") && contains(coreCpp, "closeAgentSession"),
            "MCP server should own a persistent Agent TCP session like the GUI");
    require(contains(coreCpp, "formatHitSnapshot") && contains(coreCpp, "hit_count") && contains(coreCpp, "PC") && contains(coreCpp, "LR") && contains(coreCpp, "SP"),
            "MCP server should return AI-readable hit snapshot text with PC/LR/SP and hit counts");
    require(contains(coreCpp, "visibleHitNumber") && contains(coreCpp, "resolveAddressWithModules") && contains(coreCpp, "record_count") && contains(coreCpp, "returned"),
            "MCP hit output should use shared cumulative hit numbering and agent map address resolution");

    std::cout << "mcp contract tests passed\n";
    return 0;
}
