#include "widget_app.h"

#include "music_poller.h"
#include "process_poller.h"
#include "process_sampler.h"
#include "settings_dialog.h"
#include "temperature_poller.h"
#include "weather_fetcher.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <audioclient.h>
#include <gdiplus.h>
#include <iphlpapi.h>
#include <mmdeviceapi.h>
#include <netioapi.h>
#include <string>
#include <wbemidl.h>
#include <winhttp.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

namespace {
constexpr wchar_t kWindowClass[] = L"SysmonWidgetNativeWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kClockTimer = 1;
// kMusicTimer was removed — MusicPoller drives its own scheduling and is
// ticked from kClockTimer at 1 Hz.
constexpr UINT_PTR kSystemTimer = 3;
constexpr UINT_PTR kWeatherTimer = 4;
constexpr UINT_PTR kAudioTimer = 5;
constexpr UINT kMenuShow      = 1001;
constexpr UINT kMenuHide      = 1002;
constexpr UINT kMenuSettings  = 1003;
constexpr UINT kMenuAutostart = 1004;
constexpr UINT kMenuRestart   = 1005;
constexpr UINT kMenuExit      = 1006;

// HKCU Run value name for autostart — same string Python build uses, so
// toggling either binary affects the same registry entry.
constexpr wchar_t kAutostartName[] = L"SysmonWidget";
constexpr wchar_t kAutostartKey[]  =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// Query system DPI without forcing a Win10 SDK target.
UINT QuerySystemDpi() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using PFN = UINT (WINAPI*)();
        auto pfn = reinterpret_cast<PFN>(GetProcAddress(user32, "GetDpiForSystem"));
        if (pfn) return pfn();
    }
    HDC hdc = GetDC(nullptr);
    UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 96u;
    if (hdc) ReleaseDC(nullptr, hdc);
    return dpi ? dpi : 96u;
}

RECT DefaultWidgetRect(const PositionSettings& pos) {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    // App is DPI-aware, so SPI_GETWORKAREA reports physical pixels. Scale
    // the design-unit dimensions by the system DPI so the widget looks the
    // same size on a 100% and a 150% display.
    const float scale = static_cast<float>(QuerySystemDpi()) / 96.0f;
    const int width  = static_cast<int>(380.0f * scale);
    // 626 + gap(10) + uptime-card(56) = 692.
    const int height = static_cast<int>(692.0f * scale);
    const int x_off  = static_cast<int>(pos.x * scale);
    const int y_off  = static_cast<int>(pos.y * scale);

    int left;
    if (pos.anchor == L"left") {
        left = work.left + x_off;
    } else {
        // Default: right-anchored (matches Python build).
        left = work.right - x_off - width;
    }
    const int top = work.top + y_off;
    return RECT{left, top, left + width, top + height};
}

// Indonesian day / month names. Hard-coded so we don't depend on the
// system C-locale being set to id-ID.
const wchar_t* IndoWeekday(int tm_wday) {
    static const wchar_t* const names[7] = {
        L"Minggu", L"Senin", L"Selasa", L"Rabu", L"Kamis", L"Jumat", L"Sabtu",
    };
    return (tm_wday >= 0 && tm_wday <= 6) ? names[tm_wday] : L"";
}

const wchar_t* IndoMonthLong(int tm_mon) {
    static const wchar_t* const names[12] = {
        L"Januari", L"Februari", L"Maret", L"April",  L"Mei",     L"Juni",
        L"Juli",    L"Agustus",  L"September", L"Oktober", L"November", L"Desember",
    };
    return (tm_mon >= 0 && tm_mon <= 11) ? names[tm_mon] : L"";
}

const wchar_t* IndoMonthShort(int tm_mon) {
    static const wchar_t* const names[12] = {
        L"Jan", L"Feb", L"Mar", L"Apr", L"Mei", L"Jun",
        L"Jul", L"Agu", L"Sep", L"Okt", L"Nov", L"Des",
    };
    return (tm_mon >= 0 && tm_mon <= 11) ? names[tm_mon] : L"";
}

// Map OpenWeatherMap "main" category to a single Unicode glyph rendered by
// Segoe UI's symbol fallback chain. Keep this in one place so the renderer
// only ever reads snapshot.weather_icon.
const wchar_t* WeatherMainToIcon(const std::wstring& main) {
    if (main == L"Clear")        return L"☀";
    if (main == L"Clouds")       return L"☁";
    if (main == L"Rain")         return L"☂";
    if (main == L"Drizzle")      return L"☂";
    if (main == L"Thunderstorm") return L"⚡";
    if (main == L"Snow")         return L"❄";
    return L"☁";  // Mist/Fog/Haze/Smoke/Dust/etc → generic cloud
}

void CopyText(wchar_t* destination, size_t size, const wchar_t* source) {
    wcsncpy_s(destination, size, source ? source : L"", _TRUNCATE);
}

template <typename T>
void SafeRelease(T** value) {
    if (*value) {
        (*value)->Release();
        *value = nullptr;
    }
}

float ClampRatio(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), size, nullptr, nullptr);
    return output;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring output(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, output.data(), size);
    return output;
}

