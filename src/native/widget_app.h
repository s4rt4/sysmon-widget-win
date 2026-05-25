#pragma once

#include "renderer.h"

#include <shellapi.h>
#include <memory>
#include <string>
#include <windows.h>

class AudioMeter;

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

class WidgetApp {
public:
    explicit WidgetApp(HINSTANCE instance);
    ~WidgetApp();

    WidgetApp(const WidgetApp&) = delete;
    WidgetApp& operator=(const WidgetApp&) = delete;

    bool Initialize();
    int Run();

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
    void UpdateMusicSnapshot();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();

    static bool ReadTemperatureCelsius(float* temperature);
    static WeatherSettings LoadWeatherSettings();
    static bool FetchWeather(const WeatherSettings& settings, WidgetSnapshot* snapshot);
    static bool QueryWmiLong(const wchar_t* namespace_name, const wchar_t* query,
                             const wchar_t* property, long* value);
    static void FormatThroughput(unsigned long long bytes_per_second, wchar_t* output,
                                 size_t output_size, bool suffix_up);
    static unsigned long long FileTimeToUint64(const FILETIME& value);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    Renderer renderer_;
    WidgetSnapshot snapshot_;
    std::unique_ptr<AudioMeter> audio_meter_;
    WeatherSettings weather_settings_;
    ULONGLONG last_weather_tick_ = 0;
    FILETIME previous_idle_{};
    FILETIME previous_kernel_{};
    FILETIME previous_user_{};
    bool has_previous_cpu_sample_ = false;
    ULONGLONG last_temperature_tick_ = 0;
    bool has_temperature_sample_ = false;
    unsigned long long previous_network_luid_ = 0;
    unsigned long long previous_network_in_ = 0;
    unsigned long long previous_network_out_ = 0;
    ULONGLONG previous_network_tick_ = 0;
    bool has_previous_network_sample_ = false;
};
