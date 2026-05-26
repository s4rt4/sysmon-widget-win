#pragma once

#include "widget_app.h"  // for WeatherSettings

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

// Background fetcher for the OpenWeatherMap API. WinHTTP calls block up
// to 8 s (configured timeout) and would otherwise freeze the entire UI
// once per refresh interval. We move them to a dedicated thread that
// publishes results into atomic-protected fields the UI samples each tick.
class WeatherFetcher {
public:
    struct Result {
        std::wstring temp;        // "33°C"
        std::wstring city;        // "Cileungsi"
        std::wstring detail;      // "Scattered Clouds"
        std::wstring meta;        // "H 74%  W 1.5"
        std::wstring icon;        // "☁"
    };

    explicit WeatherFetcher(const WeatherSettings& initial);
    ~WeatherFetcher();

    WeatherFetcher(const WeatherFetcher&) = delete;
    WeatherFetcher& operator=(const WeatherFetcher&) = delete;

    // UI side. Update the settings used by the next fetch (e.g. after the
    // user changes the city or API key in the dialog). Also triggers an
    // immediate refresh so the panel doesn't wait the full refresh_sec.
    void UpdateSettings(const WeatherSettings& settings);

    // UI side. Copies latest result into out. Returns true if any
    // successful fetch has completed.
    bool TryGet(Result* out) const;

private:
    void WorkerLoop();
    static bool FetchOnce(const WeatherSettings& s, Result* out);

    std::thread worker_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> request_now_{false};
    std::atomic<bool> has_value_{false};

    // Protected by mu_.
    WeatherSettings settings_;
    Result latest_;
};
