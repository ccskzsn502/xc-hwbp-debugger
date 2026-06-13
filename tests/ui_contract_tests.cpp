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
    const std::string workflow = readFile(root / ".github" / "workflows" / "build.yml");
    const std::string euiPatch = readFile(root / "cmake" / "patch_eui_neo.cmake");

    require(!contains(mainCpp, "std::vector<std::string> registerLines()"),
            "GUI must not ship with hard-coded mock register rows");
    require(!contains(mainCpp, "00000078919CFF84") && !contains(mainCpp, "DEC:88"),
            "initial GUI must not render fake register/address data before an agent hit");
    require(contains(mainCpp, "等待真实断点命中数据"),
            "empty data view should tell the user it is waiting for real breakpoint data");
    require(contains(mainCpp, "Cascadia Mono") || contains(mainCpp, "monospace"),
            "register-like text should use a recognized EUI monospace font family");
    require(contains(mainCpp, "Microsoft YaHei") || contains(mainCpp, "YaHei"),
            "Chinese UI text should use a Windows UI font instead of the decorative EUI default");

    require(contains(workflow, "windows-latest"), "GitHub Actions must build the Windows GUI in the cloud");
    require(contains(workflow, "xc-hwbp-debugger.exe"), "GitHub Actions must verify the packaged exe exists");
    require(contains(workflow, "actions/upload-artifact@v4"), "GitHub Actions must upload the Windows exe artifact");
    require(contains(euiPatch, "FT_LOAD_TARGET_LIGHT") && contains(euiPatch, "string(REPLACE"),
            "EUI text renderer patch should enable FreeType hinting for clearer small text");
    require(contains(euiPatch, "SetProcessDpiAwarenessContext") && contains(euiPatch, "DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2"),
            "EUI app runner patch should enable per-monitor DPI awareness before window creation");

    std::cout << "ui contract tests passed\n";
    return 0;
}
