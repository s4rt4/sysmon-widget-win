#include "music_poller.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cwctype>

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/base.h>

namespace {
// Thresholds — mirror the Python build so behaviour matches across binaries.
constexpr std::uint64_t kWatchdogMs       = 300'000;  // 5 min
constexpr std::uint64_t kSlowQueryMs      = 10'000;   // 10 s
constexpr std::uint64_t kCircuitOpenMs    = 600'000;  // 10 min
constexpr std::uint64_t kTimeoutMarkerMs  = 10'000;   // 10 s
constexpr std::size_t   kDurationsCap     = 5;
constexpr int           kSlowThreshold    = 3;        // 3 of last 5
constexpr int           kEndOfTrackTicks  = 3;        // ~3 s pinned at end

void CopyW(wchar_t* dest, std::size_t cap, const wchar_t* src) {
    wcsncpy_s(dest, cap, src ? src : L"", _TRUNCATE);
}
}  // namespace

MusicPoller::MusicPoller() {
    worker_ = std::thread([this] { WorkerLoop(); });
}

MusicPoller::~MusicPoller() {
    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) {
        // If the worker is mid-WinRT call we can't cancel it; join will wait
        // for that call to return. App exit is the only place this hits, so
        // accept the delay rather than detach (which would race the
        // destruction of our members).
        worker_.join();
    }
}

void MusicPoller::WorkerLoop() {
    // SMTC objects are agile, so MTA on the worker is fine even though the
    // UI thread uses STA. We wrap the whole loop in try/catch so a
    // WinRT/COM exception escaping a call (which can happen at shutdown
    // when proxies tear down) does not std::terminate the process.
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        return;
    }
    namespace media = winrt::Windows::Media::Control;

    try {
    while (!stop_.load()) {
        std::uint64_t request_id = 0;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&] {
                return stop_.load() || pending_request_id_.load() != 0;
            });
            if (stop_.load()) {
                break;
            }
            request_id = pending_request_id_.exchange(0);
        }
        if (request_id == 0) {
            continue;
        }

        Result result{};
        result.request_id = request_id;
        const std::uint64_t start = GetTickCount64();

        try {
            auto manager = media::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            auto session = manager.GetCurrentSession();
            if (!session) {
                result.has_session = false;
                result.status = L"Stopped";
            } else {
                auto playback = session.GetPlaybackInfo();
                switch (playback.PlaybackStatus()) {
                case media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
                    result.status = L"Playing"; break;
                case media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
                    result.status = L"Paused"; break;
                case media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
                    result.status = L"Stopped"; break;
                default:
                    result.status = L"Stopped"; break;
                }

                auto properties = session.TryGetMediaPropertiesAsync().get();
                auto timeline = session.GetTimelineProperties();
                result.has_session = true;
                result.title = std::wstring(properties.Title().c_str());
                result.artist = std::wstring(properties.Artist().c_str());
                result.position = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::seconds>(timeline.Position()).count());
                result.duration = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::seconds>(timeline.EndTime()).count());
            }
        } catch (const winrt::hresult_error&) {
            result.has_session = false;
            result.status = L"Stopped";
        } catch (...) {
            result.has_session = false;
            result.status = L"Stopped";
        }

        result.elapsed_ms = GetTickCount64() - start;

        {
            std::lock_guard<std::mutex> lock(mu_);
            results_.push(std::move(result));
        }
    }
    } catch (...) {
        // Swallow worker-side exceptions so the std::thread doesn't call
        // std::terminate on the process at shutdown.
    }
    // Pair with init_apartment so the apartment refcount goes back to zero
    // before the thread exits.
    try { winrt::uninit_apartment(); } catch (...) {}
}

void MusicPoller::Tick(WidgetSnapshot& snapshot, std::uint64_t now_ms) {
    // ─── Drain worker results ──────────────────────────────────────────
    bool has_result = false;
    Result latest{};
    {
        std::lock_guard<std::mutex> lock(mu_);
        while (!results_.empty()) {
            Result r = std::move(results_.front());
            results_.pop();
            // Only accept the result we're currently waiting on. Anything
            // older (after a watchdog kick) is silently discarded.
            if (r.request_id == in_flight_request_id_) {
                latest = std::move(r);
                has_result = true;
            }
        }
    }

    if (has_result) {
        durations_ms_.push_back(latest.elapsed_ms);
        if (durations_ms_.size() > kDurationsCap) {
            durations_ms_.erase(durations_ms_.begin());
        }
        MaybeOpenCircuit(now_ms);
        in_flight_ = false;
        timed_out_ = false;
        ApplyResult(snapshot, latest);
    } else if (in_flight_) {
        const std::uint64_t elapsed = now_ms - in_flight_started_ms_;
        if (!timed_out_ && elapsed > kTimeoutMarkerMs) {
            timed_out_ = true;
        }
        if (elapsed > kWatchdogMs) {
            durations_ms_.push_back(elapsed);
            if (durations_ms_.size() > kDurationsCap) {
                durations_ms_.erase(durations_ms_.begin());
            }
            MaybeOpenCircuit(now_ms);
            // Abandon. Any eventual result from the hung thread is dropped
            // by the request_id filter above.
            in_flight_ = false;
            timed_out_ = false;
        }
    }

    // ─── Local 1 Hz position ticker + end-of-track detection ───────────
    if (current_status_ == L"Playing") {
        double new_pos = snapshot.music_position + 1.0;
        if (snapshot.music_duration > 0 && new_pos >= snapshot.music_duration) {
            new_pos = snapshot.music_duration;
            ++end_of_track_ticks_;
            if (end_of_track_ticks_ >= kEndOfTrackTicks) {
                current_status_ = L"Stopped";
                snapshot.music_playing = false;
                wchar_t buf[64];
                swprintf_s(buf, L"%s %s", StatusIcon(current_status_), current_status_.c_str());
                CopyW(snapshot.music_status, _countof(snapshot.music_status), buf);
            }
        } else {
            end_of_track_ticks_ = 0;
        }
        snapshot.music_position = new_pos;
    } else {
        end_of_track_ticks_ = 0;
    }

    // ─── Schedule next fetch ───────────────────────────────────────────
    if (now_ms < circuit_open_until_ms_) {
        return;
    }
    if (circuit_open_until_ms_ != 0 && now_ms >= circuit_open_until_ms_) {
        // Cooldown elapsed.
        circuit_open_until_ms_ = 0;
        durations_ms_.clear();
    }
    if (in_flight_) {
        return;
    }
    if (last_scheduled_ms_ != 0 && now_ms - last_scheduled_ms_ < RefreshIntervalMs()) {
        return;
    }
    RequestFetch(now_ms);
}

