#pragma once

#include "renderer.h"

#include <shellapi.h>
#include <memory>
#include <string>
#include <windows.h>

class AudioMeter;
class BrightnessPoller;
class MusicPoller;
class ProcessPoller;
class ProcessSampler;
class TemperaturePoller;
class WeatherFetcher;

struct WeatherSettings {
    std::wstring api_key;
    std::wstring city = L"Cileungsi";
    std::wstring country_code = L"ID";
    std::wstring units = L"metric";
    int city_id = 0;
    int refresh_sec = 600;
    bool show_humidity = true;
    bool show_wind = true;
};

struct PositionSettings {
    // "right" → anchored to the right edge of the work area (default).
    // "left"  → anchored to the left edge.
    std::wstring anchor = L"right";
    int x = 16;  // distance from anchor edge
    int y = 16;  // distance from top of work area
};

class WidgetApp {
public:
    explicit WidgetApp(HINSTANCE instance);
    ~WidgetApp();

    WidgetApp(const WidgetApp&) = delete;
    WidgetApp& operator=(const WidgetApp&) = delete;

    bool Initialize();
    int Run();

    // Public so SettingsDialog can read/write autostart from its own UI.
    static bool IsAutostartEnabled();
    static void SetAutostartEnabled(bool enabled);

private:
    static LRESULT CALLBACK WindowProcSetup(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK WindowProcThunk(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void OnPaint();
    void OnCommand(UINT command);
    void UpdateSnapshot();
    void UpdateSystemSnapshot();
    void UpdateNetworkSnapshot();
    void UpdateStorageSnapshot();
    void UpdateAudioSnapshot();
    void UpdateWeatherSnapshot();
    void UpdateProcessSnapshot();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void OpenSettings();
    void RestartApp();

    static HICON BuildAppIcon(int size);

    static WeatherSettings LoadWeatherSettings();
    static PositionSettings LoadPositionSettings();
    void LoadNetworkState();
    void SaveNetworkState();
    static void FormatThroughput(unsigned long long bytes_per_second, wchar_t* output,
                                 size_t output_size, bool suffix_up);
    static unsigned long long FileTimeToUint64(const FILETIME& value);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    Renderer renderer_;
    WidgetSnapshot snapshot_;
    std::unique_ptr<AudioMeter> audio_meter_;
    std::unique_ptr<MusicPoller> music_poller_;
    std::unique_ptr<ProcessPoller> process_poller_;
    std::unique_ptr<TemperaturePoller> temperature_poller_;
    std::unique_ptr<WeatherFetcher> weather_fetcher_;
    std::unique_ptr<BrightnessPoller> brightness_poller_;
    bool visible_ = true;
    // Smart-invalidate state: only call InvalidateRect when something the
    // user can actually see has changed. Saves a lot of UpdateLayeredWindow
    // calls (the expensive part of every redraw).
    bool audio_dirty_ = false;
    UINT audio_interval_ms_ = 90;  // adaptive: 90 ms playing, 250 ms idle
    WeatherSettings weather_settings_;
    PositionSettings position_settings_;
    FILETIME previous_idle_{};
    FILETIME previous_kernel_{};
    FILETIME previous_user_{};
    bool has_previous_cpu_sample_ = false;
    // (temperature now handled by TemperaturePoller)
    unsigned long long previous_network_luid_ = 0;
    unsigned long long previous_network_in_ = 0;
    unsigned long long previous_network_out_ = 0;
    ULONGLONG previous_network_tick_ = 0;
    bool has_previous_network_sample_ = false;
    // Today's network usage accounting. Baselines are the cumulative
    // counter values at the start of the current calendar day; today's
    // usage = (current - baseline). Reset when the day-of-year changes.
    // Persisted to config.json so widget restarts don't lose the count.
    unsigned long long today_baseline_in_ = 0;
    unsigned long long today_baseline_out_ = 0;
    int today_yday_ = -1;     // tm_yday of the current accounting day
    int today_year_ = -1;     // tm_year, so Jan 1 of next year resets too
    ULONGLONG last_network_save_tick_ = 0;
};
