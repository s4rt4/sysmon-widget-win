#include "widget_app.h"

#include <windows.h>
#include <winrt/base.h>

namespace {
// Enable Per-Monitor DPI awareness (Win10 1703+).
void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using PFN_SetCtx = BOOL (WINAPI*)(HANDLE);
        auto pfn = reinterpret_cast<PFN_SetCtx>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (pfn && pfn(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) {
            return;
        }
    }
    SetProcessDPIAware();
}

// Named-mutex single-instance guard. If a previous instance is already
// running we exit silently — no second tray icon, no dialog stacking.
// Handle is leaked on purpose; OS releases it when the process exits.
HANDLE g_single_instance = nullptr;
bool AcquireSingleInstance() {
    g_single_instance = CreateMutexW(nullptr, FALSE,
        L"Local\\SysmonWidgetNative_SingleInstance_v1");
    if (!g_single_instance) {
        return true;  // can't create mutex → don't block startup
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_single_instance);
        g_single_instance = nullptr;
        return false;
    }
    return true;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!AcquireSingleInstance()) {
        return 0;  // another instance is already running
    }
    EnableDpiAwareness();
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    WidgetApp app(instance);
    if (!app.Initialize()) {
        MessageBoxW(nullptr, L"Sysmon Widget failed to start.", L"Sysmon Widget", MB_ICONERROR | MB_OK);
        return 1;
    }

    return app.Run();
}
