cmake_minimum_required(VERSION 3.20)
project(xc_hwbp_debugger VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XC_BUILD_AGENT "Build Android hardware breakpoint agent" ON)
option(XC_BUILD_PC_CLIENT "Build desktop GUI client" ON)

add_library(xc_shared INTERFACE)
target_include_directories(xc_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/shared/include)

if(XC_BUILD_AGENT)
    add_subdirectory(agent)
endif()

if(XC_BUILD_PC_CLIENT)
    add_subdirectory(pc-client)
endif()