void MusicPoller::ForceRefresh() {
    durations_ms_.clear();
    circuit_open_until_ms_ = 0;
    timed_out_ = false;
    in_flight_ = false;          // discard in-flight (worker may still finish)
    last_scheduled_ms_ = 0;
    RequestFetch(GetTickCount64());
}

void MusicPoller::RequestFetch(std::uint64_t now_ms) {
    in_flight_request_id_ = next_request_id_++;
    in_flight_ = true;
    in_flight_started_ms_ = now_ms;
    last_scheduled_ms_ = now_ms;
    pending_request_id_.store(in_flight_request_id_);
    cv_.notify_one();
}

void MusicPoller::MaybeOpenCircuit(std::uint64_t now_ms) {
    if (durations_ms_.size() < static_cast<std::size_t>(kSlowThreshold)) {
        return;
    }
    int slow = 0;
    for (auto d : durations_ms_) {
        if (d > kSlowQueryMs) ++slow;
    }
    if (slow >= kSlowThreshold) {
        circuit_open_until_ms_ = now_ms + kCircuitOpenMs;
        durations_ms_.clear();
    }
}

std::uint64_t MusicPoller::RefreshIntervalMs() const {
    if (circuit_open_until_ms_ != 0) {
        return 60'000;
    }
    if (timed_out_) {
        return 30'000;
    }
    if (current_status_ == L"Playing") {
        return 15'000;
    }
    return 5'000;
}

void MusicPoller::ApplyResult(WidgetSnapshot& snapshot, const Result& r) {
    current_status_ = r.status;
    end_of_track_ticks_ = 0;

    snapshot.music_position = r.position;
    snapshot.music_duration = r.duration;
    snapshot.music_playing = (r.status == L"Playing");

    const std::wstring title = CleanMeta(r.title);
    const std::wstring artist = CleanMeta(r.artist);

    if (!r.has_session) {
        CopyW(snapshot.music_title, _countof(snapshot.music_title), L"No music");
        CopyW(snapshot.music_artist, _countof(snapshot.music_artist), L"No active session");
    } else {
        CopyW(snapshot.music_title, _countof(snapshot.music_title),
              title.empty() ? L"Unknown track" : title.c_str());
        CopyW(snapshot.music_artist, _countof(snapshot.music_artist),
              artist.empty() ? L"" : artist.c_str());
    }

    wchar_t status_text[64];
    swprintf_s(status_text, L"%s %s", StatusIcon(r.status), r.status.c_str());
    CopyW(snapshot.music_status, _countof(snapshot.music_status), status_text);
}

std::wstring MusicPoller::CleanMeta(const std::wstring& v) {
    if (v.empty()) {
        return {};
    }
    // Trim leading/trailing ASCII whitespace.
    std::size_t start = 0;
    while (start < v.size() &&
           (v[start] == L' ' || v[start] == L'\t' || v[start] == L'\r' || v[start] == L'\n')) {
        ++start;
    }
    std::size_t end = v.size();
    while (end > start &&
           (v[end - 1] == L' ' || v[end - 1] == L'\t' || v[end - 1] == L'\r' || v[end - 1] == L'\n')) {
        --end;
    }
    if (start == end) {
        return {};
    }
    std::wstring trimmed = v.substr(start, end - start);
    // Case-insensitive compare against "unknown".
    std::wstring lower = trimmed;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (lower == L"unknown") {
        return {};
    }
    return trimmed;
}

const wchar_t* MusicPoller::StatusIcon(const std::wstring& status) {
    // \u escapes are encoding-independent: MSVC parses these regardless of
    // whether the source file is saved as UTF-8 / UTF-8-BOM / ANSI / etc.
    // Without this, a literal "▶" in a no-BOM UTF-8 file gets mojibake'd
    // into "â–¶" by the default ANSI source decoder.
    if (status == L"Playing") return L"▶";  // ▶ BLACK RIGHT-POINTING TRIANGLE
    if (status == L"Paused")  return L"⏸";  // ⏸ DOUBLE VERTICAL BAR
    return L"♪";                            // ♪ EIGHTH NOTE
}
