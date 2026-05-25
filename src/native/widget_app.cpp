#include "widget_app.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <audioclient.h>
#include <iphlpapi.h>
#include <mmdeviceapi.h>
#include <netioapi.h>
#include <string>
#include <wbemidl.h>
#include <winhttp.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/base.h>

namespace {
constexpr wchar_t kWindowClass[] = L"SysmonWidgetNativeWindow";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kClockTimer = 1;
constexpr UINT_PTR kMusicTimer = 2;
constexpr UINT_PTR kSystemTimer = 3;
constexpr UINT_PTR kWeatherTimer = 4;
constexpr UINT_PTR kAudioTimer = 5;
constexpr UINT kMenuShow = 1001;
constexpr UINT kMenuHide = 1002;
constexpr UINT kMenuExit = 1003;

RECT DefaultWidgetRect() {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    constexpr int width = 380;
    constexpr int height = 564;
    constexpr int margin = 18;
    return RECT{work.left + margin, work.top + margin, work.left + margin + width,
                work.top + margin + height};
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
            Decay(snapshot, 0.82f);
            return;
        }

        level = ClampRatio(std::sqrt(level) * 3.2f);
        for (int i = 0; i < WidgetSnapshot::VisualizerBars; ++i) {
            const float shape = 0.42f + 0.58f * static_cast<float>(
                std::sin(static_cast<double>(GetTickCount64()) * 0.007 + i * 0.91) * 0.5 + 0.5);
            const float target = ClampRatio(0.08f + level * shape);
            snapshot.music_levels[i] = snapshot.music_levels[i] * 0.42f + target * 0.58f;
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

WidgetApp::WidgetApp(HINSTANCE instance) : instance_(instance), weather_settings_(LoadWeatherSettings()) {}

WidgetApp::~WidgetApp() {
    RemoveTrayIcon();
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

    RECT rect = DefaultWidgetRect();
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

    UpdateSnapshot();
    UpdateSystemSnapshot();
    UpdateNetworkSnapshot();
    UpdateStorageSnapshot();
    UpdateWeatherSnapshot();
    UpdateMusicSnapshot();
    audio_meter_ = std::make_unique<AudioMeter>();
    audio_meter_->Initialize();
    UpdateAudioSnapshot();
    AddTrayIcon();
    SetTimer(hwnd_, kClockTimer, 1000, nullptr);
    SetTimer(hwnd_, kMusicTimer, 2000, nullptr);
    SetTimer(hwnd_, kSystemTimer, 1500, nullptr);
    SetTimer(hwnd_, kWeatherTimer, static_cast<UINT>(std::max(60, weather_settings_.refresh_sec)) * 1000, nullptr);
    SetTimer(hwnd_, kAudioTimer, 160, nullptr);
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
    case WM_TIMER:
        if (wparam == kClockTimer) {
            UpdateSnapshot();
            if (snapshot_.music_playing && snapshot_.music_duration > 0.0) {
                snapshot_.music_position = std::min(snapshot_.music_duration, snapshot_.music_position + 1.0);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wparam == kMusicTimer) {
            UpdateMusicSnapshot();
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wparam == kSystemTimer) {
            UpdateSystemSnapshot();
            UpdateNetworkSnapshot();
            UpdateStorageSnapshot();
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wparam == kWeatherTimer) {
            UpdateWeatherSnapshot();
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wparam == kAudioTimer) {
            UpdateAudioSnapshot();
            InvalidateRect(hwnd, nullptr, FALSE);
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
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        break;
    case kMenuHide:
        ShowWindow(hwnd_, SW_HIDE);
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
    wcsftime(snapshot_.date, _countof(snapshot_.date), L"%A, %d %B %Y", &local);
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

    const ULONGLONG now = GetTickCount64();
    if (!has_temperature_sample_ || now - last_temperature_tick_ > 10000) {
        last_temperature_tick_ = now;
        float temperature = 0.0f;
        if (ReadTemperatureCelsius(&temperature)) {
            snapshot_.temperature_usage = ClampRatio(temperature / 100.0f);
            swprintf_s(snapshot_.temperature_text, L"%.0fC", temperature);
            has_temperature_sample_ = true;
        } else {
            snapshot_.temperature_usage = 0.0f;
            CopyText(snapshot_.temperature_text, _countof(snapshot_.temperature_text), L"N/A");
            has_temperature_sample_ = true;
        }
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
    if (audio_meter_) {
        audio_meter_->Update(snapshot_);
    }
}

void WidgetApp::UpdateWeatherSnapshot() {
    const ULONGLONG now = GetTickCount64();
    if (last_weather_tick_ != 0 &&
        now - last_weather_tick_ < static_cast<ULONGLONG>(std::max(60, weather_settings_.refresh_sec)) * 1000) {
        return;
    }

    last_weather_tick_ = now;
    if (weather_settings_.api_key.empty() ||
        weather_settings_.api_key == L"YOUR_OPENWEATHERMAP_API_KEY") {
        CopyText(snapshot_.weather_temp, _countof(snapshot_.weather_temp), L"-- C");
        CopyText(snapshot_.weather_city, _countof(snapshot_.weather_city), weather_settings_.city.c_str());
        CopyText(snapshot_.weather_detail, _countof(snapshot_.weather_detail), L"API key missing");
        CopyText(snapshot_.weather_meta, _countof(snapshot_.weather_meta), L"");
        return;
    }

    if (!FetchWeather(weather_settings_, &snapshot_)) {
        CopyText(snapshot_.weather_temp, _countof(snapshot_.weather_temp), L"-- C");
        CopyText(snapshot_.weather_city, _countof(snapshot_.weather_city), weather_settings_.city.c_str());
        CopyText(snapshot_.weather_detail, _countof(snapshot_.weather_detail), L"Weather unavailable");
        CopyText(snapshot_.weather_meta, _countof(snapshot_.weather_meta), L"");
    }
}

void WidgetApp::UpdateMusicSnapshot() {
    namespace media = winrt::Windows::Media::Control;

    try {
        auto manager = media::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        auto session = manager.GetCurrentSession();
        if (!session) {
            CopyText(snapshot_.music_title, _countof(snapshot_.music_title), L"Stopped");
            CopyText(snapshot_.music_artist, _countof(snapshot_.music_artist), L"No active session");
            CopyText(snapshot_.music_status, _countof(snapshot_.music_status), L"SMTC idle");
            snapshot_.music_position = 0.0;
            snapshot_.music_duration = 0.0;
            snapshot_.music_playing = false;
            return;
        }

        auto playback = session.GetPlaybackInfo();
        switch (playback.PlaybackStatus()) {
        case media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
            CopyText(snapshot_.music_status, _countof(snapshot_.music_status), L"Playing");
            break;
        case media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
            CopyText(snapshot_.music_status, _countof(snapshot_.music_status), L"Paused");
            break;
        case media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
            CopyText(snapshot_.music_status, _countof(snapshot_.music_status), L"Stopped");
            break;
        default:
            CopyText(snapshot_.music_status, _countof(snapshot_.music_status), L"Unknown");
            break;
        }

        auto properties = session.TryGetMediaPropertiesAsync().get();
        auto timeline = session.GetTimelineProperties();
        std::wstring title = properties.Title().c_str();
        std::wstring artist = properties.Artist().c_str();

        if (title.empty()) {
            title = L"Active session";
        }
        if (artist.empty()) {
            artist = L"Unknown artist";
        }

        CopyText(snapshot_.music_title, _countof(snapshot_.music_title), title.c_str());
        CopyText(snapshot_.music_artist, _countof(snapshot_.music_artist), artist.c_str());
        snapshot_.music_position =
            static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(timeline.Position()).count());
        snapshot_.music_duration =
            static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(timeline.EndTime()).count());
        snapshot_.music_playing =
            playback.PlaybackStatus() == media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
    } catch (const winrt::hresult_error&) {
        CopyText(snapshot_.music_title, _countof(snapshot_.music_title), L"Music unavailable");
        CopyText(snapshot_.music_artist, _countof(snapshot_.music_artist), L"SMTC read failed");
        CopyText(snapshot_.music_status, _countof(snapshot_.music_status), L"Error");
        snapshot_.music_position = 0.0;
        snapshot_.music_duration = 0.0;
        snapshot_.music_playing = false;
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

bool WidgetApp::FetchWeather(const WeatherSettings& settings, WidgetSnapshot* snapshot) {
    if (!snapshot) {
        return false;
    }

    std::string path = "/data/2.5/weather?appid=" + UrlEncode(WideToUtf8(settings.api_key)) +
                       "&units=" + UrlEncode(WideToUtf8(settings.units));
    if (settings.city_id > 0) {
        path += "&id=" + std::to_string(settings.city_id);
    } else {
        path += "&q=" + UrlEncode(WideToUtf8(settings.city + L"," + settings.country_code));
    }

    const std::wstring wide_path = Utf8ToWide(path);
    HINTERNET session = WinHttpOpen(L"SysmonWidgetNative/0.1",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        return false;
    }

    WinHttpSetTimeouts(session, 3000, 3000, 5000, 8000);
    HINTERNET connection = WinHttpConnect(session, L"api.openweathermap.org",
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", wide_path.c_str(),
                                          nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          WINHTTP_FLAG_SECURE);
    bool ok = false;
    std::string response;
    if (request &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                            WINHTTP_NO_HEADER_INDEX);

        if (status == 200) {
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
                    break;
                }
                chunk.resize(read);
                response += chunk;
            }
            ok = !response.empty();
        }
    }

    if (request) {
        WinHttpCloseHandle(request);
    }
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (!ok) {
        return false;
    }

    const double temp = ExtractJsonDouble(response, "temp", 0.0);
    const int humidity = ExtractJsonInt(response, "humidity", -1);
    const double wind = ExtractJsonDouble(response, "speed", -1.0);
    std::wstring city = Utf8ToWide(ExtractJsonString(response, "name", WideToUtf8(settings.city)));
    std::wstring description = TitleCaseAscii(Utf8ToWide(ExtractJsonString(response, "description", "Unknown")));

    if (city.empty()) {
        city = settings.city;
    }
    swprintf_s(snapshot->weather_temp, L"%.0f C", temp);
    CopyText(snapshot->weather_city, _countof(snapshot->weather_city), city.c_str());
    CopyText(snapshot->weather_detail, _countof(snapshot->weather_detail), description.c_str());

    std::wstring detail;
    if (settings.show_wind && wind >= 0.0) {
        wchar_t wind_text[32]{};
        swprintf_s(wind_text, L"W %.1f", wind);
        detail += wind_text;
    }
    if (settings.show_humidity && humidity >= 0) {
        wchar_t humidity_text[32]{};
        swprintf_s(humidity_text, L"%sH %d%%", detail.empty() ? L"" : L"  ", humidity);
        detail += humidity_text;
    }
    CopyText(snapshot->weather_meta, _countof(snapshot->weather_meta), detail.c_str());
    return true;
}

unsigned long long WidgetApp::FileTimeToUint64(const FILETIME& value) {
    ULARGE_INTEGER result{};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

bool WidgetApp::ReadTemperatureCelsius(float* temperature) {
    if (!temperature) {
        return false;
    }

    long perf_temperature = 0;
    if (QueryWmiLong(L"ROOT\\CIMV2",
                     L"SELECT Temperature FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation",
                     L"Temperature", &perf_temperature) &&
        perf_temperature > 273 && perf_temperature < 423) {
        *temperature = static_cast<float>(perf_temperature - 273);
        return true;
    }

    long acpi_temperature = 0;
    if (QueryWmiLong(L"ROOT\\WMI",
                     L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature",
                     L"CurrentTemperature", &acpi_temperature) &&
        acpi_temperature > 0) {
        *temperature = static_cast<float>(acpi_temperature) / 10.0f - 273.15f;
        return *temperature > -50.0f && *temperature < 150.0f;
    }

    return false;
}

bool WidgetApp::QueryWmiLong(const wchar_t* namespace_name, const wchar_t* query,
                             const wchar_t* property, long* value) {
    if (!namespace_name || !query || !property || !value) {
        return false;
    }

    HRESULT security = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                                            RPC_C_AUTHN_LEVEL_DEFAULT,
                                            RPC_C_IMP_LEVEL_IMPERSONATE,
                                            nullptr, EOAC_NONE, nullptr);
    if (FAILED(security) && security != RPC_E_TOO_LATE) {
        return false;
    }

    IWbemLocator* locator = nullptr;
    HRESULT result = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IWbemLocator, reinterpret_cast<void**>(&locator));
    if (FAILED(result)) {
        return false;
    }

    BSTR namespace_bstr = SysAllocString(namespace_name);
    IWbemServices* services = nullptr;
    result = locator->ConnectServer(namespace_bstr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    SysFreeString(namespace_bstr);
    locator->Release();
    if (FAILED(result)) {
        return false;
    }

    CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                      nullptr, EOAC_NONE);

    IEnumWbemClassObject* enumerator = nullptr;
    BSTR query_language = SysAllocString(L"WQL");
    BSTR query_bstr = SysAllocString(query);
    result = services->ExecQuery(query_language, query_bstr,
                                 WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                 nullptr, &enumerator);
    SysFreeString(query_bstr);
    SysFreeString(query_language);
    services->Release();
    if (FAILED(result)) {
        return false;
    }

    IWbemClassObject* object = nullptr;
    ULONG returned = 0;
    bool found = false;
    result = enumerator->Next(1000, 1, &object, &returned);
    if (SUCCEEDED(result) && returned > 0) {
        VARIANT variant{};
        VariantInit(&variant);
        if (SUCCEEDED(object->Get(property, 0, &variant, nullptr, nullptr))) {
            if (variant.vt == VT_I4 || variant.vt == VT_INT) {
                *value = variant.lVal;
                found = true;
            } else if (variant.vt == VT_UI4 || variant.vt == VT_UINT) {
                *value = static_cast<long>(variant.ulVal);
                found = true;
            }
        }
        VariantClear(&variant);
        object->Release();
    }

    enumerator->Release();
    return found;
}

void WidgetApp::AddTrayIcon() {
    icon_ = LoadIconW(nullptr, IDI_APPLICATION);

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
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   cursor.x, cursor.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}
