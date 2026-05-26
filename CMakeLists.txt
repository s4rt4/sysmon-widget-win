cmake_minimum_required(VERSION 3.24)

project(SysmonWidgetNative LANGUAGES CXX)

# Static-link the MSVC runtime so the EXE doesn't need vcruntime DLLs
# installed on the target machine. Slightly larger binary (~500 KB) but
# fully portable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(SysmonWidgetNative WIN32
    src/native/app.rc
    src/native/main.cpp
    src/native/music_poller.cpp
    src/native/process_poller.cpp
    src/native/process_sampler.cpp
    src/native/renderer.cpp
    src/native/settings_dialog.cpp
    src/native/temperature_poller.cpp
    src/native/weather_fetcher.cpp
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
    comctl32
    comdlg32
    d2d1
    dwrite
    gdi32
    gdiplus
    iphlpapi
    ole32
    oleaut32
    psapi
    shell32
    user32
    uuid
    wbemuuid
    winhttp
    windowsapp
)

if(MSVC)
    # /utf-8: treat source files as UTF-8 (default MSVC interprets them as
    # the system ANSI codepage, which mojibakes literal ▶ ⏸ ♪ glyphs into
    # â–¶ etc. in the music panel status icon).
    target_compile_options(SysmonWidgetNative PRIVATE /W4 /permissive- /utf-8)
endif()
