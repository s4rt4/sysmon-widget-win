#include "widget_app.h"

#include <windows.h>
#include <winrt/base.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    WidgetApp app(instance);
    if (!app.Initialize()) {
        MessageBoxW(nullptr, L"Sysmon Widget failed to start.", L"Sysmon Widget", MB_ICONERROR | MB_OK);
        return 1;
    }

    return app.Run();
}
