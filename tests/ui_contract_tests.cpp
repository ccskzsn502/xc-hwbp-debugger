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
    const std::string editorconfig = readFile(root / ".editorconfig");
    const std::string readme = readFile(root / "README.md");
    const std::string workflow = readFile(root / ".github" / "workflows" / "build.yml");

    require(!contains(mainCpp, "std::vector<std::string> registerLines()"),
            "GUI must not ship with hard-coded mock register rows");
    require(!contains(mainCpp, "00000078919CFF84") && !contains(mainCpp, "DEC:88"),
            "initial GUI must not render fake register/address data before an agent hit");
    require(contains(mainCpp, "等待真实断点命中数据"),
            "empty data view should tell the user it is waiting for real breakpoint data");
    require(contains(mainCpp, "#include <QApplication>") && contains(mainCpp, "QMainWindow"),
            "PC GUI must be implemented with Qt Widgets");
    require(contains(editorconfig, "charset = utf-8"),
            "repository should pin source files to UTF-8 so Chinese UI text is stable across editors");
    require(!contains(mainCpp, "eui_neo.h") && !contains(mainCpp, "eui::"),
            "PC GUI must not use EUI-NEO rendering after the Qt rewrite");
    require(contains(mainCpp, "Microsoft YaHei UI") || contains(mainCpp, "Microsoft YaHei"),
            "Qt GUI should explicitly use a clear Chinese Windows UI font");
    require(contains(mainCpp, "QSplitter") && contains(mainCpp, "QTableWidget") && contains(mainCpp, "QPlainTextEdit"),
            "Qt GUI should use a fixed debugger layout with splitter, tables and plain text log/stack views");
    require(contains(mainCpp, "buildConnectionBar") && contains(mainCpp, "buildBreakpointEditor"),
            "top controls should be split into connection and breakpoint editor bars instead of one crowded toolbar");
    require(contains(mainCpp, "statusPill") && contains(mainCpp, "QLabel[role=\"statusPill\"]"),
            "session state should render as compact status pills with explicit role styling");
    require(contains(mainCpp, "primaryButton") && contains(mainCpp, "dangerButton") && contains(mainCpp, "secondaryButton"),
            "debugger actions should have primary, secondary and destructive visual hierarchy");
    require(contains(mainCpp, "QFrame#connectionBar") && contains(mainCpp, "QFrame#breakpointEditor") && contains(mainCpp, "border-radius: 6px"),
            "Qt stylesheet should define distinct professional dark debugger panels");
    require(contains(mainCpp, "font-size: 12px") && contains(mainCpp, "setDefaultSectionSize(30)"),
            "debugger UI should use compact fonts and table rows for dense hit inspection");
    require(contains(mainCpp, "breakpointsTable_") && !contains(mainCpp, "watchTable_") && !contains(mainCpp, "slotsTable_"),
            "watch breakpoints and breakpoint slots should be one linked breakpoint list, not two unrelated panes");
    require(contains(mainCpp, "hitRecordsTable_") && contains(mainCpp, "refreshHitRecordsTable") && contains(mainCpp, "selectedHitIndex_"),
            "PC GUI should show a selectable hit list under the breakpoint list instead of only the latest hit");
    require(contains(mainCpp, "itemSelectionChanged") && contains(mainCpp, "currentHitRecord"),
            "selecting a hit row should switch the rendered register snapshot");
    require(contains(mainCpp, "formatHitSnapshot") && contains(mainCpp, "X29") && !contains(mainCpp, "formatLatestHitStack"),
            "registers and PC/LR/SP should render as one hit snapshot with one register per line, not split panes");
    require(contains(mainCpp, "resolveAddressLabel") && contains(mainCpp, "AddressResolverContext"),
            "address rendering should go through a resolver context that can later consume agent maps");
    require(contains(mainCpp, "命中详情") && contains(mainCpp, "hitSnapshotText_") && !contains(mainCpp, "stackText_"),
            "registers and stack should render in one combined hit snapshot view");
    require(contains(mainCpp, "parseBreakpointAddress") && contains(mainCpp, "libtersafe.so+0x488F08"),
            "breakpoint input should support module.so+offset syntax");
    require(contains(mainCpp, "typeCombo_->addItems({\"x\", \"r\", \"w\", \"rw\"})") && contains(mainCpp, "protocolTypeForUi"),
            "breakpoint type selector should use compact x/r/w/rw labels and map them to the wire protocol");
    require(contains(mainCpp, "SocketHandle agentSocket_") && contains(mainCpp, "closeAgentSession"),
            "GUI connection button should own a persistent Agent socket that disconnect closes explicitly");
    require(contains(mainCpp, "sendAgentRequest") && contains(mainCpp, "ensureAgentSession"),
            "GUI commands should reuse one Agent session through a shared request helper");
    require(contains(mainCpp, "records.get") && contains(mainCpp, "parseRecordsGetResponse"),
            "PC GUI must request and parse records.get so breakpoint hits appear instead of only raw breakpoint.info JSON");
    require(contains(mainCpp, "refreshHitDetails") && contains(mainCpp, "hit #") && contains(mainCpp, "PC") && contains(mainCpp, "LR") && contains(mainCpp, "SP") && contains(mainCpp, "X29"),
            "PC GUI must render the selected hit snapshot into the hit detail pane");
    require(contains(mainCpp, "#include <QTimer>") && contains(mainCpp, "hitPollTimer_") && contains(mainCpp, "pollHitRecords"),
            "PC GUI must poll records.get because the JSONL protocol has no async hit push from the phone");
    require(contains(mainCpp, "queryHitRecords") && contains(methodBody(mainCpp, "void queryBreakpointInfo()"), "queryHitRecords"),
            "manual breakpoint refresh must also fetch hit records for the selected slot");
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
