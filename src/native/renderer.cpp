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
} // namespace

Renderer::~Renderer() {
    DiscardDeviceResources();
    SafeRelease(&small_format_);
    SafeRelease(&gauge_format_);
    SafeRelease(&muted_format_);
    SafeRelease(&body_format_);
    SafeRelease(&heading_format_);
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

    if (!CreateTextFormat(32.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &clock_format_)) {
        return false;
    }
    if (!CreateTextFormat(15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &heading_format_)) {
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

    body_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    muted_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    heading_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    clock_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    gauge_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    small_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    gauge_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    gauge_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    return true;
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
            SafeRelease(&accent_brush_);
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

    DrawClockCard(Rect(x, y, width, y + 90.0f), snapshot);
    y += 90.0f + gap;

    const float half = (width - gap) / 2.0f;
    DrawWeatherCard(Rect(x, y, half, y + 96.0f), snapshot);
    DrawNetworkCard(Rect(half + gap, y, width, y + 96.0f), snapshot);
    y += 96.0f + gap;

    const float mini_gap = 8.0f;
    const float mini = (width - mini_gap * 3.0f) / 4.0f;
    DrawGaugeCard(Rect(x, y, x + mini, y + 92.0f), L"CPU", snapshot.cpu_usage, snapshot.cpu_text);
    DrawGaugeCard(Rect(x + (mini + mini_gap), y, x + mini * 2.0f + mini_gap, y + 92.0f), L"RAM", snapshot.ram_usage, snapshot.ram_text);
    DrawGaugeCard(Rect(x + (mini + mini_gap) * 2.0f, y, x + mini * 3.0f + mini_gap * 2.0f, y + 92.0f), L"BAT", snapshot.battery_usage, snapshot.battery_text);
    DrawGaugeCard(Rect(x + (mini + mini_gap) * 3.0f, y, width, y + 92.0f), L"TEMP", snapshot.temperature_usage, snapshot.temperature_text);
    y += 92.0f + gap;

    DrawInfoCard(Rect(x, y, width, y + 88.0f), L"TOP PROCESS", L"sysmon-widget.exe", L"CPU 0.2% / RAM 42 MB", L"");
    y += 88.0f + gap;

    DrawStorageCard(Rect(x, y, half, y + 158.0f), snapshot);
    DrawMusicCard(Rect(half + gap, y, width, y + 158.0f), snapshot);

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

    if (!EnsureBackBuffer()) {
        return false;
    }

    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    if (FAILED(factory_->CreateDCRenderTarget(&properties, &target_))) {
        return false;
    }

    if (FAILED(target_->BindDC(memory_dc_, &client))) {
        return false;
    }

    if (FAILED(target_->CreateSolidColorBrush(Color(28, 31, 27, 0.72f), &panel_brush_))) {
        return false;
    }
    if (FAILED(target_->CreateSolidColorBrush(Color(237, 165, 72), &accent_brush_))) {
        return false;
    }
    if (FAILED(target_->CreateSolidColorBrush(Color(238, 241, 232), &text_brush_))) {
        return false;
    }
    if (FAILED(target_->CreateSolidColorBrush(Color(157, 166, 148), &muted_brush_))) {
        return false;
    }
    if (FAILED(target_->CreateSolidColorBrush(Color(63, 69, 58), &track_brush_))) {
        return false;
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
    SafeRelease(&accent_brush_);
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
    DrawTextLine(snapshot.time, clock_format_, text_brush_, Rect(rect.left + 16.0f, rect.top + 11.0f, rect.right - 16.0f, rect.top + 52.0f));
    DrawTextLine(snapshot.date, body_format_, accent_brush_, Rect(rect.left + 17.0f, rect.top + 55.0f, rect.right - 16.0f, rect.bottom - 12.0f));
}

void Renderer::DrawWeatherCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    const float left = rect.left + 14.0f;
    const float top = rect.top + 12.0f;
    const float split = rect.left + 72.0f;

    DrawTextLine(L"WEATHER", muted_format_, accent_brush_, Rect(left, top, rect.right - 12.0f, top + 17.0f));
    DrawTextLine(snapshot.weather_temp, heading_format_, text_brush_, Rect(left, rect.top + 43.0f, split - 4.0f, rect.top + 69.0f));
    DrawTextLine(snapshot.weather_city, body_format_, text_brush_, Rect(split, rect.top + 32.0f, rect.right - 12.0f, rect.top + 50.0f));
    DrawTextLine(snapshot.weather_detail, small_format_, muted_brush_, Rect(split, rect.top + 55.0f, rect.right - 12.0f, rect.top + 70.0f));
    DrawTextLine(snapshot.weather_meta, small_format_, muted_brush_, Rect(split, rect.top + 73.0f, rect.right - 12.0f, rect.bottom - 8.0f));
}

void Renderer::DrawNetworkCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    const float left = rect.left + 14.0f;
    const float mid = rect.left + (rect.right - rect.left) / 2.0f;
    const float top = rect.top + 12.0f;

    DrawTextLine(L"NETWORK", muted_format_, accent_brush_, Rect(left, top, rect.right - 12.0f, top + 17.0f));
    DrawTextLine(L"DOWN", small_format_, muted_brush_, Rect(left, rect.top + 39.0f, mid - 6.0f, rect.top + 55.0f));
    DrawTextLine(L"UP", small_format_, muted_brush_, Rect(mid + 6.0f, rect.top + 39.0f, rect.right - 12.0f, rect.top + 55.0f));
    DrawTextLine(snapshot.network_down_text, body_format_, text_brush_, Rect(left, rect.top + 58.0f, mid - 6.0f, rect.bottom - 10.0f));
    DrawTextLine(snapshot.network_up_text, body_format_, text_brush_, Rect(mid + 6.0f, rect.top + 58.0f, rect.right - 12.0f, rect.bottom - 10.0f));
}

