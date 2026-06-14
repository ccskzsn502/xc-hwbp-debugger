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
    require(argc == 2, "ui contract test requires repository root argument");
    const std::filesystem::path root = argv[1];
    const std::string mainCpp = readFile(root / "pc-client" / "src" / "main.cpp");
    const std::string pcCMake = readFile(root / "pc-client" / "CMakeLists.txt");
    const std::string editorconfig = readFile(root / ".editorconfig");
    const std::string readme = readFile(root / "README.md");
    const std::string workflow = readFile(root / ".github" / "workflows" / "build.yml");

    require(contains(mainCpp, "#include <QApplication>") && contains(mainCpp, "QMainWindow"),
            "PC GUI must be implemented with Qt Widgets");
    require(contains(editorconfig, "charset = utf-8"),
            "repository should pin source files to UTF-8 so Chinese UI text is stable across editors");
    require(!contains(mainCpp, "eui_neo.h") && !contains(mainCpp, "eui::"),
            "PC GUI must not use EUI-NEO rendering after the Qt rewrite");
    require(contains(mainCpp, "Microsoft YaHei UI") || contains(mainCpp, "Microsoft YaHei"),
            "Qt GUI should explicitly use a clear Chinese Windows UI font");

    require(contains(mainCpp, "class DebuggerShellWindow") && contains(mainCpp, "QDockWidget") && contains(mainCpp, "QTabWidget"),
            "temporary GUI should be a dockable debugger workbench shell");
    require(contains(mainCpp, "buildChrome") && contains(mainCpp, "buildLeftDock") && contains(mainCpp, "buildCenter")
                && contains(mainCpp, "buildRightDock") && contains(mainCpp, "buildBottomDock"),
            "GUI shell should separate header, left controls, center activity, right inspector and bottom output");
    require(contains(mainCpp, "QFrame#headerBar") && contains(mainCpp, "QFrame#controlRail") && contains(mainCpp, "QFrame#activityPane")
                && contains(mainCpp, "QFrame#inspectorPane"),
            "GUI shell should use named flat panes for styling");
    require(contains(mainCpp, "border-radius: 2px") && !contains(mainCpp, "border-radius: 6px"),
            "GUI shell should keep low-radius controls instead of rounded card styling");

    require(contains(mainCpp, "XC HWBP Debugger") && contains(mainCpp, "断点与命中") && contains(mainCpp, "寄存器快照"),
            "GUI shell labels must be readable Chinese, not mojibake");
    require(contains(mainCpp, "会话") && contains(mainCpp, "检查器") && contains(mainCpp, "输出"),
            "GUI shell should expose the main debugger workbench regions");
    require(contains(mainCpp, "libtersafe.so+0x488F08") && contains(mainCpp, "module.so+offset 或 0xaddr"),
            "breakpoint editor preview should show module+offset syntax");
    require(contains(mainCpp, "等待真实断点命中数据") && contains(mainCpp, "按钮暂为界面预览"),
            "temporary GUI must make clear that live debugger logic is not connected yet");
    require(contains(mainCpp, "Agent 未连接") && contains(mainCpp, "Driver 未检测") && contains(mainCpp, "MCP 预览"),
            "header should show preview status badges for the future runtime state");

    require(contains(mainCpp, "breakpointsTable_") && contains(mainCpp, "hitsTable_") && contains(mainCpp, "slotsTable_"),
            "GUI shell should include breakpoint, hit stream and slot overview tables");
    require(contains(mainCpp, "registerText_") && contains(mainCpp, "modulesText_") && contains(mainCpp, "rawText_") && contains(mainCpp, "logText_"),
            "GUI shell should include inspector tabs and bottom log output");
    require(contains(mainCpp, "primaryButton") && contains(mainCpp, "secondaryButton") && contains(mainCpp, "dangerButton"),
            "GUI shell should keep a basic action hierarchy for later wiring");

    require(!contains(mainCpp, "sendAgentRequest") && !contains(mainCpp, "ensureAgentSession") && !contains(mainCpp, "pollHitRecords")
                && !contains(mainCpp, "setBreakpoint()") && !contains(mainCpp, "removeBreakpoint()"),
            "temporary GUI shell should not include Agent or breakpoint protocol logic yet");
    require(contains(mainCpp, "QTcpServer") && contains(mainCpp, "xc::mcp::serverName") && contains(mainCpp, "127.0.0.1:23947/mcp"),
            "temporary GUI shell should keep MCP endpoint preview hooks without serving requests yet");

    require(contains(pcCMake, "find_package(Qt6") && contains(pcCMake, "Qt6::Widgets") && contains(pcCMake, "Qt6::Network"),
            "PC client CMake must build against Qt6 Widgets and keep Network available for later MCP wiring");
    require(contains(pcCMake, "xc_mcp_core"),
            "PC client should keep linking the MCP core while the shell UI is being reviewed");
    require(!contains(pcCMake, "EUI-NEO") && !contains(pcCMake, "eui_neo"),
            "PC client CMake must stop fetching EUI-NEO");
    require(contains(readme, "Qt Widgets") && contains(readme, "UTF-8") && !contains(readme, "EUI-NEO"),
            "README should describe the current Qt Widgets client and UTF-8 source encoding policy");

    require(contains(workflow, "windows-latest"), "GitHub Actions must build the Windows GUI in the cloud");
    require(contains(workflow, "xc-hwbp-debugger.exe"), "GitHub Actions must verify the packaged exe exists");
    require(contains(workflow, "actions/upload-artifact@v4"), "GitHub Actions must upload the Windows exe artifact");
    require(contains(workflow, "jurplel/install-qt-action") && contains(workflow, "windeployqt"),
            "GitHub Actions must install Qt and deploy Qt runtime DLLs for the Windows artifact");

    std::cout << "ui contract tests passed\n";
    return 0;
}