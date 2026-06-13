add_executable(xc-hwbp-mcp
    src/main.cpp
)

target_link_libraries(xc-hwbp-mcp PRIVATE xc_shared)

target_compile_features(xc-hwbp-mcp PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(xc-hwbp-mcp PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(xc-hwbp-mcp PRIVATE ws2_32)
endif()

install(TARGETS xc-hwbp-mcp RUNTIME DESTINATION .)
