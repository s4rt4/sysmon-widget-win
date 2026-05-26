#include "renderer.h"

#include <cmath>
#include <cwchar>

namespace {
template <typename T>
void SafeRelease(T** value) {
    if (*value) {
        (*value)->Release();
        *value = nullptr;
    }
}

D2D1_COLOR_F Color(float r, float g, float b, float a = 1.0f) {
    return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

D2D1_RECT_F Rect(float left, float top, float right, float bottom) {
    return D2D1::RectF(left, top, right, bottom);
}

// Solid panel background.
constexpr UINT32 kCardBgHex     = 0x0E2422;

// White / muted text.
constexpr UINT32 kTextWhiteHex  = 0xFFFFFF;
constexpr UINT32 kTextMutedHex  = 0xA0B0AC;
// Unfilled portion of progress bars (Storage, Music) is white per user
// request — was a dark teal that blended into the card background.
constexpr UINT32 kTrackBgHex    = 0xFFFFFF;

// Per-card neon accent palette. Each card gets its own bright color so the
// dark cards "pop". Order matches Renderer::CardAccent enum.
constexpr UINT32 kAccentHex[11] = {
    0x00E5FF,  // Clock    — cyan
    0xFFC400,  // Weather  — amber
    0x76FF03,  // Network  — lime
    0xFF1744,  // CPU      — pink/red
    0xD500F9,  // RAM      — violet
    0x00E676,  // Battery  — green
    0xFF6E40,  // Temp     — orange
    0xF50057,  // Process  — magenta
    0xFFEA00,  // Storage  — yellow
    0x18FFFF,  // Music    — cyan
    0x40C4FF,  // Uptime   — sky blue
};
} // namespace

Renderer::~Renderer() {
    DiscardDeviceResources();
    SafeRelease(&body_center_format_);
    SafeRelease(&mdl2_center_format_);
    SafeRelease(&mdl2_format_);
    SafeRelease(&weather_icon_format_);
    SafeRelease(&weather_temp_format_);
    SafeRelease(&small_format_);
    SafeRelease(&gauge_format_);
    SafeRelease(&muted_format_);
    SafeRelease(&body_format_);
    SafeRelease(&heading_format_);
    SafeRelease(&date_format_);
    SafeRelease(&clock_format_);
    SafeRelease(&write_factory_);
    SafeRelease(&factory_);
}

bool Renderer::Initialize(HWND hwnd) {
    hwnd_ = hwnd;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_))) {
        return false;
    }

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&write_factory_)))) {
        return false;
    }

    // 2x larger clock, left-aligned (time block on the left half of the
    // card). Date/weekday on the right are smaller.
    if (!CreateTextFormat(88.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &clock_format_)) {
        return false;
    }
    if (!CreateTextFormat(16.0f, DWRITE_FONT_WEIGHT_NORMAL, &date_format_)) {
        return false;
    }
    // Used by clock-card weekday. Bigger than the rest of the headings for
    // emphasis (the dead InfoCard reference was the only other user).
    if (!CreateTextFormat(22.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &heading_format_)) {
        return false;
    }
    if (!CreateTextFormat(13.0f, DWRITE_FONT_WEIGHT_NORMAL, &body_format_)) {
        return false;
    }
    if (!CreateTextFormat(11.0f, DWRITE_FONT_WEIGHT_NORMAL, &muted_format_)) {
        return false;
    }
    if (!CreateTextFormat(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &gauge_format_)) {
        return false;
    }
    if (!CreateTextFormat(10.0f, DWRITE_FONT_WEIGHT_NORMAL, &small_format_)) {
        return false;
    }
    // Big weather temperature (matches Python's bold 16pt scaled up).
    if (!CreateTextFormat(28.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &weather_temp_format_)) {
        return false;
    }
    // Weather glyph icon — large enough to read at a glance.
    if (!CreateTextFormat(36.0f, DWRITE_FONT_WEIGHT_NORMAL, &weather_icon_format_)) {
        return false;
    }
    // Segoe MDL2 Assets — monochrome line-art icon font shipped with
    // Win10+. We use it for the speaker (E767) and brightness (E706)
    // glyphs in the uptime strip card.
    if (FAILED(write_factory_->CreateTextFormat(
            L"Segoe MDL2 Assets", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &mdl2_format_))) {
        return false;
    }
    // Centered MDL2 + body formats for the volume/brightness columns,
    // where the values are short and the rest of the column is whitespace.
    // Centering balances the left side (long uptime/boot text) with the
    // right side (3-char percentages).
    if (FAILED(write_factory_->CreateTextFormat(
            L"Segoe MDL2 Assets", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &mdl2_center_format_))) {
        return false;
    }
    if (!CreateTextFormat(13.0f, DWRITE_FONT_WEIGHT_NORMAL, &body_center_format_)) {
        return false;
    }

    body_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    muted_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    heading_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    clock_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    date_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    gauge_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    small_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    weather_temp_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    weather_icon_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    mdl2_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    mdl2_center_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    mdl2_center_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    body_center_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    body_center_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    gauge_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    gauge_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // Clock: time block is left-aligned and vertically centered. Day name +
    // date sit on the right column with their own alignment.
    clock_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    clock_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    date_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    weather_icon_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    weather_icon_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    return true;
}