std::string UrlEncode(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            output.push_back(static_cast<char>(ch));
        } else {
            output.push_back('%');
            output.push_back(hex[ch >> 4]);
            output.push_back(hex[ch & 0x0F]);
        }
    }
    return output;
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string ExtractObject(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    size_t search_from = 0;
    size_t pos = std::string::npos;
    while ((pos = json.find(marker, search_from)) != std::string::npos) {
        size_t colon = json.find(':', pos + marker.size());
        if (colon == std::string::npos) {
            return {};
        }

        size_t value = colon + 1;
        while (value < json.size() && isspace(static_cast<unsigned char>(json[value]))) {
            ++value;
        }

        if (value < json.size() && json[value] == '{') {
            pos = value;
            break;
        }

        search_from = colon + 1;
    }
    if (pos == std::string::npos) {
        return {};
    }

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = pos; i < json.size(); ++i) {
        const char ch = json[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
        } else if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(pos, i - pos + 1);
            }
        }
    }
    return {};
}

std::string ExtractJsonString(const std::string& json, const std::string& key, const std::string& fallback = {}) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find('"', pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    ++pos;

    std::string output;
    bool escaped = false;
    for (size_t i = pos; i < json.size(); ++i) {
        const char ch = json[i];
        if (escaped) {
            output.push_back(ch);
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            return output;
        } else {
            output.push_back(ch);
        }
    }
    return fallback;
}

int ExtractJsonInt(const std::string& json, const std::string& key, int fallback = 0) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    ++pos;
    while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    try {
        return std::stoi(json.substr(pos));
    } catch (...) {
        return fallback;
    }
}

bool ExtractJsonBool(const std::string& json, const std::string& key, bool fallback) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    ++pos;
    while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (json.compare(pos, 4, "true") == 0) {
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        return false;
    }
    return fallback;
}

double ExtractJsonDouble(const std::string& json, const std::string& key, double fallback = 0.0) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    ++pos;
    while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    try {
        return std::stod(json.substr(pos));
    } catch (...) {
        return fallback;
    }
}

std::wstring GetEnvironmentString(const wchar_t* name) {
    wchar_t buffer[4096]{};
    const DWORD length = GetEnvironmentVariableW(name, buffer, _countof(buffer));
    if (length == 0 || length >= _countof(buffer)) {
        return {};
    }
    return buffer;
}

std::wstring TitleCaseAscii(std::wstring value) {
    bool make_upper = true;
    for (wchar_t& ch : value) {
        if (ch == L' ' || ch == L'-') {
            make_upper = true;
        } else if (make_upper && ch >= L'a' && ch <= L'z') {
            ch = ch - L'a' + L'A';
            make_upper = false;
        } else {
            make_upper = false;
        }
    }
    return value;
}
} // namespace

class AudioMeter {
public:
    ~AudioMeter() {
        Shutdown();
    }

    bool Initialize() {
        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          __uuidof(IMMDeviceEnumerator),
                                          reinterpret_cast<void**>(&enumerator));
        if (FAILED(result)) {
            return false;
        }

        result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
        enumerator->Release();
        if (FAILED(result)) {
            return false;
        }

        result = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(&audio_client_));
        if (FAILED(result)) {
            return false;
        }

        result = audio_client_->GetMixFormat(&format_);
        if (FAILED(result)) {
            return false;
        }

        REFERENCE_TIME buffer_duration = 1000000; // 100 ms in 100-ns units.
        result = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                           AUDCLNT_STREAMFLAGS_LOOPBACK,
                                           buffer_duration, 0, format_, nullptr);
        if (FAILED(result)) {
            return false;
        }

        result = audio_client_->GetService(__uuidof(IAudioCaptureClient),
                                           reinterpret_cast<void**>(&capture_client_));
        if (FAILED(result)) {
            return false;
        }

        result = audio_client_->Start();
        running_ = SUCCEEDED(result);
        return running_;
    }

    void Update(WidgetSnapshot& snapshot) {
        if (!running_ && !Initialize()) {
            Decay(snapshot, 0.88f);
            return;
        }

        float level = 0.0f;
        bool has_audio = false;
        UINT32 packet_length = 0;
        while (capture_client_ && SUCCEEDED(capture_client_->GetNextPacketSize(&packet_length)) &&
               packet_length > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture_client_->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && frames > 0) {
                level = std::max(level, ComputeLevel(data, frames));
                has_audio = true;
            }
            capture_client_->ReleaseBuffer(frames);
        }

        if (!has_audio) {
            Decay(snapshot, 0.62f);
            return;
        }

        // Aggressive gain: typical music RMS lands near max so the height
        // budget is "tall by default", and per-bar randomness creates the
        // variance instead of the global level.
        level = ClampRatio(std::sqrt(level) * 6.5f);

        for (int i = 0; i < WidgetSnapshot::VisualizerBars; ++i) {
            // Pure per-bar randomness each frame — no sinusoidal shaping
            // (which read as "ombak" / wave-like). pow(r, 0.4) skews the
            // uniform distribution strongly toward 1.0, so the typical
            // bar lands near the top with frequent dips, not the other
            // way around. That's the "tinggi naiknya" feel.
            const float r = static_cast<float>(rand()) /
                            static_cast<float>(RAND_MAX);
            const float shape = std::pow(r, 0.4f);  // 0..1, biased high
            const float target = ClampRatio(level * shape);
            // Very snappy attack: peaks pop into place. Moderate release
            // so bars don't flicker.
            const float alpha =
                (target > snapshot.music_levels[i]) ? 0.92f : 0.28f;
            snapshot.music_levels[i] =
                snapshot.music_levels[i] * (1.0f - alpha) + target * alpha;
        }
    }

