#include "weather_fetcher.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cwchar>
#include <string>

#include <winhttp.h>

namespace {

// ─── Local UTF / URL / JSON helpers (kept here so weather_fetcher.cpp
// has no link-time dependency on widget_app.cpp internals). ────────────

std::string WideToUtf8(const std::wstring& v) {
    if (v.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, v.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, v.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWide(const std::string& v) {
    if (v.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, nullptr, 0);
    std::wstring s(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, s.data(), n);
    return s;
}

std::string UrlEncode(const std::string& v) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string r;
    for (unsigned char c : v) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            r.push_back(static_cast<char>(c));
        } else {
            r.push_back('%');
            r.push_back(hex[c >> 4]);
            r.push_back(hex[c & 0x0F]);
        }
    }
    return r;
}

std::string ExtractJsonString(const std::string& json, const std::string& key,
                              const std::string& fallback = {}) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return fallback;
    pos = json.find('"', pos);
    if (pos == std::string::npos) return fallback;
    ++pos;
    std::string out;
    bool esc = false;
    for (size_t i = pos; i < json.size(); ++i) {
        const char c = json[i];
        if (esc) { out.push_back(c); esc = false; }
        else if (c == '\\') esc = true;
        else if (c == '"') return out;
        else out.push_back(c);
    }
    return fallback;
}

int ExtractJsonInt(const std::string& json, const std::string& key, int fallback = 0) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    try { return std::stoi(json.substr(pos)); } catch (...) { return fallback; }
}

double ExtractJsonDouble(const std::string& json, const std::string& key,
                         double fallback = 0.0) {
    const std::string marker = "\"" + key + "\"";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    try { return std::stod(json.substr(pos)); } catch (...) { return fallback; }
}

std::wstring TitleCaseAscii(std::wstring v) {
    bool upper = true;
    for (wchar_t& c : v) {
        if (c == L' ' || c == L'-') upper = true;
        else if (upper && c >= L'a' && c <= L'z') { c = c - L'a' + L'A'; upper = false; }
        else upper = false;
    }
    return v;
}

const wchar_t* WeatherMainToIcon(const std::wstring& main) {
    if (main == L"Clear")        return L"☀";
    if (main == L"Clouds")       return L"☁";
    if (main == L"Rain")         return L"☂";
    if (main == L"Drizzle")      return L"☂";
    if (main == L"Thunderstorm") return L"⚡";
    if (main == L"Snow")         return L"❄";
    return L"☁";
}

}  // namespace

WeatherFetcher::WeatherFetcher(const WeatherSettings& initial) : settings_(initial) {
    worker_ = std::thread([this] { WorkerLoop(); });
}

WeatherFetcher::~WeatherFetcher() {
    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void WeatherFetcher::UpdateSettings(const WeatherSettings& settings) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        settings_ = settings;
    }
    // Trigger an immediate re-fetch with the new credentials/city.
    request_now_.store(true);
    cv_.notify_one();
}

bool WeatherFetcher::TryGet(Result* out) const {
    if (!has_value_.load() || !out) return false;
    std::lock_guard<std::mutex> lock(mu_);
    *out = latest_;
    return true;
}

void WeatherFetcher::WorkerLoop() {
    auto last_fetch = std::chrono::steady_clock::time_point::min();

    while (!stop_.load()) {
        WeatherSettings snap;
        {
            std::lock_guard<std::mutex> lock(mu_);
            snap = settings_;
        }

        const auto interval = std::chrono::seconds(std::max(60, snap.refresh_sec));
        const auto now = std::chrono::steady_clock::now();
        const bool due = (last_fetch == std::chrono::steady_clock::time_point::min())
                         || (now - last_fetch >= interval)
                         || request_now_.exchange(false);

        if (due) {
            Result r;
            if (FetchOnce(snap, &r)) {
                {
                    std::lock_guard<std::mutex> lock(mu_);
                    latest_ = std::move(r);
                }
                has_value_.store(true);
            } else if (!has_value_.load()) {
                // No successful sample yet — publish a placeholder so the
                // panel doesn't read uninitialized strings.
                Result placeholder;
                placeholder.temp   = L"--°";
                placeholder.city   = snap.city;
                placeholder.detail = snap.api_key.empty() || snap.api_key == L"YOUR_OPENWEATHERMAP_API_KEY"
                                     ? L"API key missing" : L"Weather unavailable";
                placeholder.icon   = L"☁";
                {
                    std::lock_guard<std::mutex> lock(mu_);
                    latest_ = std::move(placeholder);
                }
                has_value_.store(true);
            }
            last_fetch = std::chrono::steady_clock::now();
        }

        // Sleep up to one second; we re-check stop_ and request_now_ at
        // every wake-up so cancellation/refresh respond quickly.
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait_for(lock, std::chrono::seconds(1),
                     [this] { return stop_.load() || request_now_.load(); });
    }
}

bool WeatherFetcher::FetchOnce(const WeatherSettings& s, Result* out) {
    if (!out) return false;
    if (s.api_key.empty() || s.api_key == L"YOUR_OPENWEATHERMAP_API_KEY") {
        return false;
    }

    std::string path = "/data/2.5/weather?appid=" + UrlEncode(WideToUtf8(s.api_key)) +
                       "&units=" + UrlEncode(WideToUtf8(s.units));
    if (s.city_id > 0) {
        path += "&id=" + std::to_string(s.city_id);
    } else {
        path += "&q=" + UrlEncode(WideToUtf8(s.city + L"," + s.country_code));
    }
    const std::wstring wpath = Utf8ToWide(path);

    HINTERNET session = WinHttpOpen(L"SysmonWidgetNative/0.1",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    WinHttpSetTimeouts(session, 3000, 3000, 5000, 8000);
    HINTERNET conn = WinHttpConnect(session, L"api.openweathermap.org",
                                    INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) { WinHttpCloseHandle(session); return false; }

    HINTERNET req = WinHttpOpenRequest(conn, L"GET", wpath.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    bool ok = false;
    std::string response;
    if (req &&
        WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, ssize = sizeof(status);
        WinHttpQueryHeaders(req,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &ssize,
                            WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
                chunk.resize(read);
                response += chunk;
            }
            ok = !response.empty();
        }
    }
    if (req) WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    if (!ok) return false;

    const double temp_v   = ExtractJsonDouble(response, "temp", 0.0);
    const int humidity    = ExtractJsonInt(response, "humidity", -1);
    const double wind     = ExtractJsonDouble(response, "speed", -1.0);
    std::wstring city     = Utf8ToWide(ExtractJsonString(response, "name", WideToUtf8(s.city)));
    std::wstring descr    = TitleCaseAscii(Utf8ToWide(
        ExtractJsonString(response, "description", "Unknown")));
    const std::wstring mainCat = Utf8ToWide(ExtractJsonString(response, "main", "Clouds"));

    if (city.empty()) city = s.city;
    wchar_t tbuf[32];
    swprintf_s(tbuf, L"%.0f°C", temp_v);
    out->temp   = tbuf;
    out->city   = city;
    out->detail = descr;
    out->icon   = WeatherMainToIcon(mainCat);

    std::wstring meta;
    if (s.show_humidity && humidity >= 0) {
        wchar_t hbuf[32];
        swprintf_s(hbuf, L"H %d%%", humidity);
        meta += hbuf;
    }
    if (s.show_wind && wind >= 0.0) {
        wchar_t wbuf[32];
        swprintf_s(wbuf, L"%sW %.1f", meta.empty() ? L"" : L"  ", wind);
        meta += wbuf;
    }
    out->meta = meta;

    return true;
}
