#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// Background poller for CPU/thermal-zone temperature via WMI. WMI queries
// are notoriously slow (50–500 ms) and run on the UI thread would stall
// the widget every 10 s. We do them on a dedicated worker that sleeps in
// between samples.
class TemperaturePoller {
public:
    TemperaturePoller();
    ~TemperaturePoller();

    TemperaturePoller(const TemperaturePoller&) = delete;
    TemperaturePoller& operator=(const TemperaturePoller&) = delete;

    // UI-thread side. Returns true if a temperature reading is available
    // and writes it to *out (in °C). Returns false until the first sample
    // is in, and also when the hardware doesn't expose a thermal zone.
    bool TryGet(float* out) const;

private:
    void WorkerLoop();

    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> has_value_{false};
    std::atomic<float> value_{0.0f};
};
