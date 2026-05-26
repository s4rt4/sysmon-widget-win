#include "process_poller.h"

#include <chrono>

namespace {
constexpr auto kSampleInterval = std::chrono::seconds(5);
}

ProcessPoller::ProcessPoller(int top_n) : top_n_(top_n) {
    worker_ = std::thread([this] { WorkerLoop(); });
}

ProcessPoller::~ProcessPoller() {
    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ProcessPoller::WorkerLoop() {
    try {
        ProcessSampler sampler;
        // Prime CPU-times cache so the second sample has meaningful deltas.
        sampler.Sample(0);

        while (!stop_.load()) {
            auto entries = sampler.Sample(top_n_);
            {
                std::lock_guard<std::mutex> lock(mu_);
                latest_ = std::move(entries);
                has_value_.store(true);
            }

            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, kSampleInterval, [this] { return stop_.load(); });
        }
    } catch (...) {
        // Worker exceptions would otherwise std::terminate the process.
    }
}

bool ProcessPoller::TryGet(std::vector<ProcessSampler::Entry>* out) const {
    if (!has_value_.load() || !out) return false;
    std::lock_guard<std::mutex> lock(mu_);
    *out = latest_;
    return true;
}
