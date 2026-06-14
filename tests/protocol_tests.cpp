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

    const std::string statusJson = R"({"id":1,"ok":true,"driver":{"name":"lsdriver","module_loaded":true,"proc_modules_readable":true,"kernel_release":"5.15.189-android13","message":"driver loaded"}})";
    const auto status = xc::parseDriverStatusResponse(statusJson);
    require(status.ok, "driver status ok should be true");
    require(status.name == "lsdriver", "driver name should parse");
    require(status.moduleLoaded, "module_loaded should parse true");
    require(status.procModulesReadable, "proc_modules_readable should parse true");
    require(status.kernelRelease == "5.15.189-android13", "kernel release should parse");
    require(status.message == "driver loaded", "driver message should parse");

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

    const std::string moduleSetRequest = xc::breakpointSetModuleRequestJson(9, 2, "libtersafe.so", 0x488F08ULL, "access", 8, "com.tencent.tmgp.sgame");
    require(moduleSetRequest == R"({"id":9,"cmd":"breakpoint.set","slot":2,"module":"libtersafe.so","offset":"0x488f08","type":"access","size":8,"target":"com.tencent.tmgp.sgame"})",
            "breakpoint.set request should serialize module+offset target syntax");
    const auto moduleSetParsed = xc::parseBreakpointSetRequest(moduleSetRequest);
    require(moduleSetParsed.id == 9, "module breakpoint id should parse");
    require(moduleSetParsed.slot == 2, "module breakpoint slot should parse");
    require(moduleSetParsed.address == 0, "module breakpoint absolute address should stay zero before agent resolves it");
    require(moduleSetParsed.module == "libtersafe.so", "module breakpoint module should parse");
    require(moduleSetParsed.offset == 0x488F08ULL, "module breakpoint offset should parse");
    require(moduleSetParsed.type == "access", "module breakpoint type should parse");
    require(moduleSetParsed.size == 8, "module breakpoint size should parse");
    require(moduleSetParsed.target == "com.tencent.tmgp.sgame", "module breakpoint target should parse");

    const std::string setResponseJson = R"({"id":7,"ok":true,"breakpoint":{"slot":0,"address":"0x78919cff84","type":"execute","size":4,"enabled":true,"message":"breakpoint written slot0"}})";
    const auto setResponse = xc::parseBreakpointSetResponse(setResponseJson);
    require(setResponse.ok, "breakpoint.set response ok should parse");
    require(setResponse.slot == 0, "breakpoint.set response slot should parse");
    require(setResponse.address == 0x78919CFF84ULL, "breakpoint.set response address should parse");
    require(setResponse.type == "execute", "breakpoint.set response type should parse");
    require(setResponse.size == 4, "breakpoint.set response size should parse");
    require(setResponse.enabled, "breakpoint.set response enabled should parse");
    require(setResponse.message == "breakpoint written slot0", "breakpoint.set response message should parse");

    const auto setFailed = xc::parseBreakpointSetResponse(R"({"id":8,"ok":false,"error":"slot out of range"})");
    require(!setFailed.ok, "failed breakpoint.set should parse ok=false");
    require(setFailed.error == "slot out of range", "failed breakpoint.set error should parse");

    const std::string recordsRequest = xc::recordsGetRequestJson(10, 2);
    require(recordsRequest == R"({"id":10,"cmd":"records.get","slot":2})",
            "records.get request should serialize slot");
    const auto records = xc::parseRecordsGetResponse(
        R"({"id":10,"ok":true,"slot":2,"address":"0x78919cff80","type":4,"size":4,"record_count":12,"returned":1,"modules":[{"module":"libtersafe.so","path":"/data/app/libtersafe.so","start":"0x7891900000","end":"0x7891a00000","load_base":"0x7891900000"},{"module":"[stack]","path":"[stack]","start":"0x7fd0010000","end":"0x7fd0020000","load_base":"0x7fd0010000"}],"records":[{"hit_count":1,"pc":"0x78919cff84","lr":"0x78919cff90","sp":"0x7fd0011000","pstate":"0x60000000","syscallno":"0x0","x0":"0x11","x1":"0x22","x2":"0x33","x3":"0x44"}]})");
    require(records.ok, "records.get ok should parse");
    require(records.slot == 2, "records.get slot should parse");
    require(records.address == 0x78919CFF80ULL, "records.get breakpoint address should parse");
    require(records.type == 4, "records.get breakpoint type should parse");
    require(records.size == 4, "records.get breakpoint size should parse");
    require(records.recordCount == 12, "records.get record_count should parse for cumulative hit numbering");
    require(records.returned == 1, "records.get returned should parse for visible hit window numbering");
    require(records.records.size() == 1, "records.get should parse one hit record");
    require(records.records[0].hitCount == 1, "records.get per-record hit_count should parse even when driver stores 1 per snapshot");
    require(records.records[0].pc == 0x78919CFF84ULL, "records.get pc should parse");
    require(records.records[0].lr == 0x78919CFF90ULL, "records.get lr should parse");
    require(records.records[0].sp == 0x7FD0011000ULL, "records.get sp should parse");
    require(records.records[0].x[0] == 0x11, "records.get x0 should parse");
    require(records.records[0].x[3] == 0x44, "records.get x3 should parse");
    require(records.modules.size() == 2, "records.get should parse agent maps needed for precise module+offset rendering");
    require(records.modules[0].module == "libtersafe.so", "records.get module name should parse");
    require(records.modules[0].loadBase == 0x7891900000ULL, "records.get module load base should parse");
    require(records.modules[1].module == "[stack]", "records.get stack mapping should parse");
    require(xc::visibleHitNumber(records, 0) == 12, "visible hit number should use record_count/returned instead of per-record hit_count");
    require(xc::resolveAddressWithModules(records.records[0].pc, records.modules) == "0x78919cff84  libtersafe.so+0xcff84", "PC should resolve through agent maps");
    require(xc::resolveAddressWithModules(records.records[0].sp, records.modules) == "0x7fd0011000  [stack]+0x1000", "SP should resolve through stack map");

    const auto recordsFailed = xc::parseRecordsGetResponse(R"({"id":11,"ok":false,"error":"slot out of range"})");
    require(!recordsFailed.ok, "failed records.get should parse ok=false");
    require(recordsFailed.error == "slot out of range", "failed records.get error should parse");

    std::cout << "protocol tests passed\n";
    return 0;
}
