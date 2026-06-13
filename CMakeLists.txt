cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
endif()
project(xc_hwbp_debugger VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(XC_BUILD_AGENT "Build Android hardware breakpoint agent" ON)
option(XC_BUILD_PC_CLIENT "Build desktop GUI client" ON)
option(XC_BUILD_MCP_SERVER "Build stdio MCP server for AI debugger control" OFF)

add_library(xc_shared INTERFACE)
target_include_directories(xc_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/shared/include)

if(XC_BUILD_AGENT)
    add_subdirectory(agent)
endif()

if(XC_BUILD_PC_CLIENT)
    add_subdirectory(pc-client)
endif()

if(XC_BUILD_MCP_SERVER)
    add_subdirectory(mcp-server)
endif()

option(XC_BUILD_TESTS "Build tests" OFF)
if(XC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