void Renderer::DrawInfoCard(const D2D1_RECT_F& rect, const wchar_t* heading, const wchar_t* value,
                            const wchar_t* detail, const wchar_t* footer) {
    DrawRoundedCard(rect);
    const float left = rect.left + 14.0f;
    DrawTextLine(heading, muted_format_, accent_brush_, Rect(left, rect.top + 12.0f, rect.right - 12.0f, rect.top + 29.0f));
    DrawTextLine(value, heading_format_, text_brush_, Rect(left, rect.top + 34.0f, rect.right - 12.0f, rect.top + 56.0f));

    if (footer && footer[0] != L'\0') {
        DrawTextLine(detail, body_format_, muted_brush_, Rect(left, rect.top + 58.0f, rect.right - 12.0f, rect.top + 75.0f));
        DrawTextLine(footer, muted_format_, muted_brush_, Rect(left, rect.bottom - 21.0f, rect.right - 12.0f, rect.bottom - 5.0f));
    } else {
        DrawTextLine(detail, body_format_, muted_brush_, Rect(left, rect.top + 62.0f, rect.right - 12.0f, rect.bottom - 8.0f));
    }
}

void Renderer::DrawGaugeCard(const D2D1_RECT_F& rect, const wchar_t* label, float value, const wchar_t* text) {
    DrawRoundedCard(rect);
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = rect.top + 40.0f;
    const float radius = 23.0f;
    const float stroke = 5.0f;

    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), track_brush_, stroke);

    const float start = -90.0f;
    const float end = start + 360.0f * value;
    const float start_rad = start * 3.14159265f / 180.0f;
    const float end_rad = end * 3.14159265f / 180.0f;

    ID2D1PathGeometry* geometry = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(factory_->CreatePathGeometry(&geometry)) && SUCCEEDED(geometry->Open(&sink))) {
        sink->BeginFigure(D2D1::Point2F(cx + std::cos(start_rad) * radius, cy + std::sin(start_rad) * radius),
                          D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(
            D2D1::Point2F(cx + std::cos(end_rad) * radius, cy + std::sin(end_rad) * radius),
            D2D1::SizeF(radius, radius), 0.0f,
            value > 0.5f ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_CLOCKWISE,
            value > 0.5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        target_->DrawGeometry(geometry, accent_brush_, stroke);
    }
    SafeRelease(&sink);
    SafeRelease(&geometry);

    DrawTextLine(text, gauge_format_, text_brush_, Rect(rect.left + 8.0f, rect.top + 31.0f, rect.right - 8.0f, rect.top + 51.0f));
    DrawTextLine(label, gauge_format_, muted_brush_, Rect(rect.left + 8.0f, rect.bottom - 25.0f, rect.right - 8.0f, rect.bottom - 7.0f));
}

void Renderer::DrawStorageCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    const float left = rect.left + 14.0f;
    const float right = rect.right - 14.0f;

    DrawTextLine(L"STORAGE", muted_format_, accent_brush_, Rect(left, rect.top + 12.0f, right, rect.top + 29.0f));
    const int rows = snapshot.storage_count > 0 ? snapshot.storage_count : 1;
    for (int i = 0; i < rows && i < 2; ++i) {
        const float top = rect.top + 38.0f + i * 48.0f;
        DrawTextLine(snapshot.storage_label[i], body_format_, text_brush_, Rect(left, top, right - 52.0f, top + 18.0f));
        DrawTextLine(snapshot.storage_percent_text[i], small_format_, muted_brush_, Rect(right - 56.0f, top + 1.0f, right, top + 17.0f));
        DrawBar(Rect(left, top + 23.0f, right, top + 29.0f), snapshot.storage_usage[i]);
        DrawTextLine(snapshot.storage_detail[i], small_format_, muted_brush_, Rect(left, top + 34.0f, right, top + 48.0f));
    }
}

