cmake_minimum_required(VERSION 3.25)

project(yapper)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_subdirectory(sources/common)
add_subdirectory(sources/server)

# The client and the tests are Windows only. The server also builds on Linux for Docker.
if(WIN32)
    add_subdirectory(third_party/imgui)
    add_subdirectory(sources/client)
    add_subdirectory(tests)
endif()
