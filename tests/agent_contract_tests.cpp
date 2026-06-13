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
    require(argc == 2, "agent contract test requires repository root argument");
    const std::filesystem::path root = argv[1];
    const std::string mainCpp = readFile(root / "agent" / "src" / "main.cpp");

    require(!contains(mainCpp, "hwbp_info info(std::string& error)"),
            "agent must not return multi-megabyte hwbp_info by value on Android");
    require(!contains(mainCpp, "hwbp_point records(std::uint32_t slot, std::string& error)"),
            "agent must not return large hwbp_point records by value on Android");
    require(!contains(mainCpp, "const hwbp_info info = breakpoints.info(error)"),
            "request handling must not copy hwbp_info onto the stack");
    require(!contains(mainCpp, "const hwbp_point point = breakpoints.records(slot, error)"),
            "request handling must not copy hwbp_point onto the stack");
    require(contains(mainCpp, "kMaxRecordsPerResponse"),
            "records.get must cap returned records so one JSONL response stays below the PC receive limit");

    std::cout << "agent contract tests passed\n";
    return 0;
}