private:
    void Shutdown() {
        if (audio_client_ && running_) {
            audio_client_->Stop();
        }
        running_ = false;
        SafeRelease(&capture_client_);
        if (format_) {
            CoTaskMemFree(format_);
            format_ = nullptr;
        }
        SafeRelease(&audio_client_);
        SafeRelease(&device_);
    }

    void Decay(WidgetSnapshot& snapshot, float factor) {
        for (float& level : snapshot.music_levels) {
            level *= factor;
            if (level < 0.03f) {
                level = 0.03f;
            }
        }
    }

    float ComputeLevel(const BYTE* data, UINT32 frames) const {
        const UINT32 channels = std::max<UINT16>(1, format_->nChannels);
        const UINT32 sample_count = frames * channels;
        double sum = 0.0;

        if (format_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
            (format_->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
             reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format_)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            const auto* samples = reinterpret_cast<const float*>(data);
            for (UINT32 i = 0; i < sample_count; ++i) {
                const double value = samples[i];
                sum += value * value;
            }
        } else if (format_->wBitsPerSample == 16) {
            const auto* samples = reinterpret_cast<const std::int16_t*>(data);
            for (UINT32 i = 0; i < sample_count; ++i) {
                const double value = static_cast<double>(samples[i]) / 32768.0;
                sum += value * value;
            }
        } else if (format_->wBitsPerSample == 32) {
            const auto* samples = reinterpret_cast<const std::int32_t*>(data);
            for (UINT32 i = 0; i < sample_count; ++i) {
                const double value = static_cast<double>(samples[i]) / 2147483648.0;
                sum += value * value;
            }
        } else {
            return 0.0f;
        }

        return sample_count > 0 ? static_cast<float>(sum / sample_count) : 0.0f;
    }

    IMMDevice* device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioCaptureClient* capture_client_ = nullptr;
    WAVEFORMATEX* format_ = nullptr;
    bool running_ = false;
};

WidgetApp::WidgetApp(HINSTANCE instance)
    : instance_(instance),
      weather_settings_(LoadWeatherSettings()),
      position_settings_(LoadPositionSettings()) {}

WidgetApp::~WidgetApp() {
    RemoveTrayIcon();
    if (icon_) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
}

bool WidgetApp::Initialize() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;
    wc.lpfnWndProc = WidgetApp::WindowProcSetup;

    RegisterClassExW(&wc);

    RECT rect = DefaultWidgetRect(position_settings_);
    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        kWindowClass, L"Sysmon Widget", WS_POPUP,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance_, this);

    if (!hwnd_) {
        return false;
    }

    if (!renderer_.Initialize(hwnd_)) {
        return false;
    }

    // Seed rand() once so the visualizer doesn't replay an identical
    // sequence every launch. (Sub-second granularity is enough for visual
    // chaos — cryptographic randomness is overkill for bar heights.)
    srand(static_cast<unsigned int>(GetTickCount64()));

    // Spin up background pollers — these all run on their own threads so
    // the UI never stalls on WMI / subprocess / WinHTTP / Toolhelp work.
    temperature_poller_ = std::make_unique<TemperaturePoller>();
    process_poller_     = std::make_unique<ProcessPoller>(2);
    weather_fetcher_    = std::make_unique<WeatherFetcher>(weather_settings_);
    music_poller_       = std::make_unique<MusicPoller>();

    UpdateSnapshot();
    UpdateSystemSnapshot();
    UpdateNetworkSnapshot();
    UpdateStorageSnapshot();
    UpdateWeatherSnapshot();
    UpdateProcessSnapshot();
    music_poller_->Tick(snapshot_, GetTickCount64());
    audio_meter_ = std::make_unique<AudioMeter>();
    audio_meter_->Initialize();
    UpdateAudioSnapshot();
    AddTrayIcon();
    SetTimer(hwnd_, kClockTimer, 1000, nullptr);
    SetTimer(hwnd_, kSystemTimer, 1500, nullptr);
    // kWeatherTimer is gone — WeatherFetcher schedules its own refresh
    // internally on its worker thread. kSystemTimer (1.5 s) polls the
    // fetcher's cached result via UpdateWeatherSnapshot.
    // 90 ms (~11 fps) — fast enough for snappy bar movement, still cheap.
    SetTimer(hwnd_, kAudioTimer, 90, nullptr);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
    return true;
}

