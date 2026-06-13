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

std::size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

std::string methodBody(const std::string& source, const std::string& signature) {
    const std::size_t signaturePos = source.find(signature);
    const std::string missing = "missing method " + signature;
    require(signaturePos != std::string::npos, missing.c_str());
    const std::size_t openBrace = source.find('{', signaturePos);
    const std::string missingBody = "missing method body for " + signature;
    require(openBrace != std::string::npos, missingBody.c_str());
    int depth = 0;
    for (std::size_t i = openBrace; i < source.size(); ++i) {
        if (source[i] == '{') {
            ++depth;
        } else if (source[i] == '}') {
            --depth;
            if (depth == 0) {
                return source.substr(openBrace, i - openBrace + 1);
            }
        }
    }
    const std::string unterminated = "unterminated method body for " + signature;
    require(false, unterminated.c_str());
    return {};
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "ui contract test requires repository root argument");
    const std::filesystem::path root = argv[1];
    const std::string mainCpp = readFile(root / "pc-client" / "src" / "main.cpp");
    const std::string pcCMake = readFile(root / "pc-client" / "CMakeLists.txt");
    const std::string workflow = readFile(root / ".github" / "workflows" / "build.yml");

    require(!contains(mainCpp, "std::vector<std::string> registerLines()"),
            "GUI must not ship with hard-coded mock register rows");
    require(!contains(mainCpp, "00000078919CFF84") && !contains(mainCpp, "DEC:88"),
            "initial GUI must not render fake register/address data before an agent hit");
    require(contains(mainCpp, "等待真实断点命中数据"),
            "empty data view should tell the user it is waiting for real breakpoint data");
    require(contains(mainCpp, "#include <QApplication>") && contains(mainCpp, "QMainWindow"),
            "PC GUI must be implemented with Qt Widgets");
    require(!contains(mainCpp, "eui_neo.h") && !contains(mainCpp, "eui::"),
            "PC GUI must not use EUI-NEO rendering after the Qt rewrite");
    require(contains(mainCpp, "Microsoft YaHei UI") || contains(mainCpp, "Microsoft YaHei"),
            "Qt GUI should explicitly use a clear Chinese Windows UI font");
    require(contains(mainCpp, "QSplitter") && contains(mainCpp, "QTableWidget") && contains(mainCpp, "QPlainTextEdit"),
            "Qt GUI should use a fixed debugger layout with splitter, tables and plain text log/stack views");
    require(contains(mainCpp, "breakpointsTable_") && !contains(mainCpp, "watchTable_") && !contains(mainCpp, "slotsTable_"),
            "watch breakpoints and breakpoint slots should be one linked breakpoint list, not two unrelated panes");
    require(contains(mainCpp, "命中详情") && contains(mainCpp, "registersText_") && contains(mainCpp, "stackText_"),
            "registers and stack should live together in one hit detail area instead of many grid panels");
    require(contains(mainCpp, "parseBreakpointAddress") && contains(mainCpp, "libtersafe.so+0x488F08"),
            "breakpoint input should support module.so+offset syntax");
    require(contains(mainCpp, "typeCombo_->addItems({\"x\", \"r\", \"w\", \"rw\"})") && contains(mainCpp, "protocolTypeForUi"),
            "breakpoint type selector should use compact x/r/w/rw labels and map them to the wire protocol");
    require(contains(mainCpp, "SocketHandle agentSocket_") && contains(mainCpp, "closeAgentSession"),
            "GUI connection button should own a persistent Agent socket that disconnect closes explicitly");
    require(contains(mainCpp, "sendAgentRequest") && contains(mainCpp, "ensureAgentSession"),
            "GUI commands should reuse one Agent session through a shared request helper");
    require(!contains(methodBody(mainCpp, "void setBreakpoint()"), "openAgentSession")
                && !contains(methodBody(mainCpp, "void removeBreakpoint()"), "openAgentSession")
                && !contains(methodBody(mainCpp, "void queryBreakpointInfo()"), "openAgentSession"),
            "breakpoint commands must not reconnect for every request");
    require(countOccurrences(methodBody(mainCpp, "void markConnected()"), "closeSocket") == 0,
            "successful connection probing must keep the Agent socket open after driver.status");
    require(contains(mainCpp, "监视断点") && contains(mainCpp, "硬件断点槽") && contains(mainCpp, "驱动状态"),
            "Qt GUI labels must stay Chinese");

    require(contains(pcCMake, "find_package(Qt6") && contains(pcCMake, "Qt6::Widgets"),
            "PC client CMake must build against Qt6 Widgets");
    require(!contains(pcCMake, "EUI-NEO") && !contains(pcCMake, "eui_neo"),
            "PC client CMake must stop fetching EUI-NEO");

    require(contains(workflow, "windows-latest"), "GitHub Actions must build the Windows GUI in the cloud");
    require(contains(workflow, "xc-hwbp-debugger.exe"), "GitHub Actions must verify the packaged exe exists");
    require(contains(workflow, "actions/upload-artifact@v4"), "GitHub Actions must upload the Windows exe artifact");
    require(contains(workflow, "jurplel/install-qt-action") && contains(workflow, "windeployqt"),
            "GitHub Actions must install Qt and deploy Qt runtime DLLs for the Windows artifact");

    std::cout << "ui contract tests passed\n";
    return 0;
}
