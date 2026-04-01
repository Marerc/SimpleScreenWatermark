cmake_minimum_required(VERSION 3.15)
project(SimpleScreenMark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SimpleScreenMark WIN32
    src/main.cpp
    src/config.cpp
    src/overlay.cpp
    src/renderer.cpp
    src/trayicon.cpp
    src/hotkey.cpp
    src/netinfo.cpp
    src/autostart.cpp
    res/app.rc
)

target_include_directories(SimpleScreenMark PRIVATE src)

target_compile_definitions(SimpleScreenMark PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Treat all source files as UTF-8 regardless of system code page
target_compile_options(SimpleScreenMark PRIVATE /utf-8)

target_link_libraries(SimpleScreenMark PRIVATE
    gdiplus
    iphlpapi
    ws2_32
    shlwapi
    shell32
    user32
    gdi32
    advapi32
    ole32
)

# Disable auto-generated manifest — we embed our own in app.rc
set_target_properties(SimpleScreenMark PROPERTIES
    LINK_FLAGS "/MANIFEST:NO"
)
