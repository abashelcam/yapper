find_package(Opus CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
find_path(OPENH264_INCLUDE_DIR wels/codec_api.h REQUIRED)
find_library(OPENH264_LIBRARY openh264 REQUIRED)

# The networking side of the client as a library, so the tests can use the real client too
add_library(yapper_client_lib STATIC
        src/net/connection.cpp
)

target_include_directories(yapper_client_lib PUBLIC src/net src/audio)
target_link_libraries(yapper_client_lib PUBLIC yapper::common)

# The client program
add_executable(yapper_client WIN32
        src/audio/audio.cpp
        src/screen/screen_share.cpp
        src/screen/screen_viewer.cpp
        src/ui/window.cpp

        src/app.cpp
        src/main.cpp
        src/settings.cpp
)

target_include_directories(yapper_client PRIVATE src ${MINIAUDIO_INCLUDE_DIR} ${OPENH264_INCLUDE_DIR})
target_link_libraries(yapper_client PRIVATE
        imgui::imgui
        yapper_client_lib
        Opus::opus
        nlohmann_json::nlohmann_json
        ${OPENH264_LIBRARY}
        d3d11
        dxgi
)