int WidgetApp::Run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WidgetApp::WindowProcSetup(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* app = static_cast<WidgetApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WidgetApp::WindowProcThunk));
        return app->HandleMessage(hwnd, message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK WidgetApp::WindowProcThunk(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<WidgetApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return app->HandleMessage(hwnd, message, wparam, lparam);
}

LRESULT WidgetApp::HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_SIZE:
        renderer_.Resize(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_DPICHANGED: {
        // System or per-monitor DPI changed. lParam is a RECT* with the
        // suggested new window bounds for the target DPI — apply it as-is
        // so window chrome and content scale correctly. Reset the renderer
        // so its cached SetDpi picks up the new value on next paint.
        RECT* suggested = reinterpret_cast<RECT*>(lparam);
        if (suggested) {
            SetWindowPos(hwnd_, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
        renderer_.Reset();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (wparam == kClockTimer) {
            // Capture previous visible state before mutating snapshot, so we
            // can decide whether anything the user sees actually changed.
            wchar_t prev_time[16];
            wcscpy_s(prev_time, snapshot_.time);
            wchar_t prev_uptime[32];
            wcscpy_s(prev_uptime, snapshot_.uptime_text);
            const bool prev_playing = snapshot_.music_playing;
            const double prev_pos = snapshot_.music_position;

            UpdateSnapshot();
            if (music_poller_) {
                music_poller_->Tick(snapshot_, GetTickCount64());
            }

            const bool playing_now = snapshot_.music_playing;
            // Switch audio-frame rate based on playback so a silent widget
            // doesn't run the visualizer at 11 fps.
            const UINT desired = playing_now ? 90 : 250;
            if (desired != audio_interval_ms_) {
                audio_interval_ms_ = desired;
                SetTimer(hwnd_, kAudioTimer, desired, nullptr);
            }

            // Only invalidate when at least one visible field changed:
            //  - the minute portion of the clock (we don't render seconds)
            //  - uptime (changes per minute too)
            //  - music is playing (position counter advances every second)
            //  - end-of-track transition (playing→stopped)
            const bool minute_changed = (wcscmp(prev_time, snapshot_.time) != 0);
            const bool uptime_changed = (wcscmp(prev_uptime, snapshot_.uptime_text) != 0);
            const bool music_changed  = (playing_now != prev_playing) ||
                                        (playing_now && prev_pos != snapshot_.music_position);
            if (minute_changed || uptime_changed || music_changed) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } else if (wparam == kSystemTimer) {
            UpdateSystemSnapshot();
            UpdateNetworkSnapshot();
            UpdateStorageSnapshot();
            // Process / weather snapshots are just memcpys from background
            // pollers — the actual scans/fetches run on worker threads, so
            // polling them here every 1.5 s is cheap and gets fresh data
            // onto the panel without waiting for kWeatherTimer (600 s).
            UpdateProcessSnapshot();
            UpdateWeatherSnapshot();
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wparam == kAudioTimer) {
            UpdateAudioSnapshot();
            // Only repaint if visualizer bar heights actually moved enough
            // to be visible (threshold inside UpdateAudioSnapshot). Saves
            // ~10 wasted full-window UpdateLayeredWindow per second when
            // music is paused/stopped.
            if (audio_dirty_) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_CONTEXTMENU:
        ShowTrayMenu();
        return 0;
    case kTrayMessage:
        if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
            ShowTrayMenu();
        } else if (LOWORD(lparam) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd_, IsWindowVisible(hwnd_) ? SW_HIDE : SW_SHOWNOACTIVATE);
        }
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wparam));
        return 0;
    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

void WidgetApp::OnPaint() {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);
    renderer_.Render(snapshot_);
    EndPaint(hwnd_, &ps);
}

void WidgetApp::OnCommand(UINT command) {
    switch (command) {
    case kMenuShow:
        if (!visible_) {
            visible_ = true;
            // Resume the foreground timers. WeatherFetcher's own worker
            // thread keeps running regardless of visibility, so weather
            // data stays fresh in the background.
            SetTimer(hwnd_, kClockTimer, 1000, nullptr);
            SetTimer(hwnd_, kSystemTimer, 1500, nullptr);
            SetTimer(hwnd_, kAudioTimer, 90, nullptr);
        }
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        break;
    case kMenuHide:
        if (visible_) {
            visible_ = false;
            // Kill foreground timers so a hidden widget consumes ~0% CPU.
            // Background worker threads (music/process/temp/weather) keep
            // running so data is fresh when the widget is unhidden.
            KillTimer(hwnd_, kClockTimer);
            KillTimer(hwnd_, kSystemTimer);
            KillTimer(hwnd_, kAudioTimer);
        }
        ShowWindow(hwnd_, SW_HIDE);
        break;
    case kMenuSettings:
        OpenSettings();
        break;
    case kMenuAutostart:
        SetAutostartEnabled(!IsAutostartEnabled());
        break;
    case kMenuRestart:
        RestartApp();
        break;
    case kMenuExit:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

void WidgetApp::UpdateSnapshot() {
    std::time_t raw = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &raw);
    wcsftime(snapshot_.time, _countof(snapshot_.time), L"%H:%M", &local);
    // Day name + date with Indonesian month, hard-coded so we don't need
    // the system C locale set to id-ID.
    swprintf_s(snapshot_.weekday, L"%s", IndoWeekday(local.tm_wday));
    swprintf_s(snapshot_.date_long, L"%d %s %d",
               local.tm_mday, IndoMonthLong(local.tm_mon), local.tm_year + 1900);

    // ── Uptime + boot time (strip card at the bottom) ──────────────────
    const ULONGLONG up_ms = GetTickCount64();
    const ULONGLONG up_sec = up_ms / 1000ULL;
    const ULONGLONG days  = up_sec / 86400ULL;
    const ULONGLONG hours = (up_sec % 86400ULL) / 3600ULL;
    const ULONGLONG mins  = (up_sec % 3600ULL) / 60ULL;
    if (days > 0) {
        // For >1 day, use d/h/m form (more readable than total hours).
        swprintf_s(snapshot_.uptime_text, L"%llud %lluh %llum", days, hours, mins);
    } else if (hours > 0) {
        swprintf_s(snapshot_.uptime_text, L"%lluh %llum", hours, mins);
    } else {
        swprintf_s(snapshot_.uptime_text, L"%llum", mins);
    }

    // Boot timestamp = now - uptime, derived via FILETIME arithmetic.
    SYSTEMTIME now_st{};
    GetLocalTime(&now_st);
    FILETIME now_ft{};
    SystemTimeToFileTime(&now_st, &now_ft);
    ULARGE_INTEGER now_ui{};
    now_ui.LowPart  = now_ft.dwLowDateTime;
    now_ui.HighPart = now_ft.dwHighDateTime;
    ULARGE_INTEGER boot_ui{};
    // FILETIME unit is 100 ns, so multiply ms by 10 000.
    boot_ui.QuadPart = now_ui.QuadPart - up_ms * 10000ULL;
    FILETIME boot_ft{};
    boot_ft.dwLowDateTime  = boot_ui.LowPart;
    boot_ft.dwHighDateTime = boot_ui.HighPart;
    SYSTEMTIME boot_st{};
    if (FileTimeToSystemTime(&boot_ft, &boot_st)) {
        // "26 Mei  08:10" — Indonesian short month, 24h time.
        swprintf_s(snapshot_.boot_text, L"%d %s  %02d:%02d",
                   boot_st.wDay, IndoMonthShort(boot_st.wMonth - 1),
                   boot_st.wHour, boot_st.wMinute);
    }
}

void WidgetApp::UpdateSystemSnapshot() {
    FILETIME idle{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        if (has_previous_cpu_sample_) {
            const unsigned long long idle_delta =
                FileTimeToUint64(idle) - FileTimeToUint64(previous_idle_);
            const unsigned long long kernel_delta =
                FileTimeToUint64(kernel) - FileTimeToUint64(previous_kernel_);
            const unsigned long long user_delta =
                FileTimeToUint64(user) - FileTimeToUint64(previous_user_);
            const unsigned long long total_delta = kernel_delta + user_delta;

            if (total_delta > 0) {
                const float cpu = 1.0f - static_cast<float>(idle_delta) / static_cast<float>(total_delta);
                snapshot_.cpu_usage = ClampRatio(cpu);
                swprintf_s(snapshot_.cpu_text, L"%.0f%%", snapshot_.cpu_usage * 100.0f);
            }
        }

        previous_idle_ = idle;
        previous_kernel_ = kernel;
        previous_user_ = user;
        has_previous_cpu_sample_ = true;
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        snapshot_.ram_usage = ClampRatio(static_cast<float>(memory.dwMemoryLoad) / 100.0f);
        swprintf_s(snapshot_.ram_text, L"%lu%%", memory.dwMemoryLoad);
    }

    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) && power.BatteryLifePercent != 255) {
        snapshot_.battery_usage = ClampRatio(static_cast<float>(power.BatteryLifePercent) / 100.0f);
        swprintf_s(snapshot_.battery_text, L"%u%%", power.BatteryLifePercent);
    } else {
        snapshot_.battery_usage = 0.0f;
        CopyText(snapshot_.battery_text, _countof(snapshot_.battery_text), L"N/A");
    }

    // Temperature is published asynchronously by TemperaturePoller — we
    // just read whatever the worker last cached. Never blocks the UI.
    float temperature = 0.0f;
    if (temperature_poller_ && temperature_poller_->TryGet(&temperature)) {
        snapshot_.temperature_usage = ClampRatio(temperature / 100.0f);
        swprintf_s(snapshot_.temperature_text, L"%.0fC", temperature);
    } else {
        snapshot_.temperature_usage = 0.0f;
        CopyText(snapshot_.temperature_text, _countof(snapshot_.temperature_text), L"N/A");
    }
}

