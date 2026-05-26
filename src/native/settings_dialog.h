#pragma once

#include "widget_app.h"

#include <windows.h>

// Modal-ish settings dialog. Edits weather config, widget position, and
// the autostart toggle. Disables `parent` while open. Returns true if the
// user clicked Save (in which case the out_* params are populated and
// config.json has been written).
class SettingsDialog {
public:
    static bool Show(HWND parent,
                     const WeatherSettings& weather_in,
                     const PositionSettings& position_in,
                     WeatherSettings* weather_out,
                     PositionSettings* position_out);
};
