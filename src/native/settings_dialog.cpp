#include "settings_dialog.h"

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <commctrl.h>
#include <shlobj.h>

namespace {

constexpr wchar_t kClassName[] = L"SysmonWidgetSettingsDialog";

// Control IDs
constexpr int kIdApiKey       = 101;
constexpr int kIdCity         = 102;
constexpr int kIdCityId       = 103;
constexpr int kIdCountry      = 104;
constexpr int kIdAnchor       = 105;
constexpr int kIdPosX         = 106;
constexpr int kIdPosY         = 107;
constexpr int kIdAutostart    = 108;
constexpr int kIdSave         = 110;
constexpr int kIdCancel       = 111;
constexpr int kIdOpenFolder   = 112;

struct DialogState {
    WeatherSettings weather_current;
    PositionSettings position_current;
    WeatherSettings weather_result;
    PositionSettings position_result;
    HWND hwnd = nullptr;
    HWND api_key = nullptr;
    HWND city = nullptr;
    HWND city_id = nullptr;
    HWND country = nullptr;
    HWND anchor = nullptr;
    HWND pos_x = nullptr;
    HWND pos_y = nullptr;
    HWND autostart = nullptr;
    HFONT font = nullptr;
    bool saved = false;
    bool alive = true;
};

std::wstring Trim(std::wstring v) {
    while (!v.empty() && (v.back() == L' ' || v.back() == L'\t' ||
                          v.back() == L'\r' || v.back() == L'\n')) v.pop_back();
    while (!v.empty() && (v.front() == L' ' || v.front() == L'\t')) v.erase(v.begin());
    return v;
}

std::wstring GetEditText(HWND edit) {
    const int len = GetWindowTextLengthW(edit);
    std::wstring out(static_cast<size_t>(len), L'\0');
    if (len > 0) GetWindowTextW(edit, out.data(), len + 1);
    return out;
}

std::string WideToUtf8(const std::wstring& v) {
    if (v.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, v.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, v.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

// JSON-escape a UTF-8 string (basic: ", \, control chars).
std::string JsonString(const std::string& v) {
    std::string r;
    r.reserve(v.size() + 2);
    r.push_back('"');
    for (char c : v) {
        switch (c) {
        case '"':  r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n";  break;
        case '\r': r += "\\r";  break;
        case '\t': r += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                r += buf;
            } else {
                r.push_back(c);
            }
        }
    }
    r.push_back('"');
    return r;
}

std::filesystem::path ConfigPath() {
    wchar_t appdata[MAX_PATH] = L"";
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(appdata) / L"SysmonWidget" / L"config.json";
}

std::string ReadExisting(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string BuildWeatherJson(const WeatherSettings& s) {
    std::string r = "{\n";
    r += "    \"api_key\": "      + JsonString(WideToUtf8(s.api_key)) + ",\n";
    r += "    \"city\": "         + JsonString(WideToUtf8(s.city)) + ",\n";
    r += "    \"city_id\": "      + std::to_string(s.city_id) + ",\n";
    r += "    \"country_code\": " + JsonString(WideToUtf8(s.country_code)) + ",\n";
    r += "    \"units\": "        + JsonString(WideToUtf8(s.units)) + ",\n";
    r += "    \"refresh_sec\": "  + std::to_string(s.refresh_sec) + ",\n";
    r += "    \"show_humidity\": " + std::string(s.show_humidity ? "true" : "false") + ",\n";
    r += "    \"show_wind\": "    + std::string(s.show_wind ? "true" : "false") + "\n";
    r += "  }";
    return r;
}

std::string BuildPositionJson(const PositionSettings& p) {
    std::string r = "{\n";
    r += "    \"anchor\": " + JsonString(WideToUtf8(p.anchor)) + ",\n";
    r += "    \"x\": "      + std::to_string(p.x) + ",\n";
    r += "    \"y\": "      + std::to_string(p.y) + "\n";
    r += "  }";
    return r;
}

// Replace (or insert) the top-level object at `key` inside the existing
// JSON. Preserves any other top-level keys so config.json shared with the
// Python build doesn't lose data. Generic so we can use it for "weather"
// and "position" alike.
std::string MergeObject(const std::string& existing, const std::string& key,
                        const std::string& value_json) {
    const std::string key_token = "\"" + key + "\"";

    if (existing.empty()) {
        return "{\n  " + key_token + ": " + value_json + "\n}\n";
    }

    auto skip_ws = [](const std::string& s, size_t& i) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    };

    // Locate the key.
    size_t key_pos = existing.find(key_token);
    if (key_pos != std::string::npos) {
        size_t colon = existing.find(':', key_pos);
        if (colon != std::string::npos) {
            size_t vs = colon + 1;
            skip_ws(existing, vs);
            if (vs < existing.size() && existing[vs] == '{') {
                // Walk to matching '}'.
                int depth = 0;
                bool in_str = false, esc = false;
                size_t ve = vs;
                for (size_t i = vs; i < existing.size(); ++i) {
                    char c = existing[i];
                    if (in_str) {
                        if (esc) esc = false;
                        else if (c == '\\') esc = true;
                        else if (c == '"') in_str = false;
                    } else {
                        if (c == '"') in_str = true;
                        else if (c == '{') ++depth;
                        else if (c == '}') {
                            if (--depth == 0) { ve = i + 1; break; }
                        }
                    }
                }
                return existing.substr(0, vs) + value_json + existing.substr(ve);
            }
        }
    }

    // No key — inject before final closing brace of the top object. Add a
    // comma if there are other keys present.
    size_t end = existing.find_last_of('}');
    if (end == std::string::npos) {
        return "{\n  " + key_token + ": " + value_json + "\n}\n";
    }
    std::string prefix = existing.substr(0, end);
    // Trim trailing whitespace.
    while (!prefix.empty() && std::isspace(static_cast<unsigned char>(prefix.back()))) {
        prefix.pop_back();
    }
    // Detect if the existing object had any keys at all.
    size_t open = prefix.find('{');
    bool has_keys = false;
    if (open != std::string::npos) {
        for (size_t i = open + 1; i < prefix.size(); ++i) {
            if (!std::isspace(static_cast<unsigned char>(prefix[i]))) {
                has_keys = true; break;
            }
        }
    }
    std::string out = prefix;
    if (has_keys) out += ",";
    out += "\n  " + key_token + ": " + value_json + "\n}\n";
    return out;
}

bool WriteConfig(const WeatherSettings& w, const PositionSettings& p) {
    auto path = ConfigPath();
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::string merged = ReadExisting(path);
    merged = MergeObject(merged, "weather",  BuildWeatherJson(w));
    merged = MergeObject(merged, "position", BuildPositionJson(p));

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(merged.data(), static_cast<std::streamsize>(merged.size()));
    return f.good();
}

void OpenConfigFolder() {
    auto path = ConfigPath();
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    ShellExecuteW(nullptr, L"open", path.parent_path().c_str(),
                  nullptr, nullptr, SW_SHOWNORMAL);
}

// Build the Save result from the controls.
bool CollectAndValidate(DialogState* st) {
    // ─── Weather ──────────────────────────────────────────────────────
    st->weather_result = st->weather_current;
    st->weather_result.api_key      = Trim(GetEditText(st->api_key));
    st->weather_result.city         = Trim(GetEditText(st->city));
    st->weather_result.country_code = Trim(GetEditText(st->country));
    std::transform(st->weather_result.country_code.begin(),
                   st->weather_result.country_code.end(),
                   st->weather_result.country_code.begin(),
                   [](wchar_t c) { return (wchar_t)std::toupper((int)c); });

    const std::wstring city_id_text = Trim(GetEditText(st->city_id));
    try {
        st->weather_result.city_id = city_id_text.empty() ? 0 : std::stoi(city_id_text);
        if (st->weather_result.city_id < 0) st->weather_result.city_id = 0;
    } catch (...) {
        MessageBoxW(st->hwnd, L"City ID must be a number.", L"Settings",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    // ─── Position ────────────────────────────────────────────────────
    st->position_result = st->position_current;
    const int sel = static_cast<int>(SendMessageW(st->anchor, CB_GETCURSEL, 0, 0));
    st->position_result.anchor = (sel == 1) ? L"left" : L"right";

    try {
        const std::wstring xt = Trim(GetEditText(st->pos_x));
        const std::wstring yt = Trim(GetEditText(st->pos_y));
        st->position_result.x = xt.empty() ? 0 : std::stoi(xt);
        st->position_result.y = yt.empty() ? 0 : std::stoi(yt);
        if (st->position_result.x < 0) st->position_result.x = 0;
        if (st->position_result.y < 0) st->position_result.y = 0;
    } catch (...) {
        MessageBoxW(st->hwnd, L"X / Y offset must be a number.", L"Settings",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    return true;
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_COMMAND: {
        if (!st) break;
        const int id = LOWORD(wp);
        if (id == kIdSave) {
            if (!CollectAndValidate(st)) return 0;
            if (!WriteConfig(st->weather_result, st->position_result)) {
                MessageBoxW(hwnd, L"Failed to write config.json.",
                            L"Settings", MB_OK | MB_ICONERROR);
                return 0;
            }
            WidgetApp::SetAutostartEnabled(
                SendMessageW(st->autostart, BM_GETCHECK, 0, 0) == BST_CHECKED);
            st->saved = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == kIdCancel) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == kIdOpenFolder) {
            OpenConfigFolder();
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (st) {
            if (st->font) DeleteObject(st->font);
            st->alive = false;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND MakeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, HFONT font) {
    HWND h_ = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, parent, nullptr, nullptr, nullptr);
    if (h_ && font) SendMessageW(h_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return h_;
}

HWND MakeEdit(HWND parent, int id, int x, int y, int w, int h, HFONT font,
              DWORD extra = 0) {
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr, nullptr);
    if (e && font) SendMessageW(e, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return e;
}

HWND MakeCheck(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h,
               HFONT font) {
    HWND c = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr, nullptr);
    if (c && font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
}

HWND MakeCombo(HWND parent, int id, int x, int y, int w, int h, HFONT font) {
    // For CBS_DROPDOWNLIST the height parameter includes the dropdown
    // list area, so we pass a tall value (~120 px) and Windows shows the
    // collapsed combo at the top.
    HWND c = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr, nullptr);
    if (c && font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
}

HWND MakeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h,
                HFONT font, bool is_default = false) {
    HWND b = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        (is_default ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr, nullptr);
    if (b && font) SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return b;
}

void EnsureClassRegistered() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

bool SettingsDialog::Show(HWND parent,
                          const WeatherSettings& weather_in,
                          const PositionSettings& position_in,
                          WeatherSettings* weather_out,
                          PositionSettings* position_out) {
    EnsureClassRegistered();

    DialogState state;
    state.weather_current = weather_in;
    state.position_current = position_in;

    // Query DPI so we can scale the dialog AND its controls (the app is
    // PER_MONITOR_AWARE, so without this everything is sized in physical
    // pixels — fine at 100% scaling, cramped/clipped at 125/150%).
    UINT dpi = 96;
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        using PFN = UINT (WINAPI*)(HWND);
        if (auto pfn = reinterpret_cast<PFN>(GetProcAddress(u32, "GetDpiForWindow"))) {
            if (parent) dpi = pfn(parent);
        }
    }
    const float scale = dpi / 96.0f;
    auto S = [scale](int v) { return static_cast<int>(v * scale + 0.5f); };

    // Logical (design-unit) client area. Grew from 340 → 460 to fit the
    // new Position section between Weather and Startup.
    constexpr int kClientW = 440;
    constexpr int kClientH = 460;
    constexpr DWORD kStyle   = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    constexpr DWORD kStyleEx = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;

    // Compute outer window size including title bar + border at this DPI.
    RECT rc = {0, 0, S(kClientW), S(kClientH)};
    using PFN_AWRDPI = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto pfn_awr = u32 ? reinterpret_cast<PFN_AWRDPI>(
        GetProcAddress(u32, "AdjustWindowRectExForDpi")) : nullptr;
    if (pfn_awr) {
        pfn_awr(&rc, kStyle, FALSE, kStyleEx, dpi);
    } else {
        AdjustWindowRectEx(&rc, kStyle, FALSE, kStyleEx);
    }
    const int outer_w = rc.right - rc.left;
    const int outer_h = rc.bottom - rc.top;

    // Center dialog on parent (or screen).
    RECT pr{};
    if (parent) GetWindowRect(parent, &pr);
    else        SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
    const int cx = (pr.left + pr.right) / 2 - outer_w / 2;
    const int cy = (pr.top + pr.bottom) / 2 - outer_h / 2;

    HWND hwnd = CreateWindowExW(
        kStyleEx, kClassName, L"Sysmon Widget Settings",
        kStyle, cx, cy, outer_w, outer_h,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return false;

    state.hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    // Shared UI font scaled to DPI. -MulDiv(pt, dpi, 72) is the standard
    // CreateFont height calculation for "X points at given DPI".
    state.font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL,
                             FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    constexpr int kPadX = 18;
    constexpr int kRowH = 28;
    constexpr int kLabelW = 84;
    int y = 18;

    MakeLabel(hwnd, L"Weather", S(kPadX), S(y), S(200), S(18), state.font);
    y += 26;

    MakeLabel(hwnd, L"API key", S(kPadX), S(y + 4), S(kLabelW), S(18), state.font);
    state.api_key = MakeEdit(hwnd, kIdApiKey, S(kPadX + kLabelW), S(y), S(300), S(24),
                             state.font, ES_PASSWORD);
    SetWindowTextW(state.api_key, weather_in.api_key.c_str());
    y += kRowH;

    MakeLabel(hwnd, L"City", S(kPadX), S(y + 4), S(kLabelW), S(18), state.font);
    state.city = MakeEdit(hwnd, kIdCity, S(kPadX + kLabelW), S(y), S(300), S(24), state.font);
    SetWindowTextW(state.city, weather_in.city.c_str());
    y += kRowH;

    MakeLabel(hwnd, L"City ID", S(kPadX), S(y + 4), S(kLabelW), S(18), state.font);
    wchar_t city_id_buf[16];
    swprintf_s(city_id_buf, L"%d", weather_in.city_id);
    state.city_id = MakeEdit(hwnd, kIdCityId, S(kPadX + kLabelW), S(y), S(100), S(24),
                             state.font, ES_NUMBER);
    SetWindowTextW(state.city_id, city_id_buf);
    y += kRowH;

    MakeLabel(hwnd, L"Country", S(kPadX), S(y + 4), S(kLabelW), S(18), state.font);
    state.country = MakeEdit(hwnd, kIdCountry, S(kPadX + kLabelW), S(y), S(100), S(24), state.font);
    SetWindowTextW(state.country, weather_in.country_code.c_str());
    SendMessageW(state.country, EM_SETLIMITTEXT, 2, 0);
    y += kRowH + 10;

    // ─── Position section ──────────────────────────────────────────
    MakeLabel(hwnd, L"Position", S(kPadX), S(y), S(200), S(18), state.font);
    y += 26;

    MakeLabel(hwnd, L"Anchor", S(kPadX), S(y + 4), S(kLabelW), S(18), state.font);
    // Tall height (120) so the dropdown list has room to show; the
    // collapsed control sits at the top.
    state.anchor = MakeCombo(hwnd, kIdAnchor, S(kPadX + kLabelW), S(y), S(180), S(120),
                             state.font);
    SendMessageW(state.anchor, CB_ADDSTRING, 0, (LPARAM)L"Right");
    SendMessageW(state.anchor, CB_ADDSTRING, 0, (LPARAM)L"Left");
    SendMessageW(state.anchor, CB_SETCURSEL,
                 (position_in.anchor == L"left") ? 1 : 0, 0);
    y += kRowH;

    // X / Y offsets on one row to save vertical space.
    MakeLabel(hwnd, L"X offset", S(kPadX), S(y + 4), S(kLabelW), S(18), state.font);
    wchar_t pos_buf[16];
    swprintf_s(pos_buf, L"%d", position_in.x);
    state.pos_x = MakeEdit(hwnd, kIdPosX, S(kPadX + kLabelW), S(y), S(70), S(24),
                           state.font, ES_NUMBER);
    SetWindowTextW(state.pos_x, pos_buf);

    MakeLabel(hwnd, L"Y", S(kPadX + kLabelW + 84), S(y + 4), S(20), S(18), state.font);
    swprintf_s(pos_buf, L"%d", position_in.y);
    state.pos_y = MakeEdit(hwnd, kIdPosY, S(kPadX + kLabelW + 108), S(y), S(70), S(24),
                           state.font, ES_NUMBER);
    SetWindowTextW(state.pos_y, pos_buf);
    y += kRowH + 10;

    // ─── Startup section ────────────────────────────────────────────
    MakeLabel(hwnd, L"Startup", S(kPadX), S(y), S(200), S(18), state.font);
    y += 26;

    state.autostart = MakeCheck(hwnd, kIdAutostart, L"Start with Windows",
                                S(kPadX), S(y), S(260), S(22), state.font);
    SendMessageW(state.autostart, BM_SETCHECK,
                 WidgetApp::IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    // Bottom button row — positioned from logical client height so it
    // moves with kClientH if we resize the dialog later.
    const int by = kClientH - 50;
    MakeButton(hwnd, kIdOpenFolder, L"Open Config Folder",
               S(kPadX), S(by), S(160), S(30), state.font);
    MakeButton(hwnd, kIdCancel, L"Cancel",
               S(kClientW - 200), S(by), S(80), S(30), state.font);
    MakeButton(hwnd, kIdSave,   L"Save",
               S(kClientW - 110), S(by), S(80), S(30), state.font, true);

    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(state.api_key);

    // Modal-ish message pump until the dialog window is destroyed.
    MSG msg;
    while (state.alive && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (parent) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    if (state.saved) {
        if (weather_out)  *weather_out  = state.weather_result;
        if (position_out) *position_out = state.position_result;
    }
    return state.saved;
}