void Renderer::Reset() {
    DiscardDeviceResources();
    // Marquee state is presentation-only — drop it so the title doesn't
    // skip frames after a DPI change (column width changes → wrap value
    // changes → old offset would be out of bounds for one frame).
    title_scroll_offset_ = 0.0f;
    title_scroll_last_tick_ = 0;
    title_prev_[0] = L'\0';
}

void Renderer::Resize(UINT width, UINT height) {
    if (width_ != width || height_ != height) {
        width_ = width;
        height_ = height;
        DiscardBackBuffer();
        if (target_) {
            SafeRelease(&target_);
            SafeRelease(&track_brush_);
            SafeRelease(&muted_brush_);
            SafeRelease(&text_brush_);
            for (int i = 0; i < AccentCount; ++i) {
                SafeRelease(&accent_brushes_[i]);
            }
            SafeRelease(&panel_brush_);
        }
    }
}

void Renderer::Render(const WidgetSnapshot& snapshot) {
    if (!EnsureDeviceResources()) {
        return;
    }

    target_->BeginDraw();
    target_->Clear(Color(0, 0, 0, 0.0f));

    const float width = target_->GetSize().width;
    const float gap = 10.0f;
    const float x = 0.0f;
    float y = 0.0f;

    // Bigger clock card to fit the 88pt time glyph + day/date right column.
    DrawClockCard(Rect(x, y, width, y + 140.0f), snapshot);
    y += 140.0f + gap;

    const float half = (width - gap) / 2.0f;
    DrawWeatherCard(Rect(x, y, half, y + 108.0f), snapshot);
    DrawNetworkCard(Rect(half + gap, y, width, y + 108.0f), snapshot);
    y += 108.0f + gap;

    const float mini_gap = 8.0f;
    const float mini = (width - mini_gap * 3.0f) / 4.0f;
    DrawGaugeCard(Rect(x, y, x + mini, y + 92.0f), L"CPU", snapshot.cpu_usage, snapshot.cpu_text,
                  accent_brushes_[AccentCpu]);
    DrawGaugeCard(Rect(x + (mini + mini_gap), y, x + mini * 2.0f + mini_gap, y + 92.0f), L"RAM",
                  snapshot.ram_usage, snapshot.ram_text, accent_brushes_[AccentRam]);
    DrawGaugeCard(Rect(x + (mini + mini_gap) * 2.0f, y, x + mini * 3.0f + mini_gap * 2.0f, y + 92.0f),
                  L"BAT", snapshot.battery_usage, snapshot.battery_text,
                  accent_brushes_[AccentBattery]);
    DrawGaugeCard(Rect(x + (mini + mini_gap) * 3.0f, y, width, y + 92.0f), L"TEMP",
                  snapshot.temperature_usage, snapshot.temperature_text,
                  accent_brushes_[AccentTemp]);
    y += 92.0f + gap;

    // Music is now the wide row (2-column layout: left = text/progress,
    // right = visualizer). Process card moves to the half-card slot to the
    // right of Storage, stacked vertically.
    DrawMusicCard(Rect(x, y, width, y + 88.0f), snapshot);
    y += 88.0f + gap;

    DrawStorageCard(Rect(x, y, half, y + 158.0f), snapshot);
    DrawProcessCard(Rect(half + gap, y, width, y + 158.0f), snapshot);
    y += 158.0f + gap;

    // Thin uptime / boot-time strip card at the very bottom.
    DrawUptimeCard(Rect(x, y, width, y + 56.0f), snapshot);

    HRESULT result = target_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    } else if (SUCCEEDED(result)) {
        PresentLayeredWindow();
    }
}

