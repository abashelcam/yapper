find_package(nlohmann_json CONFIG REQUIRED)

# The server code as a library, so the tests can start a real server too
add_library(yapper_server_lib STATIC
        src/app.cpp
        src/config.cpp
)

target_include_directories(yapper_server_lib PUBLIC src)
target_link_libraries(yapper_server_lib PUBLIC yapper::common nlohmann_json::nlohmann_json)

# The server program
add_executable(yapper_server
        src/main.cpp
)

target_link_libraries(yapper_server PRIVATE yapper_server_lib)
