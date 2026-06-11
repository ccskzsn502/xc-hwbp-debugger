add_executable(xc-protocol-tests protocol_tests.cpp)
target_link_libraries(xc-protocol-tests PRIVATE xc_shared)
add_test(NAME xc-protocol-tests COMMAND xc-protocol-tests)
