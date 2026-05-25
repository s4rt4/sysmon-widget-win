# Native Prototype

This folder contains the Phase 0 native Win32 prototype for Sysmon Widget.

Current scope:

- Borderless widget window.
- Hidden from taskbar and Alt-Tab with `WS_EX_TOOLWINDOW`.
- No focus stealing with `WS_EX_NOACTIVATE`.
- Color-key transparent background.
- Direct2D/DirectWrite static panel rendering.
- Tray icon with Show, Hide, and Exit.

Build from a Visual Studio Developer PowerShell after installing CMake:

```powershell
cmake -S . -B build-native -G "Visual Studio 17 2022" -A x64
cmake --build build-native --config Release
.\build-native\Release\SysmonWidgetNative.exe
```

The Python implementation remains the reference app until the native rewrite
reaches feature parity.