bool Renderer::CreateTextFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** output) {
    return SUCCEEDED(write_factory_->CreateTextFormat(
        L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"en-us", output));
}

bool Renderer::EnsureDeviceResources() {
    if (target_ && memory_dc_) {
        return true;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);
    width_ = static_cast<UINT>(client.right - client.left);
    height_ = static_cast<UINT>(client.bottom - client.top);

    // Guard against a zero-sized client area (window not yet shown).
    // CreateDIBSection / CreateDCRenderTarget would otherwise fail and
    // leave partially-initialised state.
    if (width_ == 0 || height_ == 0) {
        return false;
    }

    // Helper: tear down whatever was created so far on a failure path —
    // otherwise the next call sees target_/memory_dc_ non-null and the
    // early-out would skip recreation of the missing brushes/formats.
    auto fail = [this]() -> bool {
        DiscardDeviceResources();
        return false;
    };

    if (!EnsureBackBuffer()) {
        return fail();
    }

    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    if (FAILED(factory_->CreateDCRenderTarget(&properties, &target_))) {
        return fail();
    }

    if (FAILED(target_->BindDC(memory_dc_, &client))) {
        return fail();
    }

    // Tell the render target what DPI it's actually drawing at. The bitmap
    // and DC are sized in physical pixels (because the app is DPI-aware),
    // but our layout coords are design units. SetDpi makes D2D translate
    // DIP coordinates to physical pixels for us, giving sharp text at all
    // display scales.
    UINT dpi = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using PFN_GDFW = UINT (WINAPI*)(HWND);
        auto pfn = reinterpret_cast<PFN_GDFW>(GetProcAddress(user32, "GetDpiForWindow"));
        if (pfn) dpi = pfn(hwnd_);
    }
    if (dpi == 0) dpi = 96;
    target_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));

    // Layered windows render onto a per-pixel-alpha bitmap, so ClearType
    // (which assumes an opaque background) produces colored fringes and a
    // pixelated look. GRAYSCALE antialiasing is the recommended mode.
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    // Solid card background (#0E2422) — no longer translucent.
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(kCardBgHex), &panel_brush_))) {
        return fail();
    }
    // White text content, muted gray captions, dark track for progress bars.
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(kTextWhiteHex), &text_brush_))) {
        return fail();
    }
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(kTextMutedHex), &muted_brush_))) {
        return fail();
    }
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(kTrackBgHex), &track_brush_))) {
        return fail();
    }
    // Per-card neon accents.
    for (int i = 0; i < AccentCount; ++i) {
        if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(kAccentHex[i]),
                                                  &accent_brushes_[i]))) {
            return fail();
        }
    }

    return true;
}

bool Renderer::EnsureBackBuffer() {
    if (memory_dc_) {
        return true;
    }

    HDC screen = GetDC(nullptr);
    if (!screen) {
        return false;
    }

    memory_dc_ = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!memory_dc_) {
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(width_);
    info.bmiHeader.biHeight = -static_cast<LONG>(height_);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    bitmap_ = CreateDIBSection(memory_dc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap_) {
        DiscardBackBuffer();
        return false;
    }

    old_bitmap_ = static_cast<HBITMAP>(SelectObject(memory_dc_, bitmap_));
    RECT rect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    if (target_ && FAILED(target_->BindDC(memory_dc_, &rect))) {
        return false;
    }

    return true;
}

void Renderer::DiscardDeviceResources() {
    SafeRelease(&track_brush_);
    SafeRelease(&muted_brush_);
    SafeRelease(&text_brush_);
    for (int i = 0; i < AccentCount; ++i) {
        SafeRelease(&accent_brushes_[i]);
    }
    SafeRelease(&panel_brush_);
    SafeRelease(&target_);
    DiscardBackBuffer();
}

