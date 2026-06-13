find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(xc-hwbp-debugger
    src/main.cpp
)

target_link_libraries(xc-hwbp-debugger PRIVATE xc_shared Qt6::Widgets)

target_compile_features(xc-hwbp-debugger PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(xc-hwbp-debugger PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(xc-hwbp-debugger PRIVATE ws2_32)
endif()

install(TARGETS xc-hwbp-debugger RUNTIME DESTINATION .)
