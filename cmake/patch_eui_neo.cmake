function(xc_patch_eui_neo EUI_NEO_SOURCE_DIR)
    set(text_cpp "${EUI_NEO_SOURCE_DIR}/core/render/text.cpp")
    set(glfw_main_cpp "${EUI_NEO_SOURCE_DIR}/core/app/glfw_app_main.cpp")
    set(runtime_render_h "${EUI_NEO_SOURCE_DIR}/core/runtime/runtime_render.h")

    if(NOT EXISTS "${text_cpp}")
        message(FATAL_ERROR "EUI-NEO text renderer not found: ${text_cpp}")
    endif()
    file(READ "${text_cpp}" text_cpp_content)
    string(REPLACE
        "constexpr FT_Int32 kGlyphLoadFlags = FT_LOAD_DEFAULT | FT_LOAD_COLOR | FT_LOAD_NO_SVG | FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT;"
        "constexpr FT_Int32 kGlyphLoadFlags = FT_LOAD_DEFAULT | FT_LOAD_COLOR | FT_LOAD_NO_SVG | FT_LOAD_TARGET_LIGHT;"
        text_cpp_content
        "${text_cpp_content}")
    file(WRITE "${text_cpp}" "${text_cpp_content}")

    if(NOT EXISTS "${glfw_main_cpp}")
        message(FATAL_ERROR "EUI-NEO GLFW app runner not found: ${glfw_main_cpp}")
    endif()
    file(READ "${glfw_main_cpp}" glfw_main_content)
    if(NOT glfw_main_content MATCHES "enableProcessDpiAwareness")
        string(REPLACE
            "#include <mmsystem.h>\n#endif"
            "#include <mmsystem.h>\n#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2\n#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)\n#endif\n#endif"
            glfw_main_content
            "${glfw_main_content}")
        string(REPLACE
            "struct TimerResolutionGuard {"
            "void enableProcessDpiAwareness() {\n#ifdef _WIN32\n    HMODULE user32 = GetModuleHandleA(\"user32.dll\");\n    if (user32 != nullptr) {\n        using SetDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);\n        auto setDpiAwarenessContext = reinterpret_cast<SetDpiAwarenessContextFn>(\n            GetProcAddress(user32, \"SetProcessDpiAwarenessContext\"));\n        if (setDpiAwarenessContext != nullptr &&\n            setDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {\n            return;\n        }\n    }\n    SetProcessDPIAware();\n#endif\n}\n\nstruct TimerResolutionGuard {"
            glfw_main_content
            "${glfw_main_content}")
        string(REPLACE
            "int main() {\n    core::render::initializeRenderBackendLoader();"
            "int main() {\n    enableProcessDpiAwareness();\n    core::render::initializeRenderBackendLoader();"
            glfw_main_content
            "${glfw_main_content}")
        file(WRITE "${glfw_main_cpp}" "${glfw_main_content}")
    endif()

    if(NOT EXISTS "${runtime_render_h}")
        message(FATAL_ERROR "EUI-NEO runtime renderer not found: ${runtime_render_h}")
    endif()
    file(READ "${runtime_render_h}" runtime_render_content)
    if(NOT runtime_render_content MATCHES "std::round\\(x\\);")
        string(REPLACE
            "    instance.primitive->setPosition(x, y);"
            "    x = std::round(x);\n    y = std::round(y);\n    instance.primitive->setPosition(x, y);"
            runtime_render_content
            "${runtime_render_content}")
        file(WRITE "${runtime_render_h}" "${runtime_render_content}")
    endif()
endfunction()

if(DEFINED EUI_NEO_SOURCE_DIR)
    xc_patch_eui_neo("${EUI_NEO_SOURCE_DIR}")
endif()
