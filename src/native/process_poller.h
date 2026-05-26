#pragma once

#include "process_sampler.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

// Background poller that runs ProcessSampler::Sample(2) on its own thread.
// Process enumeration walks every running process via CreateToolhelp32Snapshot
// + OpenProcess + GetProcessTimes/MemoryInfo — on a busy system this can
// take 50–200 ms, which would otherwise stall the UI every 5 s.
class ProcessPoller {
public:
    explicit ProcessPoller(int top_n = 2);
    ~ProcessPoller();

    ProcessPoller(const ProcessPoller&) = delete;
    ProcessPoller& operator=(const ProcessPoller&) = delete;

    // UI-thread side. Copies the latest top-N snapshot into `out`. Returns
    // true if at least one sample has completed since startup.
    bool TryGet(std::vector<ProcessSampler::Entry>* out) const;

private:
    void WorkerLoop();

    int top_n_;
    std::thread worker_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> has_value_{false};
    std::vector<ProcessSampler::Entry> latest_;  // protected by mu_
};
