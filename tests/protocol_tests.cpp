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

    const std::string setRequest = xc::breakpointSetRequestJson(7, 0, 0x78919CFF84ULL, "execute", 4);
    require(setRequest == R"({"id":7,"cmd":"breakpoint.set","slot":0,"address":"0x78919cff84","type":"execute","size":4})", "breakpoint.set request should serialize address/type/size");

    const auto setParsed = xc::parseBreakpointSetRequest(setRequest);
    require(setParsed.id == 7, "breakpoint.set id should parse");
    require(setParsed.slot == 0, "breakpoint.set slot should parse");
    require(setParsed.address == 0x78919CFF84ULL, "breakpoint.set address should parse hex string");
    require(setParsed.type == "execute", "breakpoint.set type should parse");
    require(setParsed.size == 4, "breakpoint.set size should parse");

    const std::string setResponseJson = R"({"id":7,"ok":true,"breakpoint":{"slot":0,"address":"0x78919cff84","type":"execute","size":4,"enabled":true,"message":"硬件断点已写入 slot0"}})";
    const auto setResponse = xc::parseBreakpointSetResponse(setResponseJson);
    require(setResponse.ok, "breakpoint.set response ok should parse");
    require(setResponse.slot == 0, "breakpoint.set response slot should parse");
    require(setResponse.address == 0x78919CFF84ULL, "breakpoint.set response address should parse");
    require(setResponse.type == "execute", "breakpoint.set response type should parse");
    require(setResponse.size == 4, "breakpoint.set response size should parse");
    require(setResponse.enabled, "breakpoint.set response enabled should parse");
    require(setResponse.message == "硬件断点已写入 slot0", "breakpoint.set response message should parse");

    const auto setFailed = xc::parseBreakpointSetResponse(R"({"id":8,"ok":false,"error":"slot out of range"})");
    require(!setFailed.ok, "failed breakpoint.set should parse ok=false");
    require(setFailed.error == "slot out of range", "failed breakpoint.set error should parse");

    std::cout << "protocol tests passed\n";
    return 0;
}