void WidgetApp::UpdateNetworkSnapshot() {
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) {
        CopyText(snapshot_.network_down_text, _countof(snapshot_.network_down_text), L"N/A");
        CopyText(snapshot_.network_up_text, _countof(snapshot_.network_up_text), L"N/A");
        return;
    }

    MIB_IF_ROW2* selected = nullptr;
    unsigned long long best_total = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        MIB_IF_ROW2* row = &table->Table[i];
        if (row->OperStatus != IfOperStatusUp || row->InterfaceAndOperStatusFlags.HardwareInterface == FALSE ||
            row->Type == IF_TYPE_SOFTWARE_LOOPBACK || row->Type == IF_TYPE_TUNNEL) {
            continue;
        }

        const unsigned long long total = row->InOctets + row->OutOctets;
        if (!selected || total > best_total) {
            selected = row;
            best_total = total;
        }
    }

    if (!selected) {
        FreeMibTable(table);
        CopyText(snapshot_.network_down_text, _countof(snapshot_.network_down_text), L"0.0 KB/s");
        CopyText(snapshot_.network_up_text, _countof(snapshot_.network_up_text), L"0.0 KB/s");
        has_previous_network_sample_ = false;
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const bool same_interface =
        has_previous_network_sample_ &&
        selected->InterfaceLuid.Value == previous_network_luid_ &&
        now > previous_network_tick_;

    if (same_interface) {
        const double seconds = static_cast<double>(now - previous_network_tick_) / 1000.0;
        const unsigned long long in_delta =
            selected->InOctets >= previous_network_in_ ? selected->InOctets - previous_network_in_ : 0;
        const unsigned long long out_delta =
            selected->OutOctets >= previous_network_out_ ? selected->OutOctets - previous_network_out_ : 0;
        const auto down_per_second = static_cast<unsigned long long>(static_cast<double>(in_delta) / seconds);
        const auto up_per_second = static_cast<unsigned long long>(static_cast<double>(out_delta) / seconds);

        FormatThroughput(down_per_second, snapshot_.network_down_text,
                         _countof(snapshot_.network_down_text), false);
        FormatThroughput(up_per_second, snapshot_.network_up_text,
                         _countof(snapshot_.network_up_text), false);
    } else {
        CopyText(snapshot_.network_down_text, _countof(snapshot_.network_down_text), L"0.0 KB/s");
        CopyText(snapshot_.network_up_text, _countof(snapshot_.network_up_text), L"0.0 KB/s");
    }

    previous_network_luid_ = selected->InterfaceLuid.Value;
    previous_network_in_ = selected->InOctets;
    previous_network_out_ = selected->OutOctets;
    previous_network_tick_ = now;
    has_previous_network_sample_ = true;
    FreeMibTable(table);
}

