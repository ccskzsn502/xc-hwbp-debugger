include(FetchContent)

FetchContent_Declare(
    eui_neo
    GIT_REPOSITORY https://github.com/sudoevolve/EUI-NEO.git
    GIT_TAG main
)
set(EUI_BUILD_APPS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(eui_neo)

add_executable(xc-hwbp-debugger
    ${eui_neo_SOURCE_DIR}/core/app/glfw_app_main.cpp
    src/main.cpp
)

target_link_libraries(xc-hwbp-debugger PRIVATE xc_shared eui::neo)
eui_neo_configure_app(xc-hwbp-debugger)

install(TARGETS xc-hwbp-debugger RUNTIME DESTINATION .)
install(DIRECTORY "${eui_neo_SOURCE_DIR}/assets/" DESTINATION assets)

if(WIN32)
    target_link_libraries(xc-hwbp-debugger PRIVATE ws2_32)
endif()
