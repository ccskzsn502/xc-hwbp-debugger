add_library(xc_mcp_core STATIC
    src/mcp_core.cpp
)

target_include_directories(xc_mcp_core PUBLIC include)
target_link_libraries(xc_mcp_core PUBLIC xc_shared)
target_compile_features(xc_mcp_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(xc_mcp_core PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(xc_mcp_core PUBLIC ws2_32)
endif()

if(XC_BUILD_MCP_SERVER)
    add_executable(xc-hwbp-mcp
        src/main.cpp
    )

    target_link_libraries(xc-hwbp-mcp PRIVATE xc_mcp_core)

    target_compile_features(xc-hwbp-mcp PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(xc-hwbp-mcp PRIVATE /utf-8)
    endif()

    install(TARGETS xc-hwbp-mcp RUNTIME DESTINATION .)
endif()