void WidgetApp::UpdateStorageSnapshot() {
    wchar_t drives[256]{};
    const DWORD length = GetLogicalDriveStringsW(_countof(drives), drives);
    if (length == 0 || length >= _countof(drives)) {
        snapshot_.storage_count = 1;
        CopyText(snapshot_.storage_label[0], _countof(snapshot_.storage_label[0]), L"--");
        CopyText(snapshot_.storage_percent_text[0], _countof(snapshot_.storage_percent_text[0]), L"--");
        CopyText(snapshot_.storage_detail[0], _countof(snapshot_.storage_detail[0]), L"Storage unavailable");
        snapshot_.storage_usage[0] = 0.0f;
        return;
    }

    int count = 0;
    for (wchar_t* drive = drives; *drive; drive += wcslen(drive) + 1) {
        if (count >= 2) {
            break;
        }
        if (GetDriveTypeW(drive) != DRIVE_FIXED) {
            continue;
        }

        ULARGE_INTEGER free_to_caller{};
        ULARGE_INTEGER total{};
        ULARGE_INTEGER free_total{};
        if (!GetDiskFreeSpaceExW(drive, &free_to_caller, &total, &free_total) || total.QuadPart == 0) {
            continue;
        }

        const double used = static_cast<double>(total.QuadPart - free_total.QuadPart);
        const double usage = used / static_cast<double>(total.QuadPart);
        const double free_gib = static_cast<double>(free_total.QuadPart) / (1024.0 * 1024.0 * 1024.0);
        const double total_gib = static_cast<double>(total.QuadPart) / (1024.0 * 1024.0 * 1024.0);

        wchar_t label[16]{};
        wcsncpy_s(label, drive, _TRUNCATE);
        if (wcslen(label) > 2) {
            label[2] = L'\0';
        }

        CopyText(snapshot_.storage_label[count], _countof(snapshot_.storage_label[count]), label);
        snapshot_.storage_usage[count] = ClampRatio(static_cast<float>(usage));
        swprintf_s(snapshot_.storage_percent_text[count], L"%.0f%%", snapshot_.storage_usage[count] * 100.0f);
        swprintf_s(snapshot_.storage_detail[count], L"%.0f GB free / %.0f GB", free_gib, total_gib);
        ++count;
    }

    snapshot_.storage_count = count;
    for (int i = count; i < 2; ++i) {
        CopyText(snapshot_.storage_label[i], _countof(snapshot_.storage_label[i]), L"");
        CopyText(snapshot_.storage_percent_text[i], _countof(snapshot_.storage_percent_text[i]), L"");
        CopyText(snapshot_.storage_detail[i], _countof(snapshot_.storage_detail[i]), L"");
        snapshot_.storage_usage[i] = 0.0f;
    }

    if (count == 0) {
        snapshot_.storage_count = 1;
        CopyText(snapshot_.storage_label[0], _countof(snapshot_.storage_label[0]), L"--");
        CopyText(snapshot_.storage_percent_text[0], _countof(snapshot_.storage_percent_text[0]), L"--");
        CopyText(snapshot_.storage_detail[0], _countof(snapshot_.storage_detail[0]), L"No fixed drive");
    }
}

void WidgetApp::UpdateAudioSnapshot() {
    if (!audio_meter_) {
        audio_dirty_ = false;
        return;
    }
    // Snapshot levels before update so we can detect material change.
    // 1 % change per bar is the threshold below which the redraw isn't
    // perceptible at 18 bars × ~80 px tall.
    float prev[WidgetSnapshot::VisualizerBars];
    for (int i = 0; i < WidgetSnapshot::VisualizerBars; ++i) {
        prev[i] = snapshot_.music_levels[i];
    }
    audio_meter_->Update(snapshot_);

    audio_dirty_ = false;
    for (int i = 0; i < WidgetSnapshot::VisualizerBars; ++i) {
        if (std::fabs(prev[i] - snapshot_.music_levels[i]) > 0.01f) {
            audio_dirty_ = true;
            break;
        }
    }
}

void WidgetApp::UpdateWeatherSnapshot() {
    // WeatherFetcher runs on its own thread and decides when to refresh
    // based on settings.refresh_sec. We just copy out whatever it has
    // most recently published — never blocks on WinHTTP.
    if (!weather_fetcher_) return;
    WeatherFetcher::Result r;
    if (!weather_fetcher_->TryGet(&r)) return;
    CopyText(snapshot_.weather_temp,   _countof(snapshot_.weather_temp),   r.temp.c_str());
    CopyText(snapshot_.weather_city,   _countof(snapshot_.weather_city),   r.city.c_str());
    CopyText(snapshot_.weather_detail, _countof(snapshot_.weather_detail), r.detail.c_str());
    CopyText(snapshot_.weather_meta,   _countof(snapshot_.weather_meta),   r.meta.c_str());
    CopyText(snapshot_.weather_icon,   _countof(snapshot_.weather_icon),   r.icon.c_str());
}

