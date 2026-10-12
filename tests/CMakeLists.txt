add_executable(yapper_tests
        tests.cpp
)

target_link_libraries(yapper_tests PRIVATE yapper_server_lib yapper_client_lib)

add_test(NAME yapper_tests COMMAND yapper_tests)