void Renderer::DiscardBackBuffer() {
    if (memory_dc_ && old_bitmap_) {
        SelectObject(memory_dc_, old_bitmap_);
        old_bitmap_ = nullptr;
    }
    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    if (memory_dc_) {
        DeleteDC(memory_dc_);
        memory_dc_ = nullptr;
    }
}

void Renderer::PresentLayeredWindow() {
    RECT window{};
    GetWindowRect(hwnd_, &window);

    POINT destination{window.left, window.top};
    SIZE size{static_cast<LONG>(width_), static_cast<LONG>(height_)};
    POINT source{0, 0};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(hwnd_, screen, &destination, &size, memory_dc_, &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

void Renderer::DrawRoundedCard(const D2D1_RECT_F& rect) {
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 8.0f, 8.0f), panel_brush_);
}

void Renderer::DrawTextLine(const wchar_t* text, IDWriteTextFormat* format, ID2D1Brush* brush,
                            const D2D1_RECT_F& rect) {
    target_->DrawTextW(text, static_cast<UINT32>(std::wcslen(text)), format, rect, brush,
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Renderer::DrawClockCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    // Big time on the left half, weekday + date on the right.
    const float left = rect.left + 18.0f;
    const float right = rect.right - 16.0f;
    const float mid = rect.left + (rect.right - rect.left) * 0.60f;

    DrawTextLine(snapshot.time, clock_format_, text_brush_,
                 Rect(left, rect.top, mid - 6.0f, rect.bottom));

    // Thin vertical separator between the time block and the date column.
    target_->DrawLine(
        D2D1::Point2F(mid - 3.0f, rect.top + 26.0f),
        D2D1::Point2F(mid - 3.0f, rect.bottom - 22.0f),
        muted_brush_, 1.0f);

    // Right column starts further from the separator so the text doesn't
    // hug the line. weekday/date/seconds are stacked tightly around the
    // card's vertical center.
    const float r_left = mid + 12.0f;
    DrawTextLine(snapshot.weekday, heading_format_, accent_brushes_[AccentClock],
                 Rect(r_left, rect.top + 40.0f, right, rect.top + 70.0f));
    DrawTextLine(snapshot.date_long, date_format_, text_brush_,
                 Rect(r_left, rect.top + 72.0f, right, rect.top + 94.0f));
    DrawTextLine(snapshot.seconds_text, body_format_, accent_brushes_[AccentClock],
                 Rect(r_left, rect.top + 96.0f, right, rect.top + 118.0f));
}

void Renderer::DrawWeatherCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    const float left = rect.left + 14.0f;
    const float top = rect.top + 10.0f;
    const float icon_w = 56.0f;
    const float right = rect.right - 12.0f;

    DrawTextLine(L"WEATHER", muted_format_, accent_brushes_[AccentWeather],
                 Rect(left, top, right, top + 17.0f));
    // Big weather glyph on the left.
    DrawTextLine(snapshot.weather_icon, weather_icon_format_, accent_brushes_[AccentWeather],
                 Rect(left, rect.top + 28.0f, left + icon_w, rect.bottom - 8.0f));
    // Right column packed tighter to fit 4 lines: big temp, city, detail,
    // and humidity/wind meta from weather_meta.
    DrawTextLine(snapshot.weather_temp, weather_temp_format_, text_brush_,
                 Rect(left + icon_w, rect.top + 24.0f, right, rect.top + 60.0f));
    DrawTextLine(snapshot.weather_city, small_format_, text_brush_,
                 Rect(left + icon_w, rect.top + 62.0f, right, rect.top + 76.0f));
    DrawTextLine(snapshot.weather_detail, small_format_, muted_brush_,
                 Rect(left + icon_w, rect.top + 76.0f, right, rect.top + 90.0f));
    DrawTextLine(snapshot.weather_meta, small_format_, muted_brush_,
                 Rect(left + icon_w, rect.top + 90.0f, right, rect.bottom - 6.0f));
}