void WidgetApp::UpdateProcessSnapshot() {
    if (!process_poller_) {
        return;
    }
    std::vector<ProcessSampler::Entry> entries;
    if (!process_poller_->TryGet(&entries)) {
        return;  // worker hasn't completed its first sample yet
    }
    snapshot_.top_process_count = static_cast<int>(entries.size());

    for (int i = 0; i < 2; ++i) {
        if (i < static_cast<int>(entries.size())) {
            // Trim ".exe" suffix for display brevity (keep if name is short
            // enough that trimming makes it ambiguous).
            std::wstring name = entries[i].name;
            if (name.size() > 4) {
                auto pos = name.size() - 4;
                if (name.compare(pos, 4, L".exe") == 0 ||
                    name.compare(pos, 4, L".EXE") == 0) {
                    name.resize(pos);
                }
            }
            // Truncate long names so they fit in a half-card column.
            constexpr size_t kMaxName = 16;
            if (name.size() > kMaxName) {
                name.resize(kMaxName - 1);
                name += L"…";
            }
            CopyText(snapshot_.top_process_name[i],
                     _countof(snapshot_.top_process_name[i]), name.c_str());

            // Format RAM as MB/GB.
            const double rss_mb = static_cast<double>(entries[i].working_set_bytes) /
                                  (1024.0 * 1024.0);
            wchar_t detail[64];
            if (rss_mb >= 1024.0) {
                swprintf_s(detail, L"%.0f%%  %.1f GB",
                           entries[i].cpu_percent, rss_mb / 1024.0);
            } else {
                swprintf_s(detail, L"%.0f%%  %.0f MB",
                           entries[i].cpu_percent, rss_mb);
            }
            CopyText(snapshot_.top_process_detail[i],
                     _countof(snapshot_.top_process_detail[i]), detail);
        } else {
            CopyText(snapshot_.top_process_name[i],
                     _countof(snapshot_.top_process_name[i]), L"--");
            CopyText(snapshot_.top_process_detail[i],
                     _countof(snapshot_.top_process_detail[i]), L"--");
        }
    }
}

void WidgetApp::FormatThroughput(unsigned long long bytes_per_second, wchar_t* output,
                                 size_t output_size, bool suffix_up) {
    const double kib = static_cast<double>(bytes_per_second) / 1024.0;
    const double mib = kib / 1024.0;

    if (mib >= 1.0) {
        swprintf_s(output, output_size, suffix_up ? L"%.1f MB/s up" : L"%.1f MB/s", mib);
    } else {
        swprintf_s(output, output_size, suffix_up ? L"%.1f KB/s up" : L"%.1f KB/s", kib);
    }
}

WeatherSettings WidgetApp::LoadWeatherSettings() {
    WeatherSettings settings;

    const std::wstring config_dir_override = GetEnvironmentString(L"SYSMON_WIDGET_CONFIG_DIR");
    std::filesystem::path config_path;
    if (!config_dir_override.empty()) {
        config_path = std::filesystem::path(config_dir_override) / L"config.json";
    } else {
        const std::wstring appdata = GetEnvironmentString(L"APPDATA");
        if (!appdata.empty()) {
            config_path = std::filesystem::path(appdata) / L"SysmonWidget" / L"config.json";
        }
    }

    const std::string settings_json = config_path.empty() ? std::string{} : ReadTextFile(config_path);
    const std::string weather_json = ExtractObject(settings_json, "weather");
    if (!weather_json.empty()) {
        settings.api_key = Utf8ToWide(ExtractJsonString(weather_json, "api_key", ""));
        settings.city = Utf8ToWide(ExtractJsonString(weather_json, "city", "Cileungsi"));
        settings.country_code = Utf8ToWide(ExtractJsonString(weather_json, "country_code", "ID"));
        settings.units = Utf8ToWide(ExtractJsonString(weather_json, "units", "metric"));
        settings.city_id = ExtractJsonInt(weather_json, "city_id", 0);
        settings.refresh_sec = ExtractJsonInt(weather_json, "refresh_sec", 600);
        settings.show_humidity = ExtractJsonBool(weather_json, "show_humidity", true);
        settings.show_wind = ExtractJsonBool(weather_json, "show_wind", true);
    }

    if (settings.api_key.empty()) {
        settings.api_key = GetEnvironmentString(L"OPENWEATHERMAP_API_KEY");
    }
    if (settings.api_key.empty()) {
        settings.api_key = GetEnvironmentString(L"SYSMON_WIDGET_WEATHER_API_KEY");
    }

    return settings;
}

