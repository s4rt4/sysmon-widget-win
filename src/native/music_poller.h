#pragma once

#include "renderer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// MusicPoller runs SMTC queries on a dedicated worker thread so a slow
// WinRT call cannot freeze the UI. It also implements the same defensive
// guards we battle-tested in the Python build:
//
//   * Watchdog (5 min)        — abandon hung query, increment request id
//   * Circuit breaker          — 3-of-5 last queries > 10s → pause 10 min
//   * End-of-track auto-stop   — position pinned at duration → status Stopped
//   * Adaptive refresh interval — 5s/15s/30s/60s based on state
//   * Unknown-meta filter       — strip literal "Unknown" artist/album
//   * Dynamic status icon       — ▶ Playing / ⏸ Paused / ♪ Stopped
//
// Call Tick() once per second from the UI thread; it drains worker results,
// drives the local position ticker, runs the watchdog/circuit logic, and
// schedules the next worker fetch when the refresh interval has elapsed.
class MusicPoller {
public:
    MusicPoller();
    ~MusicPoller();

    MusicPoller(const MusicPoller&) = delete;
    MusicPoller& operator=(const MusicPoller&) = delete;

    // Called from UI thread once per second.
    void Tick(WidgetSnapshot& snapshot, uint64_t now_ms);

    // Called from UI thread on user action. Resets guards and forces an
    // immediate fetch.
    void ForceRefresh();

private:
    struct Result {
        std::uint64_t request_id = 0;
        bool has_session = false;
        std::wstring status;   // "Playing" / "Paused" / "Stopped"
        std::wstring title;
        std::wstring artist;
        double position = 0.0;
        double duration = 0.0;
        std::uint64_t elapsed_ms = 0;
    };

    void WorkerLoop();
    void ApplyResult(WidgetSnapshot& snapshot, const Result& r);
    void RequestFetch(std::uint64_t now_ms);
    void MaybeOpenCircuit(std::uint64_t now_ms);
    std::uint64_t RefreshIntervalMs() const;

    static std::wstring CleanMeta(const std::wstring& v);
    static const wchar_t* StatusIcon(const std::wstring& status);

    // Worker-thread plumbing.
    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> pending_request_id_{0};
    std::queue<Result> results_;

    // UI-thread state.
    std::uint64_t next_request_id_ = 1;
    std::uint64_t in_flight_request_id_ = 0;
    bool in_flight_ = false;
    std::uint64_t in_flight_started_ms_ = 0;
    std::uint64_t last_scheduled_ms_ = 0;
    bool timed_out_ = false;
    std::vector<std::uint64_t> durations_ms_;
    std::uint64_t circuit_open_until_ms_ = 0;

    int end_of_track_ticks_ = 0;
    std::wstring current_status_ = L"Stopped";
};