void Renderer::DrawNetworkCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    const float left = rect.left + 14.0f;
    const float right = rect.right - 12.0f;
    const float mid = rect.left + (rect.right - rect.left) / 2.0f;

    DrawTextLine(L"NETWORK", muted_format_, accent_brushes_[AccentNetwork],
                 Rect(left, rect.top + 10.0f, right, rect.top + 27.0f));

    // Down/Up labels + values — compressed slightly so the daily total
    // line below has room within the 108 px card.
    DrawTextLine(L"Download", small_format_, text_brush_,
                 Rect(left, rect.top + 34.0f, mid - 6.0f, rect.top + 48.0f));
    DrawTextLine(L"Upload", small_format_, text_brush_,
                 Rect(mid + 6.0f, rect.top + 34.0f, right, rect.top + 48.0f));
    DrawTextLine(snapshot.network_down_text, body_format_, text_brush_,
                 Rect(left, rect.top + 52.0f, mid - 6.0f, rect.top + 72.0f));
    DrawTextLine(snapshot.network_up_text, body_format_, text_brush_,
                 Rect(mid + 6.0f, rect.top + 52.0f, right, rect.top + 72.0f));

    // Today's cumulative usage (in + out), reset at local-midnight.
    DrawTextLine(snapshot.network_today_text, small_format_,
                 accent_brushes_[AccentNetwork],
                 Rect(left, rect.top + 84.0f, right, rect.bottom - 6.0f));
}

void Renderer::DrawInfoCard(const D2D1_RECT_F& rect, const wchar_t* heading, const wchar_t* value,
                            const wchar_t* detail, const wchar_t* footer, ID2D1Brush* accent) {
    DrawRoundedCard(rect);
    const float left = rect.left + 14.0f;
    DrawTextLine(heading, muted_format_, accent,
                 Rect(left, rect.top + 12.0f, rect.right - 12.0f, rect.top + 29.0f));
    DrawTextLine(value, heading_format_, text_brush_,
                 Rect(left, rect.top + 34.0f, rect.right - 12.0f, rect.top + 56.0f));

    if (footer && footer[0] != L'\0') {
        DrawTextLine(detail, body_format_, muted_brush_,
                     Rect(left, rect.top + 58.0f, rect.right - 12.0f, rect.top + 75.0f));
        DrawTextLine(footer, muted_format_, muted_brush_,
                     Rect(left, rect.bottom - 21.0f, rect.right - 12.0f, rect.bottom - 5.0f));
    } else {
        DrawTextLine(detail, body_format_, muted_brush_,
                     Rect(left, rect.top + 62.0f, rect.right - 12.0f, rect.bottom - 8.0f));
    }
}

void Renderer::DrawProcessCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    const float left = rect.left + 14.0f;
    const float right = rect.right - 14.0f;
    ID2D1Brush* accent = accent_brushes_[AccentBattery];  // #00E676 green

    // Header in green.
    DrawTextLine(L"TOP PROCESSES", muted_format_, accent,
                 Rect(left, rect.top + 12.0f, right, rect.top + 29.0f));

    // Two processes stacked vertically — each entry: bold name (white) +
    // CPU icon + cpu% + RAM icon + ram value (all in green).
    const float row_top[2] = {rect.top + 42.0f, rect.top + 100.0f};

    // Inline layout within the detail row. Numbers tuned for a half-card
    // (~157 px text width). MDL2 icons E950=Processor, E88E=Memory.
    constexpr float kIconW    = 16.0f;
    constexpr float kCpuValW  = 32.0f;   // "100%" max
    constexpr float kGapCol   = 8.0f;    // gap between CPU and RAM blocks
    constexpr float kIconGap  = 3.0f;    // gap between icon and its value

    for (int i = 0; i < 2; ++i) {
        // Process name (white, bold).
        DrawTextLine(snapshot.top_process_name[i], body_format_, text_brush_,
                     Rect(left, row_top[i], right, row_top[i] + 22.0f));

        const float dy_top = row_top[i] + 24.0f;
        const float dy_bot = row_top[i] + 44.0f;

        // ── CPU icon (E950 Processor) + percent ─────────────────────
        float x = left;
        DrawTextLine(L"", mdl2_format_, accent,
                     Rect(x, dy_top - 1.0f, x + kIconW, dy_bot));
        x += kIconW + kIconGap;
        DrawTextLine(snapshot.top_process_cpu[i], small_format_, accent,
                     Rect(x, dy_top, x + kCpuValW, dy_bot));
        x += kCpuValW + kGapCol;

        // ── RAM icon (EEA1 Memory) + value ──────────────────────────
        DrawTextLine(L"", mdl2_format_, accent,
                     Rect(x, dy_top - 1.0f, x + kIconW, dy_bot));
        x += kIconW + kIconGap;
        DrawTextLine(snapshot.top_process_ram[i], small_format_, accent,
                     Rect(x, dy_top, right, dy_bot));
    }
}