PositionSettings WidgetApp::LoadPositionSettings() {
    PositionSettings p;

    const std::wstring config_dir_override = GetEnvironmentString(L"SYSMON_WIDGET_CONFIG_DIR");
    std::filesystem::path config_path;
    if (!config_dir_override.empty()) {
        config_path = std::filesystem::path(config_dir_override) / L"config.json";
    } else {
        const std::wstring appdata = GetEnvironmentString(L"APPDATA");
        if (!appdata.empty()) {
            config_path = std::filesystem::path(appdata) / L"SysmonWidget" / L"config.json";
        }
    }

    const std::string settings_json = config_path.empty() ? std::string{} : ReadTextFile(config_path);
    const std::string position_json = ExtractObject(settings_json, "position");
    if (!position_json.empty()) {
        const std::string anchor = ExtractJsonString(position_json, "anchor", "right");
        p.anchor = Utf8ToWide(anchor.empty() ? "right" : anchor);
        p.x = ExtractJsonInt(position_json, "x", 16);
        p.y = ExtractJsonInt(position_json, "y", 16);
    }
    // Normalize anchor — accept "Right"/"RIGHT" etc.
    std::transform(p.anchor.begin(), p.anchor.end(), p.anchor.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (p.anchor != L"left") p.anchor = L"right";

    return p;
}

unsigned long long WidgetApp::FileTimeToUint64(const FILETIME& value) {
    ULARGE_INTEGER result{};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

HICON WidgetApp::BuildAppIcon(int size) {
    using namespace Gdiplus;
    Bitmap bmp(size, size, PixelFormat32bppPARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0, 0, 0, 0));

    // Rounded card background — matches the widget's own card color
    // (#0E2422) with a soft cyan border so the icon reads at small sizes.
    const REAL pad = size * 0.06f;
    const REAL w = size - pad * 2.0f;
    const REAL h = size - pad * 2.0f;
    const REAL r = size * 0.22f;
    GraphicsPath path;
    path.AddArc(pad,              pad,              r, r, 180.0f, 90.0f);
    path.AddArc(pad + w - r,      pad,              r, r, 270.0f, 90.0f);
    path.AddArc(pad + w - r,      pad + h - r,      r, r, 0.0f,   90.0f);
    path.AddArc(pad,              pad + h - r,      r, r, 90.0f,  90.0f);
    path.CloseFigure();

    SolidBrush bg(Color(255, 14, 36, 34));   // #0E2422
    g.FillPath(&bg, &path);
    Pen border(Color(255, 64, 196, 255), size * 0.04f);  // sky-blue rim
    g.DrawPath(&border, &path);

    // Open ring — orange/amber, like a gauge dial.
    const REAL ring_pad = size * 0.24f;
    const REAL ring_size = size - ring_pad * 2.0f;
    Pen ring_pen(Color(255, 255, 152, 0), size * 0.10f);
    ring_pen.SetStartCap(LineCapRound);
    ring_pen.SetEndCap(LineCapRound);
    g.DrawArc(&ring_pen, ring_pad, ring_pad, ring_size, ring_size,
              -130.0f, 280.0f);

    // White center dot.
    SolidBrush dot(Color(255, 247, 235, 237));
    const REAL dot_pad = size * 0.40f;
    g.FillEllipse(&dot, dot_pad, dot_pad, size - dot_pad * 2.0f, size - dot_pad * 2.0f);

    HICON icon = nullptr;
    bmp.GetHICON(&icon);
    return icon;
}

void WidgetApp::AddTrayIcon() {
    // GDI+ is only used to render the tray icon glyph, so spin it up just
    // long enough to build the HICON and shut it down again. Bitmap::GetHICON
    // hands us a Win32 HICON that survives GDI+ teardown. Saves ~3 MB
    // working set vs initializing GDI+ for the whole process lifetime.
    {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
        icon_ = BuildAppIcon(32);
        Gdiplus::GdiplusShutdown(token);
    }

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = 1;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = icon_;
    wcscpy_s(data.szTip, L"Sysmon Widget");
    Shell_NotifyIconW(NIM_ADD, &data);
}

bool WidgetApp::IsAutostartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kAutostartKey, 0, KEY_READ, &key)
        != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    const LONG r = RegQueryValueExW(key, kAutostartName, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

void WidgetApp::SetAutostartEnabled(bool enabled) {
    if (enabled) {
        wchar_t exe[MAX_PATH] = L"";
        if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return;
        std::wstring quoted = L"\"";
        quoted += exe;
        quoted += L"\"";

        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kAutostartKey, 0, nullptr, 0,
                            KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(key, kAutostartName, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(quoted.c_str()),
                           static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(key);
        }
    } else {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kAutostartKey, 0, KEY_WRITE, &key)
            == ERROR_SUCCESS) {
            RegDeleteValueW(key, kAutostartName);
            RegCloseKey(key);
        }
    }
}

void WidgetApp::OpenSettings() {
    WeatherSettings ws_out;
    PositionSettings ps_out;
    if (!SettingsDialog::Show(hwnd_, weather_settings_, position_settings_,
                              &ws_out, &ps_out)) {
        return;
    }
    // Weather: push new settings to the background fetcher (also triggers
    // an immediate re-fetch with the new credentials/city).
    weather_settings_ = ws_out;
    if (weather_fetcher_) weather_fetcher_->UpdateSettings(weather_settings_);

    // Position: move window if it actually changed.
    const bool moved = (ps_out.anchor != position_settings_.anchor ||
                        ps_out.x != position_settings_.x ||
                        ps_out.y != position_settings_.y);
    position_settings_ = ps_out;
    if (moved) {
        RECT r = DefaultWidgetRect(position_settings_);
        SetWindowPos(hwnd_, nullptr, r.left, r.top,
                     r.right - r.left, r.bottom - r.top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
    }
}

void WidgetApp::RestartApp() {
    wchar_t exe[MAX_PATH] = L"";
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return;

    // Remove our tray icon explicitly first so the new instance's NIM_ADD
    // doesn't briefly coexist with ours (would show two stacked icons until
    // mouse hover triggers a refresh).
    RemoveTrayIcon();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe, nullptr, nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    DestroyWindow(hwnd_);
}

void WidgetApp::RemoveTrayIcon() {
    if (!hwnd_) {
        return;
    }

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void WidgetApp::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuShow, L"Show Widget");
    AppendMenuW(menu, MF_STRING, kMenuHide, L"Hide Widget");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings");
    SetMenuDefaultItem(menu, kMenuSettings, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const bool on = IsAutostartEnabled();
    AppendMenuW(menu, MF_STRING | (on ? MF_CHECKED : 0), kMenuAutostart,
                L"Start with Windows");
    AppendMenuW(menu, MF_STRING, kMenuRestart, L"Restart Widget");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   cursor.x, cursor.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}
