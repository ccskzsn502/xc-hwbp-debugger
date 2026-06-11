add_executable(xc-hwbp-agent
    src/main.cpp
)

target_link_libraries(xc-hwbp-agent PRIVATE xc_shared)

if(ANDROID)
    target_link_libraries(xc-hwbp-agent PRIVATE log)
endif()
