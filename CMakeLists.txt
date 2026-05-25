cmake_minimum_required(VERSION 3.24)

project(SysmonWidgetNative LANGUAGES CXX)

add_executable(SysmonWidgetNative WIN32
    src/native/main.cpp
    src/native/renderer.cpp
    src/native/widget_app.cpp
)

target_compile_features(SysmonWidgetNative PRIVATE cxx_std_20)

target_compile_definitions(SysmonWidgetNative PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0601
    NTDDI_VERSION=0x06010000
)

target_include_directories(SysmonWidgetNative PRIVATE
    src/native
)

target_link_libraries(SysmonWidgetNative PRIVATE
    advapi32
    d2d1
    dwrite
    gdi32
    iphlpapi
    ole32
    oleaut32
    shell32
    user32
    uuid
    wbemuuid
    winhttp
    windowsapp
)

if(MSVC)
    target_compile_options(SysmonWidgetNative PRIVATE /W4 /permissive-)
endif()