void Renderer::DrawUptimeCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    const float left = rect.left + 12.0f;
    const float right = rect.right - 12.0f;
    const float total_w = right - left;

    // Unequal column widths — left half holds the long uptime/boot strings,
    // right half holds short percentages. With equal widths the percentages
    // would float against a wide empty column ("heavy left, empty right").
    constexpr float kFracUptime = 0.28f;
    constexpr float kFracBoot   = 0.32f;
    constexpr float kFracVol    = 0.20f;
    constexpr float kFracBrt    = 0.20f;

    const float x0 = left;
    const float x1 = x0 + total_w * kFracUptime;
    const float x2 = x1 + total_w * kFracBoot;
    const float x3 = x2 + total_w * kFracVol;
    const float x4 = right;
    constexpr float kGap = 4.0f;
    (void)kFracBrt;  // implied by x4 = right

    // ─── Column 1: UPTIME (left-aligned, fills column) ──────────────
    DrawTextLine(L"UPTIME", muted_format_, accent_brushes_[AccentUptime],
                 Rect(x0, rect.top + 8.0f, x1 - kGap, rect.top + 24.0f));
    DrawTextLine(snapshot.uptime_text, body_format_, text_brush_,
                 Rect(x0, rect.top + 26.0f, x1 - kGap, rect.top + 48.0f));

    // ─── Column 2: BOOT (left-aligned, fills column) ────────────────
    DrawTextLine(L"BOOT", muted_format_, accent_brushes_[AccentUptime],
                 Rect(x1, rect.top + 8.0f, x2 - kGap, rect.top + 24.0f));
    DrawTextLine(snapshot.boot_text, body_format_, text_brush_,
                 Rect(x1, rect.top + 26.0f, x2 - kGap, rect.top + 48.0f));

    // ─── Column 3: VOLUME (icon + % centered in narrower column) ────
    // U+E767 in Segoe MDL2 Assets = "Volume" speaker glyph.
    DrawTextLine(L"", mdl2_center_format_, accent_brushes_[AccentUptime],
                 Rect(x2, rect.top + 6.0f, x3 - kGap, rect.top + 26.0f));
    wchar_t vol_text[8];
    if (snapshot.volume_percent >= 0) {
        swprintf_s(vol_text, L"%d%%", snapshot.volume_percent);
    } else {
        wcscpy_s(vol_text, L"--");
    }
    DrawTextLine(vol_text, body_center_format_, text_brush_,
                 Rect(x2, rect.top + 26.0f, x3 - kGap, rect.top + 48.0f));

    // ─── Column 4: BRIGHTNESS (icon + % centered) ───────────────────
    // U+E706 in Segoe MDL2 Assets = "Brightness" sun glyph.
    DrawTextLine(L"", mdl2_center_format_, accent_brushes_[AccentUptime],
                 Rect(x3, rect.top + 6.0f, x4, rect.top + 26.0f));
    wchar_t brt_text[8];
    if (snapshot.brightness_percent >= 0) {
        swprintf_s(brt_text, L"%d%%", snapshot.brightness_percent);
    } else {
        wcscpy_s(brt_text, L"--");
    }
    DrawTextLine(brt_text, body_center_format_, text_brush_,
                 Rect(x3, rect.top + 26.0f, x4, rect.top + 48.0f));
}