void Renderer::DrawMusicCard(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
    DrawRoundedCard(rect);
    const float left = rect.left + 14.0f;
    const float right = rect.right - 14.0f;

    DrawTextLine(L"MUSIC", muted_format_, accent_brush_, Rect(left, rect.top + 12.0f, right, rect.top + 29.0f));
    DrawTextLine(snapshot.music_title, body_format_, text_brush_, Rect(left, rect.top + 34.0f, right, rect.top + 52.0f));
    DrawTextLine(snapshot.music_artist, small_format_, muted_brush_, Rect(left, rect.top + 55.0f, right, rect.top + 70.0f));

    wchar_t time_text[48]{};
    const auto pos = static_cast<int>(snapshot.music_position);
    const auto dur = static_cast<int>(snapshot.music_duration);
    swprintf_s(time_text, L"%d:%02d / %d:%02d", pos / 60, pos % 60, dur / 60, dur % 60);
    DrawTextLine(time_text, small_format_, muted_brush_, Rect(left, rect.top + 73.0f, right, rect.top + 88.0f));

    const float progress = snapshot.music_duration > 0.0
                               ? static_cast<float>(snapshot.music_position / snapshot.music_duration)
                               : 0.0f;
    DrawBar(Rect(left, rect.top + 92.0f, right, rect.top + 96.0f), progress);
    DrawVisualizer(Rect(left, rect.top + 108.0f, right, rect.bottom - 12.0f), snapshot);
}

void Renderer::DrawBar(const D2D1_RECT_F& rect, float value) {
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 3.0f, 3.0f), track_brush_);
    const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    if (clamped <= 0.0f) {
        return;
    }
    D2D1_RECT_F fill = rect;
    fill.right = fill.left + (rect.right - rect.left) * clamped;
    target_->FillRoundedRectangle(D2D1::RoundedRect(fill, 3.0f, 3.0f), accent_brush_);
}

void Renderer::DrawVisualizer(const D2D1_RECT_F& rect, const WidgetSnapshot& snapshot) {
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
        target_->FillRoundedRectangle(D2D1::RoundedRect(Rect(x0, y0, x0 + width, rect.bottom), 2.0f, 2.0f), accent_brush_);
    }
}
