add_executable(xc-protocol-tests protocol_tests.cpp)
target_link_libraries(xc-protocol-tests PRIVATE xc_shared)
add_test(NAME xc-protocol-tests COMMAND xc-protocol-tests)

add_executable(xc-ui-contract-tests ui_contract_tests.cpp)
add_test(NAME xc-ui-contract-tests COMMAND xc-ui-contract-tests ${CMAKE_SOURCE_DIR})