void Renderer::DrawGaugeCard(const D2D1_RECT_F& rect, const wchar_t* label, float value,
                             const wchar_t* text, ID2D1Brush* accent) {
    DrawRoundedCard(rect);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = rect.top + 40.0f;
    const float radius = 23.0f;
    const float stroke = 5.0f;

    // Unfilled portion of the gauge ring is white (per user request — makes
    // the colored arc pop against a bright track).
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), text_brush_, stroke);

    const float start = -90.0f;
    const float end = start + 360.0f * value;
    const float start_rad = start * 3.14159265f / 180.0f;
    const float end_rad = end * 3.14159265f / 180.0f;

    ID2D1PathGeometry* geometry = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(factory_->CreatePathGeometry(&geometry)) && SUCCEEDED(geometry->Open(&sink))) {
        sink->BeginFigure(
            D2D1::Point2F(cx + std::cos(start_rad) * radius, cy + std::sin(start_rad) * radius),
            D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(
            D2D1::Point2F(cx + std::cos(end_rad) * radius, cy + std::sin(end_rad) * radius),
            D2D1::SizeF(radius, radius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
            value > 0.5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        target_->DrawGeometry(geometry, accent, stroke);
    }
    SafeRelease(&sink);
    SafeRelease(&geometry);

    DrawTextLine(text, gauge_format_, text_brush_,
                 Rect(rect.left + 8.0f, rect.top + 31.0f, rect.right - 8.0f, rect.top + 51.0f));
    // Label (CPU/RAM/BAT/TEMP) — white per user request.
    DrawTextLine(label, gauge_format_, text_brush_,
                 Rect(rect.left + 8.0f, rect.bottom - 25.0f, rect.right - 8.0f, rect.bottom - 7.0f));
}

void Renderer::DrawStorageCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    const float left = rect.left + 14.0f;
    const float right = rect.right - 14.0f;

    DrawTextLine(L"STORAGE", muted_format_, accent_brushes_[AccentStorage],
                 Rect(left, rect.top + 12.0f, right, rect.top + 29.0f));
    const int rows = snapshot.storage_count > 0 ? snapshot.storage_count : 1;
    for (int i = 0; i < rows && i < 2; ++i) {
        const float top = rect.top + 38.0f + i * 48.0f;
        DrawTextLine(snapshot.storage_label[i], body_format_, text_brush_,
                     Rect(left, top, right - 52.0f, top + 18.0f));
        DrawTextLine(snapshot.storage_percent_text[i], small_format_, muted_brush_,
                     Rect(right - 56.0f, top + 1.0f, right, top + 17.0f));
        DrawBar(Rect(left, top + 23.0f, right, top + 29.0f), snapshot.storage_usage[i],
                accent_brushes_[AccentStorage]);
        DrawTextLine(snapshot.storage_detail[i], small_format_, muted_brush_,
                     Rect(left, top + 34.0f, right, top + 48.0f));
    }
}

void Renderer::DrawMusicCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    // Wide card with two columns:
    //   Left  → status header, title, artist, time, progress bar
    //   Right → visualizer fills the column
    const float left = rect.left + 14.0f;
    const float right = rect.right - 14.0f;
    const float mid = rect.left + (rect.right - rect.left) * 0.50f;
    const float col_left_right = mid - 6.0f;
    const float col_right_left = mid + 6.0f;

    const wchar_t* header = snapshot.music_status[0] ? snapshot.music_status : L"♪ MUSIC";
    DrawTextLine(header, muted_format_, accent_brushes_[AccentMusic],
                 Rect(left, rect.top + 8.0f, col_left_right, rect.top + 24.0f));
    // ── Music title: marquee-scroll when it overflows the column ──
    {
        const float title_top    = rect.top + 26.0f;
        const float title_bottom = rect.top + 44.0f;
        const float visible_w    = col_left_right - left;

        // Measure the title at body_format_ to know whether it overflows.
        IDWriteTextLayout* layout = nullptr;
        const UINT32 len = static_cast<UINT32>(std::wcslen(snapshot.music_title));
        if (write_factory_ && len > 0) {
            write_factory_->CreateTextLayout(snapshot.music_title, len,
                                             body_format_, 10000.0f, 100.0f, &layout);
        }
        DWRITE_TEXT_METRICS m{};
        if (layout) layout->GetMetrics(&m);
        const float text_w = m.widthIncludingTrailingWhitespace;
        SafeRelease(&layout);

        // Reset scroll position when the track changes.
        if (std::wcscmp(snapshot.music_title, title_prev_) != 0) {
            title_scroll_offset_ = 0.0f;
            wcsncpy_s(title_prev_, _countof(title_prev_), snapshot.music_title, _TRUNCATE);
        }

        if (text_w <= visible_w || len == 0) {
            // Fits — draw normally, keep offset at 0.
            title_scroll_offset_ = 0.0f;
            DrawTextLine(snapshot.music_title, body_format_, text_brush_,
                         Rect(left, title_top, col_left_right, title_bottom));
        } else {
            // Advance offset only while Playing. Pause halts the scroll
            // (last_tick → 0 sentinel) so the title doesn't jump when
            // playback resumes after a long gap.
            const ULONGLONG now = GetTickCount64();
            if (snapshot.music_playing) {
                if (title_scroll_last_tick_ != 0) {
                    const float dt_s = (now - title_scroll_last_tick_) / 1000.0f;
                    if (dt_s < 1.0f) {  // clamp on long redraw gaps
                        title_scroll_offset_ += dt_s * 25.0f;  // 25 px/s — "lambat"
                    }
                }
                title_scroll_last_tick_ = now;
            } else {
                title_scroll_last_tick_ = 0;
            }

            const float gap = 40.0f;
            const float wrap = text_w + gap;
            if (title_scroll_offset_ > wrap) {
                title_scroll_offset_ -= wrap;
            }

            // Clip strictly to the title column so the second copy doesn't
            // bleed into the visualizer column or status row.
            target_->PushAxisAlignedClip(
                Rect(left, title_top, col_left_right, title_bottom),
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            const float draw_x = left - title_scroll_offset_;
            DrawTextLine(snapshot.music_title, body_format_, text_brush_,
                         Rect(draw_x, title_top, draw_x + text_w + 4.0f, title_bottom));
            // Second copy starts after `wrap` so the scroll looks seamless
            // — by the time the first copy is fully off-screen left, this
            // one is at the original position.
            const float draw_x2 = draw_x + wrap;
            DrawTextLine(snapshot.music_title, body_format_, text_brush_,
                         Rect(draw_x2, title_top, draw_x2 + text_w + 4.0f, title_bottom));

            target_->PopAxisAlignedClip();
        }
    }

    DrawTextLine(snapshot.music_artist, small_format_, muted_brush_,
                 Rect(left, rect.top + 46.0f, col_left_right, rect.top + 60.0f));

    wchar_t time_text[48]{};
    const auto pos = static_cast<int>(snapshot.music_position);
    const auto dur = static_cast<int>(snapshot.music_duration);
    swprintf_s(time_text, L"%d:%02d / %d:%02d", pos / 60, pos % 60, dur / 60, dur % 60);
    DrawTextLine(time_text, small_format_, muted_brush_,
                 Rect(left, rect.top + 62.0f, col_left_right, rect.top + 76.0f));

    const float progress = snapshot.music_duration > 0.0
                               ? static_cast<float>(snapshot.music_position / snapshot.music_duration)
                               : 0.0f;
    DrawBar(Rect(left, rect.top + 78.0f, col_left_right, rect.top + 84.0f), progress,
            accent_brushes_[AccentMusic]);

    // Right column: visualizer fills the column from top to bottom.
    DrawVisualizer(Rect(col_right_left, rect.top + 8.0f, right, rect.bottom - 8.0f),
                   snapshot, accent_brushes_[AccentMusic]);
}

void Renderer::DrawBar(const D2D1_RECT_F& rect, float value, ID2D1Brush* accent) {
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 3.0f, 3.0f), track_brush_);
    const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    if (clamped <= 0.0f) {
        return;
    }
    D2D1_RECT_F fill = rect;
    fill.right = fill.left + (rect.right - rect.left) * clamped;
    target_->FillRoundedRectangle(D2D1::RoundedRect(fill, 3.0f, 3.0f), accent);
}

void Renderer::DrawVisualizer(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot,
                              ID2D1Brush* accent) {
    const int bars = WidgetSnapshot::VisualizerBars;
    const float gap = 3.0f;
    const float width = (rect.right - rect.left - gap * (bars - 1)) / bars;
    const float max_height = rect.bottom - rect.top;

    for (int i = 0; i < bars; ++i) {
        float level = snapshot.music_levels[i];
        if (level <= 0.0f) {
            level = snapshot.music_playing ? 0.24f : 0.10f;
        }
        level = std::pow(level > 1.0f ? 1.0f : level, 0.72f);
        const float bar_height = max_height * (0.12f + level * 0.88f);
        const float x0 = rect.left + i * (width + gap);
        const float y0 = rect.bottom - bar_height;
        target_->FillRoundedRectangle(
            D2D1::RoundedRect(Rect(x0, y0, x0 + width, rect.bottom), 2.0f, 2.0f), accent);
    }
}
