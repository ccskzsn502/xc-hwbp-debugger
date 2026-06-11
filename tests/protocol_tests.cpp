#include "xc/protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    const std::string helloJson = R"({"id":0,"ok":true,"name":"xc-hwbp-agent","protocol":"xc-hwbp-jsonl","version":1})";
    const auto hello = xc::parseHelloResponse(helloJson);
    require(hello.ok, "hello ok should be true");
    require(hello.name == "xc-hwbp-agent", "hello name should parse");
    require(hello.protocol == "xc-hwbp-jsonl", "hello protocol should parse");
    require(hello.version == 1, "hello version should parse");

    const std::string statusJson = R"({"id":1,"ok":true,"driver":{"name":"lsdriver","module_loaded":true,"proc_modules_readable":true,"kernel_release":"5.15.189-android13","message":"lsdriver 驱动模块已加载"}})";
    const auto status = xc::parseDriverStatusResponse(statusJson);
    require(status.ok, "driver status ok should be true");
    require(status.name == "lsdriver", "driver name should parse");
    require(status.moduleLoaded, "module_loaded should parse true");
    require(status.procModulesReadable, "proc_modules_readable should parse true");
    require(status.kernelRelease == "5.15.189-android13", "kernel release should parse");
    require(status.message == "lsdriver 驱动模块已加载", "driver message should parse unicode text");

    const auto failed = xc::parseDriverStatusResponse(R"({"id":2,"ok":false,"error":"bad command"})");
    require(!failed.ok, "failed status should parse ok=false");
    require(failed.error == "bad command", "error should parse");

    std::cout << "protocol tests passed\n";
    return 0;
}
