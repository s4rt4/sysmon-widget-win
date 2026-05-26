#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

struct WidgetSnapshot {
    static constexpr int VisualizerBars = 18;

    wchar_t time[16]{};
    wchar_t weekday[32]{};          // "Tuesday"
    wchar_t date_long[48]{};        // "26 May 2026"
    // Top 2 processes for the side-by-side process card.
    wchar_t top_process_name[2][32]{{L"--"}, {L"--"}};
    wchar_t top_process_detail[2][64]{{L"CPU --  RAM --"}, {L"CPU --  RAM --"}};
    int top_process_count = 0;
    wchar_t weather_temp[32]{L"-- C"};
    wchar_t weather_city[64]{L"Weather"};
    wchar_t weather_detail[96]{L"Loading weather"};
    wchar_t weather_meta[64]{};
    wchar_t weather_icon[8]{L"☁"};  // ☁ default cloud
    // Uptime / boot-time strip card at the very bottom of the widget.
    wchar_t uptime_text[32]{L"--"};       // "1d 5h 23m"
    wchar_t boot_text[48]{L"--"};         // "26 May 08:10"
    wchar_t music_title[128]{L"Stopped"};
    wchar_t music_artist[128]{L"No active session"};
    wchar_t music_status[32]{L"SMTC idle"};
    wchar_t network_down_text[32]{L"0.0 KB/s"};
    wchar_t network_up_text[32]{L"0.0 KB/s"};
    int storage_count = 0;
    wchar_t storage_label[2][16]{{L"C:"}, {L""}};
    wchar_t storage_percent_text[2][16]{{L"--"}, {L""}};
    wchar_t storage_detail[2][64]{{L"Storage unavailable"}, {L""}};
    float storage_usage[2]{0.0f, 0.0f};
    float cpu_usage = 0.0f;
    float ram_usage = 0.0f;
    float battery_usage = 0.0f;
    float temperature_usage = 0.0f;
    wchar_t cpu_text[16]{L"0%"};
    wchar_t ram_text[16]{L"0%"};
    wchar_t battery_text[16]{L"N/A"};
    wchar_t temperature_text[16]{L"N/A"};
    double music_position = 0.0;
    double music_duration = 0.0;
    bool music_playing = false;
    float music_levels[VisualizerBars]{};
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Initialize(HWND hwnd);
    void Resize(UINT width, UINT height);
    // Throw away the current Direct2D device resources (target + brushes)
    // so the next Render() call rebuilds them. Used after WM_DPICHANGED so
    // the target's SetDpi gets re-queried for the new monitor.
    void Reset();
    void Render(const WidgetSnapshot& snapshot);

private:
    bool CreateTextFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** output);
    bool EnsureDeviceResources();
    bool EnsureBackBuffer();
    void DiscardDeviceResources();
    void DiscardBackBuffer();
    void PresentLayeredWindow();

    // Per-card neon accent indices.
    enum CardAccent {
        AccentClock,
        AccentWeather,
        AccentNetwork,
        AccentCpu,
        AccentRam,
        AccentBattery,
        AccentTemp,
        AccentProcess,
        AccentStorage,
        AccentMusic,
        AccentUptime,
        AccentCount,
    };

    void DrawRoundedCard(const D2D1_RECT_F& rect);
    void DrawTextLine(const wchar_t* text, IDWriteTextFormat* format, ID2D1Brush* brush,
                      const D2D1_RECT_F& rect);
    void DrawClockCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot);
    void DrawWeatherCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot);
    void DrawNetworkCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot);
    void DrawInfoCard(const D2D1_RECT_F& rect, const wchar_t* heading, const wchar_t* value,
                      const wchar_t* detail, const wchar_t* footer, ID2D1Brush* accent);
    void DrawProcessCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot);
    void DrawUptimeCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot);
    void DrawGaugeCard(const D2D1_RECT_F& rect, const wchar_t* label, float value,
                       const wchar_t* text, ID2D1Brush* accent);
    void DrawStorageCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot);
    void DrawMusicCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot);
    void DrawBar(const D2D1_RECT_F& rect, float value, ID2D1Brush* accent);
    void DrawVisualizer(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot,
                        ID2D1Brush* accent);

    HWND hwnd_ = nullptr;
    ID2D1Factory* factory_ = nullptr;
    IDWriteFactory* write_factory_ = nullptr;
    ID2D1DCRenderTarget* target_ = nullptr;
    HDC memory_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HBITMAP old_bitmap_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    ID2D1SolidColorBrush* panel_brush_ = nullptr;
    ID2D1SolidColorBrush* text_brush_ = nullptr;
    ID2D1SolidColorBrush* muted_brush_ = nullptr;
    ID2D1SolidColorBrush* track_brush_ = nullptr;
    ID2D1SolidColorBrush* accent_brushes_[AccentCount]{};
    IDWriteTextFormat* clock_format_ = nullptr;        // 44pt centered (time)
    IDWriteTextFormat* date_format_ = nullptr;         // 15pt centered (date)
    IDWriteTextFormat* heading_format_ = nullptr;
    IDWriteTextFormat* body_format_ = nullptr;
    IDWriteTextFormat* muted_format_ = nullptr;
    IDWriteTextFormat* gauge_format_ = nullptr;
    IDWriteTextFormat* small_format_ = nullptr;
    IDWriteTextFormat* weather_temp_format_ = nullptr; // 28pt (big °C)
    IDWriteTextFormat* weather_icon_format_ = nullptr; // 36pt (☀☁☂⚡❄)
};
