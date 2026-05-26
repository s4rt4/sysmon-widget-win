#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// Background poller for display brightness via WMI (WmiMonitorBrightness).
// Same threading pattern as TemperaturePoller — WMI queries cost 50–200 ms
// and would freeze the UI if run on the main thread.
//
// Many desktop monitors don't expose WmiMonitorBrightness; TryGet returns
// false in that case and the UI shows N/A.
class BrightnessPoller {
public:
    BrightnessPoller();
    ~BrightnessPoller();

    BrightnessPoller(const BrightnessPoller&) = delete;
    BrightnessPoller& operator=(const BrightnessPoller&) = delete;

    // UI thread. Returns true if a brightness reading is available; writes
    // 0–100 to *out. Returns false on unsupported hardware.
    bool TryGet(int* out) const;

private:
    void WorkerLoop();

    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> has_value_{false};
    std::atomic<int> value_{0};
};
