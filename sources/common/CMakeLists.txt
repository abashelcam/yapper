find_package(asio CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(yapper_common STATIC
        src/crypto.cpp
        src/protocol.cpp
        src/secure_socket.cpp
        src/serialize.cpp
)
add_library(yapper::common ALIAS yapper_common)

target_include_directories(yapper_common PUBLIC src)
target_link_libraries(yapper_common PUBLIC asio::asio unofficial-sodium::sodium Threads::Threads)

# Asio's auto-detection turns coroutines off for clang-based tools like CLion's clangd,
# so it reports co_spawn as missing even though MSVC builds fine. Just tell it directly.
target_compile_definitions(yapper_common PUBLIC
        ASIO_HAS_CO_AWAIT=1
        ASIO_HAS_STD_COROUTINE=1
)

if(WIN32)
    # SODIUM_STATIC because vcpkg's x64-windows-static links libsodium statically.
    # NOMINMAX stops windows.h from defining min/max macros that break std::min/std::max.
    target_compile_definitions(yapper_common PUBLIC _WIN32_WINNT=0x0A00 SODIUM_STATIC NOMINMAX)
endif()
