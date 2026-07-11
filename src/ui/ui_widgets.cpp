// UI 控件库实现：Direct2D 自绘控件，复刻 Flutter Material 3 风格
#include "ui/ui_widgets.h"
#include <d2d1_1.h>   // ID2D1DeviceContext（VideoView 跨设备贴图所需）
#include <algorithm>
#include <cmath>
#include <chrono>

namespace meplayer {

// ============================================================
// 静态成员定义
// ============================================================
Widget* FocusManager::focused_ = nullptr;
InteractiveWidget* HoverTracker::current_ = nullptr;

// HoverTracker 实现（此处 InteractiveWidget 已完整）
void HoverTracker::setHovered(InteractiveWidget* w) {
    if (current_ && current_ != w) current_->onMouseLeave();
    current_ = w;
}
void HoverTracker::clear() {
    if (current_) { current_->onMouseLeave(); current_ = nullptr; }
}

// ============================================================
// 共享绘制辅助
// ============================================================

// 填充圆角矩形（radius=0 时即普通矩形）
static void fillRoundedRect(ID2D1RenderTarget* rt, const D2D1_RECT_F& r,
                            float radius, D2D1_COLOR_F c) {
    auto* b = BrushCache::get(rt, c);
    rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
}

// 绘制圆角矩形边框
static void drawRoundedRectBorder(ID2D1RenderTarget* rt, const D2D1_RECT_F& r,
                                  float radius, D2D1_COLOR_F c, float width) {
    auto* b = BrushCache::get(rt, c);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b, width);
}

// 填充圆形（给定中心 + 半径）
static void fillCircle(ID2D1RenderTarget* rt, float cx, float cy, float r,
                       D2D1_COLOR_F c) {
    auto* b = BrushCache::get(rt, c);
    D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);
    rt->FillEllipse(e, b);
}

// 判断点是否在矩形内
static inline bool pointInRect(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

// ============================================================
// ThemeManager
// ============================================================
ThemeManager::ThemeManager() {
    scheme_ = Theme::fromSeed(Theme::kSeedBlue, false);
}
ThemeManager& ThemeManager::instance() {
    static ThemeManager inst;
    return inst;
}
void ThemeManager::update(uint32_t seed, bool dark) {
    scheme_ = Theme::fromSeed(seed, dark);
}
ColorScheme& ThemeManager::current() { return scheme_; }

// ============================================================
// Text
// ============================================================
Text::Text(std::wstring t, float size, D2D1_COLOR_F c, DWRITE_FONT_WEIGHT w)
    : text(std::move(t)), fontSize(size), color(c), weight(w) {}

void Text::ensureLayout(float maxWidth, float maxHeight) {
    float mw = maxWidth > 0 ? maxWidth : 10000.0f;
    float mh = maxHeight > 0 ? maxHeight : 10000.0f;
    if (layout_ && std::fabs(layoutMaxW_ - mw) < 1.0f) return;
    auto* dw = WindowManager::instance().dwFactory();
    if (!dw) return;
    IDWriteTextFormat* fmt = TextFormatCache::get(dw, fontSize,
                                                  L"Microsoft YaHei", weight);
    layout_.Reset();
    dw->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                         fmt, mw, mh, layout_.GetAddressOf());
    if (layout_) {
        layout_->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP
                                      : DWRITE_WORD_WRAPPING_NO_WRAP);
        layout_->SetTextAlignment(textAlignment);
        layoutMaxW_ = mw;
    }
}

Size Text::measure(const Size& max) {
    if (text.empty()) { measured_ = {0, 0}; measuredValid_ = true; return measured_; }
    ensureLayout(max.w, max.h);
    if (!layout_) { measured_ = {0, 0}; measuredValid_ = true; return measured_; }
    DWRITE_TEXT_METRICS m{};
    layout_->GetMetrics(&m);
    measured_ = {m.width, m.height};
    measuredValid_ = true;
    return measured_;
}

void Text::layout(const Rect& b) { bounds_ = b; }

void Text::draw(ID2D1RenderTarget* rt) {
    if (text.empty()) return;
    ensureLayout(bounds_.w, bounds_.h);
    if (!layout_) return;
    auto* b = BrushCache::get(rt, color);
    rt->DrawTextLayout(D2D1::Point2F(bounds_.x, bounds_.y),
                       layout_.Get(), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// ============================================================
// Icon —— Direct2D 几何路径自绘
// ============================================================
Icon::Icon(IconKind k, float sz, D2D1_COLOR_F c)
    : kind(k), size(sz), color(c) {}

// 是否为填充型图标（FillGeometry）
static bool iconIsFill(IconKind k) {
    switch (k) {
    case IconKind::Play: case IconKind::Pause:
    case IconKind::Next: case IconKind::Prev:
    case IconKind::Folder: case IconKind::Volume:
        return true;
    default:
        return false;
    }
}
// 是否为描边型图标（DrawGeometry）
static bool iconIsStroke(IconKind k) {
    switch (k) {
    case IconKind::Search: case IconKind::Refresh:
    case IconKind::Add: case IconKind::Close:
    case IconKind::Fullscreen: case IconKind::Sort:
    case IconKind::ChevronRight: case IconKind::ChevronLeft:
    case IconKind::Settings: case IconKind::PictureInPicture:
    case IconKind::Iptv:
        return true;
    default:
        return false;
    }
}

// 按 (kind,size) 缓存路径几何
ComPtr<ID2D1PathGeometry> Icon::geometryFor(IconKind k, float size) {
    int64_t key = static_cast<int64_t>(k) * 1000000LL +
                  static_cast<int64_t>(size * 100);
    static std::map<int64_t, ComPtr<ID2D1PathGeometry>> cache;
    auto it = cache.find(key);
    if (it != cache.end() && it->second) return it->second;

    auto* f = WindowManager::instance().d2dFactory();
    ComPtr<ID2D1PathGeometry> geo;
    if (!f) return geo;
    f->CreatePathGeometry(geo.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink;
    geo->Open(sink.GetAddressOf());
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);

    const float s = size;
    const float m = s * 0.18f;

    auto addRect = [&](float x, float y, float w, float h, D2D1_FIGURE_BEGIN b) {
        D2D1_POINT_2F pts[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
        sink->BeginFigure(pts[0], b);
        sink->AddLines(pts + 1, 3);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    };
    auto addTri = [&](float x1, float y1, float x2, float y2,
                      float x3, float y3, D2D1_FIGURE_BEGIN b) {
        D2D1_POINT_2F pts[3] = {{x1, y1}, {x2, y2}, {x3, y3}};
        sink->BeginFigure(pts[0], b);
        sink->AddLines(pts + 1, 2);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    };
    auto addLine = [&](float x1, float y1, float x2, float y2) {
        sink->BeginFigure(D2D1::Point2F(x1, y1), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(x2, y2));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    };
    auto addCircle = [&](float cx, float cy, float r, D2D1_FIGURE_BEGIN b) {
        sink->BeginFigure(D2D1::Point2F(cx + r, cy), b);
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx - r, cy),
            D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
            D2D1_ARC_SIZE_LARGE));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + r, cy),
            D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
            D2D1_ARC_SIZE_LARGE));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    };
    auto addArcOpen = [&](float x1, float y1, float x2, float y2,
                          float x3, float y3, float x4, float y4) {
        // 三段 90° 弧组成的开放曲线
        sink->BeginFigure(D2D1::Point2F(x1, y1), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x2, y2),
            D2D1::SizeF(s * 0.35f, s * 0.35f), 90,
            D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x3, y3),
            D2D1::SizeF(s * 0.35f, s * 0.35f), 90,
            D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x4, y4),
            D2D1::SizeF(s * 0.35f, s * 0.35f), 90,
            D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    };

    switch (k) {
    case IconKind::Play:
        addTri(m, m, s - m, s / 2, m, s - m, D2D1_FIGURE_BEGIN_FILLED);
        break;
    case IconKind::Pause:
        addRect(s * 0.32f, m, s * 0.14f, s - 2 * m, D2D1_FIGURE_BEGIN_FILLED);
        addRect(s * 0.54f, m, s * 0.14f, s - 2 * m, D2D1_FIGURE_BEGIN_FILLED);
        break;
    case IconKind::Next:
        addTri(m, m, s * 0.62f, s / 2, m, s - m, D2D1_FIGURE_BEGIN_FILLED);
        addRect(s - m - s * 0.14f, m, s * 0.14f, s - 2 * m, D2D1_FIGURE_BEGIN_FILLED);
        break;
    case IconKind::Prev:
        addTri(s - m, m, s * 0.38f, s / 2, s - m, s - m, D2D1_FIGURE_BEGIN_FILLED);
        addRect(m, m, s * 0.14f, s - 2 * m, D2D1_FIGURE_BEGIN_FILLED);
        break;
    case IconKind::Folder:
        sink->BeginFigure(D2D1::Point2F(m, s * 0.30f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(s * 0.40f, s * 0.30f));
        sink->AddLine(D2D1::Point2F(s * 0.52f, s * 0.18f));
        sink->AddLine(D2D1::Point2F(s - m, s * 0.18f));
        sink->AddLine(D2D1::Point2F(s - m, s - m));
        sink->AddLine(D2D1::Point2F(m, s - m));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        break;
    case IconKind::Volume:
        sink->BeginFigure(D2D1::Point2F(m, s * 0.42f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(s * 0.30f, s * 0.42f));
        sink->AddLine(D2D1::Point2F(s * 0.52f, s * 0.22f));
        sink->AddLine(D2D1::Point2F(s * 0.52f, s * 0.78f));
        sink->AddLine(D2D1::Point2F(s * 0.30f, s * 0.58f));
        sink->AddLine(D2D1::Point2F(m, s * 0.58f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        break;
    case IconKind::Search:
        addCircle(s * 0.42f, s * 0.42f, s * 0.20f, D2D1_FIGURE_BEGIN_HOLLOW);
        addLine(s * 0.56f, s * 0.56f, s - m, s - m);
        break;
    case IconKind::Refresh:
        addArcOpen(s * 0.5f, m, s - m, s * 0.5f, s * 0.5f, s - m, m, s * 0.5f);
        addLine(s * 0.5f, m, s * 0.64f, s * 0.12f);   // 箭头
        addLine(s * 0.5f, m, s * 0.40f, s * 0.12f);
        break;
    case IconKind::Add:
        addLine(s * 0.5f, m, s * 0.5f, s - m);
        addLine(m, s * 0.5f, s - m, s * 0.5f);
        break;
    case IconKind::Close:
        addLine(m, m, s - m, s - m);
        addLine(s - m, m, m, s - m);
        break;
    case IconKind::Fullscreen:
        addLine(m, s * 0.32f, m, m);      addLine(m, m, s * 0.32f, m);
        addLine(s - m, s * 0.32f, s - m, m); addLine(s - m, m, s * 0.68f, m);
        addLine(m, s * 0.68f, m, s - m);  addLine(m, s - m, s * 0.32f, s - m);
        addLine(s - m, s * 0.68f, s - m, s - m); addLine(s - m, s - m, s * 0.68f, s - m);
        break;
    case IconKind::Sort:
        addLine(m, s * 0.30f, s * 0.70f, s * 0.30f);
        addLine(m, s * 0.50f, s * 0.82f, s * 0.50f);
        addLine(m, s * 0.70f, s - m, s * 0.70f);
        break;
    case IconKind::ChevronRight:
        sink->BeginFigure(D2D1::Point2F(s * 0.42f, m), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(s - m, s * 0.5f));
        sink->AddLine(D2D1::Point2F(s * 0.42f, s - m));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        break;
    case IconKind::ChevronLeft:
        sink->BeginFigure(D2D1::Point2F(s * 0.58f, m), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(m, s * 0.5f));
        sink->AddLine(D2D1::Point2F(s * 0.58f, s - m));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        break;
    case IconKind::Settings:
        addCircle(s * 0.5f, s * 0.5f, s * 0.30f, D2D1_FIGURE_BEGIN_HOLLOW);
        addCircle(s * 0.5f, s * 0.5f, s * 0.12f, D2D1_FIGURE_BEGIN_HOLLOW);
        addLine(s * 0.5f, m, s * 0.5f, s * 0.20f);
        addLine(s * 0.5f, s - m, s * 0.5f, s * 0.80f);
        addLine(m, s * 0.5f, s * 0.20f, s * 0.5f);
        addLine(s - m, s * 0.5f, s * 0.80f, s * 0.5f);
        break;
    case IconKind::PictureInPicture:
        addRect(m, m, s - 2 * m, s - 2 * m, D2D1_FIGURE_BEGIN_HOLLOW);
        addRect(s * 0.50f, s * 0.50f, s * 0.30f, s * 0.30f, D2D1_FIGURE_BEGIN_HOLLOW);
        break;
    case IconKind::Video:
        addRect(m, s * 0.25f, s - 2 * m, s * 0.55f, D2D1_FIGURE_BEGIN_HOLLOW);
        addTri(s * 0.42f, s * 0.36f, s * 0.42f, s * 0.64f,
               s * 0.64f, s * 0.50f, D2D1_FIGURE_BEGIN_FILLED);
        break;
    case IconKind::Music:
    case IconKind::MusicNote:
        addCircle(s * 0.40f, s * 0.62f, s * 0.12f, D2D1_FIGURE_BEGIN_FILLED);
        addLine(s * 0.52f, s * 0.62f, s * 0.52f, s * 0.20f);
        break;
    case IconKind::Iptv:
        addRect(m, s * 0.30f, s - 2 * m, s * 0.55f, D2D1_FIGURE_BEGIN_HOLLOW);
        addLine(s * 0.40f, s * 0.30f, s * 0.30f, m);
        addLine(s * 0.60f, s * 0.30f, s * 0.70f, m);
        break;
    }

    sink->Close();
    cache[key] = geo;
    return geo;
}

Size Icon::measure(const Size& /*max*/) { return {size, size}; }

void Icon::draw(ID2D1RenderTarget* rt) {
    auto geo = geometryFor(kind, size);
    if (!geo) return;
    // 居中放置到 bounds_
    float ox = bounds_.x + (bounds_.w - size) / 2.0f;
    float oy = bounds_.y + (bounds_.h - size) / 2.0f;
    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(ox, oy));
    auto* b = BrushCache::get(rt, color);
    if (iconIsFill(kind)) rt->FillGeometry(geo.Get(), b);
    if (iconIsStroke(kind)) rt->DrawGeometry(geo.Get(), b, std::max(1.0f, size * 0.08f));
    // 混合型（Video/Music/MusicNote）：填充 + 描边都画
    if (kind == IconKind::Video || kind == IconKind::Music ||
        kind == IconKind::MusicNote) {
        rt->FillGeometry(geo.Get(), b);
        rt->DrawGeometry(geo.Get(), b, std::max(1.0f, size * 0.06f));
    }
    rt->SetTransform(old);
}

// ============================================================
// ImageIcon（WIC 解码 → ID2D1Bitmap）
// ============================================================
ImageIcon::ImageIcon(std::wstring p, float sz) : path(std::move(p)), size(sz) {}

bool ImageIcon::ensureBitmap(ID2D1RenderTarget* rt) {
    if (bitmap_ || tried_) return bitmap_.Get();
    tried_ = true;
    if (path.empty()) return false;

    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));
    if (FAILED(hr) || !wic) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr) || !decoder) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr) || !frame) return false;

    ComPtr<IWICFormatConverter> conv;
    hr = wic->CreateFormatConverter(conv.GetAddressOf());
    if (FAILED(hr) || !conv) return false;
    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) return false;

    rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, bitmap_.GetAddressOf());
    return bitmap_.Get();
}

Size ImageIcon::measure(const Size& /*max*/) { return {size, size}; }

void ImageIcon::draw(ID2D1RenderTarget* rt) {
    if (ensureBitmap(rt) && bitmap_) {
        rt->DrawBitmap(bitmap_.Get(), bounds_.d2d(), 1.0f,
                       D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        // 回退：灰色圆角矩形 + Music 图标占位
        auto& sc = ThemeManager::instance().current();
        fillRoundedRect(rt, bounds_.d2d(), 6, sc.surfaceVariant);
        Icon placeholder(IconKind::Music, size * 0.5f, sc.onSurfaceVariant);
        placeholder.layout(bounds_);
        placeholder.draw(rt);
    }
}

// ============================================================
// IconButton
// ============================================================
IconButton::IconButton(IconKind k, std::function<void()> cb)
    : onClick(std::move(cb)), iconKind(k) {
    auto& sc = ThemeManager::instance().current();
    iconColor = sc.onSurface;
}

Size IconButton::measure(const Size& /*max*/) { return {buttonSize, buttonSize}; }

void IconButton::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    // 悬停/按下背景
    if (hovered_ || pressed_) {
        float a = pressed_ ? 0.12f : 0.08f;
        D2D1_COLOR_F bg = withAlpha(sc.onSurface, a);
        if (circular) {
            fillCircle(rt, bounds_.x + bounds_.w / 2,
                       bounds_.y + bounds_.h / 2, bounds_.w / 2, bg);
        } else {
            fillRoundedRect(rt, bounds_.d2d(), 8, bg);
        }
    }
    // 图标
    Icon ic(iconKind, iconSize, iconColor);
    Rect ib = {bounds_.x + (bounds_.w - iconSize) / 2,
               bounds_.y + (bounds_.h - iconSize) / 2,
               iconSize, iconSize};
    ic.layout(ib);
    ic.draw(rt);
}

Widget* IconButton::hitTest(float x, float y) {
    if (pointInRect(x, y, bounds_.d2d())) return this;
    return nullptr;
}
bool IconButton::onMouseDown(const MouseEvent&) { pressed_ = true; markDirty(); return true; }
bool IconButton::onMouseUp(const MouseEvent&) {
    bool wasPressed = pressed_;
    pressed_ = false;
    markDirty();
    if (wasPressed && onClick) onClick();
    return true;
}
bool IconButton::onMouseMove(const MouseEvent&) { handleHover(); return true; }

// ============================================================
// TextButton
// ============================================================
TextButton::TextButton(std::wstring text, std::function<void()> cb)
    : onClick(std::move(cb)), label(std::move(text)) {}

Size TextButton::measure(const Size& max) {
    Text t(label, fontSize);
    Size s = t.measure({max.w, max.h});
    measured_ = {s.w + 24, std::max(s.h + 10, 36.0f)};
    return measured_;
}

void TextButton::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    if (hovered_ || pressed_) {
        float a = pressed_ ? 0.12f : 0.08f;
        fillRoundedRect(rt, bounds_.d2d(), 18, withAlpha(sc.primary, a));
    }
    Text t(label, fontSize, pressed_ ? sc.primary : sc.onSurface);
    Size s = t.measure({bounds_.w, bounds_.h});
    Rect tr = {bounds_.x + (bounds_.w - s.w) / 2,
               bounds_.y + (bounds_.h - s.h) / 2, s.w, s.h};
    t.layout(tr);
    t.draw(rt);
}

Widget* TextButton::hitTest(float x, float y) {
    return pointInRect(x, y, bounds_.d2d()) ? this : nullptr;
}
bool TextButton::onMouseDown(const MouseEvent&) { pressed_ = true; markDirty(); return true; }
bool TextButton::onMouseUp(const MouseEvent&) {
    bool w = pressed_; pressed_ = false; markDirty();
    if (w && onClick) onClick();
    return true;
}
bool TextButton::onMouseMove(const MouseEvent&) { handleHover(); return true; }

// ============================================================
// FilledButton
// ============================================================
FilledButton::FilledButton(std::wstring text, std::function<void()> cb)
    : onClick(std::move(cb)), label(std::move(text)) {}

Size FilledButton::measure(const Size& max) {
    Text t(label, fontSize);
    Size s = t.measure({max.w, max.h});
    return {s.w + 32, std::max(s.h + 14, 40.0f)};
}

void FilledButton::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    D2D1_COLOR_F bg = pressed_ ? withAlpha(sc.primary, 0.85f) : sc.primary;
    fillRoundedRect(rt, bounds_.d2d(), 20, bg);
    Text t(label, fontSize, sc.onPrimary, DWRITE_FONT_WEIGHT_MEDIUM);
    Size s = t.measure({bounds_.w, bounds_.h});
    Rect tr = {bounds_.x + (bounds_.w - s.w) / 2,
               bounds_.y + (bounds_.h - s.h) / 2, s.w, s.h};
    t.layout(tr);
    t.draw(rt);
}

Widget* FilledButton::hitTest(float x, float y) {
    return pointInRect(x, y, bounds_.d2d()) ? this : nullptr;
}
bool FilledButton::onMouseDown(const MouseEvent&) { pressed_ = true; markDirty(); return true; }
bool FilledButton::onMouseUp(const MouseEvent&) {
    bool w = pressed_; pressed_ = false; markDirty();
    if (w && onClick) onClick();
    return true;
}
bool FilledButton::onMouseMove(const MouseEvent&) { handleHover(); return true; }

// ============================================================
// Container
// ============================================================
Size Container::measure(const Size& max) {
    Size inner = {max.w - padding.l - padding.r, max.h - padding.t - padding.b};
    Size s = child ? child->measure(inner) : Size{0, 0};
    return {s.w + padding.l + padding.r, s.h + padding.t + padding.b};
}

void Container::layout(const Rect& b) {
    bounds_ = b;
    if (child) {
        Rect inner = {b.x + padding.l, b.y + padding.t,
                      b.w - padding.l - padding.r,
                      b.h - padding.t - padding.b};
        child->layout(inner);
    }
}

void Container::draw(ID2D1RenderTarget* rt) {
    if (color.a > 0) {
        fillRoundedRect(rt, bounds_.d2d(), radius, color);
    }
    if (borderWidth > 0) {
        drawRoundedRectBorder(rt, bounds_.d2d(), radius, borderColor, borderWidth);
    }
    if (child) child->draw(rt);
}

Widget* Container::hitTest(float x, float y) {
    if (!pointInRect(x, y, bounds_.d2d())) return nullptr;
    return child ? child->hitTest(x, y) : nullptr;
}

// ============================================================
// Card（圆角 12 + surfaceVariant + 多层阴影）
// ============================================================
Size Card::measure(const Size& max) {
    return child ? child->measure(max) : Size{0, 0};
}

void Card::layout(const Rect& b) {
    bounds_ = b;
    if (child) child->layout(b);
}

void Card::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    // 海拔阴影：多层半透明圆角矩形向下偏移
    D2D1_RECT_F r = bounds_.d2d();
    for (int i = 1; i <= static_cast<int>(elevation); i++) {
        D2D1_RECT_F sr = {r.left, r.top + i, r.right, r.bottom + i};
        fillRoundedRect(rt, sr, radius, D2D1::ColorF(0, 0, 0, 0.06f));
    }
    fillRoundedRect(rt, r, radius, sc.surfaceVariant);
    if (child) child->draw(rt);
}

Widget* Card::hitTest(float x, float y) {
    if (!pointInRect(x, y, bounds_.d2d())) return nullptr;
    return child ? child->hitTest(x, y) : nullptr;
}

// ============================================================
// ListTile
// ============================================================
Size ListTile::measure(const Size& max) {
    float h = subtitle.empty() ? 56.0f : 72.0f;
    return {max.w, std::min(h, max.h > 0 ? max.h : h)};
}

void ListTile::layout(const Rect& b) {
    bounds_ = b;
    float padX = 16;
    // leading 区
    float leadSize = 24;
    float leadX = b.x + padX;
    float leadY = b.y + (b.h - leadSize) / 2;
    leadingRect_ = {leadX, leadY, leadSize, leadSize};
    // trailing 区
    float trailW = 24;
    float trailX = b.x + b.w - padX - trailW;
    float trailY = b.y + (b.h - trailW) / 2;
    trailingRect_ = {trailX, trailY, trailW, trailW};
    // 文本区
    float textX = leadX + leadSize + 12;
    float textW = trailX - 12 - textX;
    if (subtitle.empty()) {
        titleRect_ = {textX, b.y, textW, b.h};
        subtitleRect_ = {0, 0, 0, 0};
    } else {
        titleRect_ = {textX, b.y + 12, textW, b.h / 2};
        subtitleRect_ = {textX, b.y + b.h / 2, textW, b.h / 2 - 12};
    }
    // 子项布局
    if (leading) leading->layout(leadingRect_);
    if (trailing) trailing->layout(trailingRect_);
}

void ListTile::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    if (hovered_ || pressed_) {
        float a = pressed_ ? 0.12f : 0.08f;
        fillRoundedRect(rt, bounds_.d2d(), 8, withAlpha(sc.onSurface, a));
    }
    if (leading) leading->draw(rt);
    if (trailing) trailing->draw(rt);
    // title
    Text t(title, fontSize, sc.onSurface, DWRITE_FONT_WEIGHT_MEDIUM);
    t.layout(titleRect_);
    t.draw(rt);
    // subtitle
    if (!subtitle.empty()) {
        Text st(subtitle, subtitleSize, sc.onSurfaceVariant);
        st.layout(subtitleRect_);
        st.draw(rt);
    }
}

Widget* ListTile::hitTest(float x, float y) {
    return pointInRect(x, y, bounds_.d2d()) ? this : nullptr;
}
bool ListTile::onMouseDown(const MouseEvent&) { pressed_ = true; markDirty(); return true; }
bool ListTile::onMouseUp(const MouseEvent&) {
    bool w = pressed_; pressed_ = false; markDirty();
    if (w && onTap) onTap();
    return true;
}
bool ListTile::onMouseMove(const MouseEvent&) { handleHover(); return true; }

// ============================================================
// NavigationRail
// ============================================================
Size NavigationRail::measure(const Size& max) {
    float h = std::min(static_cast<float>(items.size()) * itemHeight, max.h);
    return {width, h > 0 ? h : max.h};
}

void NavigationRail::layout(const Rect& b) { bounds_ = b; }

int NavigationRail::itemAt(float y) const {
    float yy = y - bounds_.y;
    if (yy < 0) return -1;
    int idx = static_cast<int>(yy / itemHeight);
    if (idx < 0 || idx >= static_cast<int>(items.size())) return -1;
    return idx;
}

void NavigationRail::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    // 背景
    fillRoundedRect(rt, bounds_.d2d(), 0, sc.surface);
    for (size_t i = 0; i < items.size(); i++) {
        const auto& it = items[i];
        float y = bounds_.y + i * itemHeight;
        float cx = bounds_.x + width / 2;
        bool selected = static_cast<int>(i) == selectedIndex;
        // 指示条/药丸背景
        if (selected) {
            Rect pill = {cx - 32, y + (itemHeight - 32) / 2, 64, 32};
            fillRoundedRect(rt, pill.d2d(), 16, sc.primaryContainer);
        } else if (static_cast<int>(i) == hoveredIndex_) {
            Rect pill = {cx - 32, y + (itemHeight - 32) / 2, 64, 32};
            fillRoundedRect(rt, pill.d2d(), 16, withAlpha(sc.onSurface, 0.08f));
        }
        // 图标
        D2D1_COLOR_F ic = selected ? sc.onPrimaryContainer : sc.onSurfaceVariant;
        Icon icon(it.icon, 24, ic);
        icon.layout({cx - 12, y + (itemHeight - 32) / 2 + 4, 24, 24});
        icon.draw(rt);
        // 标签
        Text lbl(it.label, 11, selected ? sc.onSurface : sc.onSurfaceVariant);
        Size s = lbl.measure({width, 20});
        lbl.layout({cx - s.w / 2, y + itemHeight - 18, s.w, s.h});
        lbl.draw(rt);
    }
}

Widget* NavigationRail::hitTest(float x, float y) {
    if (!pointInRect(x, y, bounds_.d2d())) return nullptr;
    if (itemAt(y) < 0) return nullptr;
    return this;
}

bool NavigationRail::onMouseDown(const MouseEvent& e) {
    int idx = itemAt(e.y + bounds_.y);
    if (idx >= 0 && idx != selectedIndex) {
        selectedIndex = idx;
        if (onChanged) onChanged(idx);
        markDirty();
    }
    return true;
}
bool NavigationRail::onMouseMove(const MouseEvent& e) {
    handleHover();
    int idx = itemAt(e.y + bounds_.y);
    if (idx != hoveredIndex_) { hoveredIndex_ = idx; markDirty(); }
    return true;
}
void NavigationRail::onMouseLeave() {
    bool changed = hovered_ || hoveredIndex_ >= 0;
    hovered_ = false; pressed_ = false; hoveredIndex_ = -1;
    if (changed) markDirty();
}

// ============================================================
// Slider
// ============================================================
float Slider::valueFromX(float x) const {
    float usable = bounds_.w - 2 * thumbRadius_;
    if (usable <= 0) return 0;
    float v = (x - bounds_.x - thumbRadius_) / usable;
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    return v;
}

Size Slider::measure(const Size& max) {
    return {max.w, std::min(36.0f, max.h > 0 ? max.h : 36.0f)};
}

void Slider::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    float cy = bounds_.y + bounds_.h / 2;
    float x0 = bounds_.x + thumbRadius_;
    float x1 = bounds_.x + bounds_.w - thumbRadius_;
    // 轨道
    auto* trackBrush = BrushCache::get(rt, withAlpha(sc.onSurface, 0.15f));
    rt->FillRectangle(D2D1::RectF(x0, cy - trackHeight_ / 2,
                                  x1, cy + trackHeight_ / 2), trackBrush);
    // 已选轨道
    float thumbX = x0 + (x1 - x0) * value;
    auto* activeBrush = BrushCache::get(rt, sc.primary);
    rt->FillRectangle(D2D1::RectF(x0, cy - trackHeight_ / 2,
                                  thumbX, cy + trackHeight_ / 2), activeBrush);
    // 拇指
    float r = (hovered_ || dragging_) ? thumbRadius_ + 2 : thumbRadius_;
    fillCircle(rt, thumbX, cy, r, sc.primary);
    if (hovered_ || dragging_) {
        fillCircle(rt, thumbX, cy, r + 6, withAlpha(sc.primary, 0.12f));
    }
}

Widget* Slider::hitTest(float x, float y) {
    return pointInRect(x, y, bounds_.d2d()) ? this : nullptr;
}
bool Slider::onMouseDown(const MouseEvent& e) {
    dragging_ = true;
    pressed_ = true;
    value = valueFromX(e.x + bounds_.x);
    if (onChanged) onChanged(value);
    markDirty();
    return true;
}
bool Slider::onMouseUp(const MouseEvent&) {
    dragging_ = false; pressed_ = false; markDirty(); return true;
}
bool Slider::onMouseMove(const MouseEvent& e) {
    handleHover();
    if (dragging_) {
        value = valueFromX(e.x + bounds_.x);
        if (onChanged) onChanged(value);
        markDirty();
    }
    return true;
}

// ============================================================
// ProgressBar
// ============================================================
Size ProgressBar::measure(const Size& max) {
    return {max.w, std::min(4.0f, max.h > 0 ? max.h : 4.0f)};
}

void ProgressBar::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    float h = bounds_.h;
    fillRoundedRect(rt, bounds_.d2d(), h / 2, withAlpha(sc.onSurface, 0.1f));
    D2D1_RECT_F fill = {bounds_.x, bounds_.y, bounds_.x + bounds_.w * value,
                        bounds_.bottom()};
    if (fill.right > fill.left) {
        fillRoundedRect(rt, fill, h / 2, sc.primary);
    }
}

// ============================================================
// TextField
// ============================================================
Size TextField::measure(const Size& max) {
    return {max.w, std::min(height, max.h > 0 ? max.h : height)};
}

bool TextField::cursorVisible() const {
    return (GetTickCount() % 1000) < 500;
}

void TextField::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    bool foc = (FocusManager::focused() == this);
    D2D1_COLOR_F border = foc ? sc.primary : sc.outline;
    float bw = foc ? 2.0f : 1.0f;
    D2D1_RECT_F r = bounds_.d2d();
    // 内缩边框，避免超出 bounds
    D2D1_RECT_F br = {r.left + bw / 2, r.top + bw / 2,
                      r.right - bw / 2, r.bottom - bw / 2};
    drawRoundedRectBorder(rt, br, 8, border, bw);
    // 文本/占位符
    float padX = 12;
    std::wstring show = text;
    D2D1_COLOR_F tc = sc.onSurface;
    if (show.empty()) {
        show = placeholder;
        tc = withAlpha(sc.onSurface, 0.4f);
    }
    if (!show.empty()) {
        Text t(show, fontSize, tc);
        t.layout({bounds_.x + padX, bounds_.y + (bounds_.h - fontSize - 4) / 2,
                  bounds_.w - padX * 2, fontSize + 4});
        t.draw(rt);
    }
    // 光标
    if (foc && cursorVisible()) {
        Text t(text.empty() ? std::wstring(L" ") : text, fontSize, sc.onSurface);
        Size s = t.measure({bounds_.w, bounds_.h});
        float cx = bounds_.x + padX + s.w;
        float cy0 = bounds_.y + 8;
        float cy1 = bounds_.bottom() - 8;
        auto* b = BrushCache::get(rt, sc.primary);
        rt->DrawLine(D2D1::Point2F(cx, cy0), D2D1::Point2F(cx, cy1), b, 1.0f);
    }
}

Widget* TextField::hitTest(float x, float y) {
    return pointInRect(x, y, bounds_.d2d()) ? this : nullptr;
}

bool TextField::onMouseDown(const MouseEvent&) {
    focused_ = true;
    FocusManager::setFocused(this);
    markDirty();
    return true;
}
bool TextField::onMouseMove(const MouseEvent&) { handleHover(); return true; }

bool TextField::onChar(wchar_t ch) {
    if (FocusManager::focused() != this) return false;
    if (ch == L'\b') {  // 退格
        if (!text.empty()) { text.pop_back(); if (onChanged) onChanged(text); markDirty(); }
        return true;
    }
    if (ch == L'\r' || ch == L'\n') {
        if (onEnter) onEnter();
        return true;
    }
    if (ch >= 0x20) {  // 可打印
        text.push_back(ch);
        if (onChanged) onChanged(text);
        markDirty();
        return true;
    }
    return false;
}

bool TextField::onKey(const KeyEvent& e) {
    if (FocusManager::focused() != this) return false;
    if (e.key == VK_BACK) {
        if (!text.empty()) { text.pop_back(); if (onChanged) onChanged(text); markDirty(); }
        return true;
    }
    if (e.key == VK_RETURN) {
        if (onEnter) onEnter();
        return true;
    }
    if (e.key == VK_DELETE) {
        text.clear(); if (onChanged) onChanged(text); markDirty();
        return true;
    }
    return false;
}

// ============================================================
// Scrollable
// ============================================================
Size Scrollable::measure(const Size& max) {
    if (!child) { contentHeight_ = 0; return {max.w, max.h}; }
    Size s = child->measure({max.w, 1e6f});
    contentHeight_ = s.h;
    viewportHeight_ = max.h;
    return {max.w, max.h};
}

void Scrollable::clampOffset() {
    float maxOff = contentHeight_ - viewportHeight_;
    if (maxOff < 0) maxOff = 0;
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > maxOff) scrollOffset = maxOff;
}

void Scrollable::layout(const Rect& b) {
    bounds_ = b;
    viewportHeight_ = b.h;
    if (child) {
        // 以近无限高度测量，获取内容真实高度
        Size s = child->measure({b.w, 1e6f});
        contentHeight_ = s.h;
        clampOffset();
        child->layout({b.x, b.y - scrollOffset, b.w, contentHeight_});
    }
}

void Scrollable::draw(ID2D1RenderTarget* rt) {
    if (!child) return;
    rt->PushAxisAlignedClip(bounds_.d2d(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    child->draw(rt);
    rt->PopAxisAlignedClip();
    // 滚动条
    if (showScrollbar && contentHeight_ > viewportHeight_) {
        auto& sc = ThemeManager::instance().current();
        float maxOff = contentHeight_ - viewportHeight_;
        float thumbH = viewportHeight_ * (viewportHeight_ / contentHeight_);
        if (thumbH < 24) thumbH = 24;
        float thumbY = bounds_.y + (maxOff > 0
                        ? (scrollOffset / maxOff) * (viewportHeight_ - thumbH) : 0);
        Rect thumb = {bounds_.right() - 6, thumbY, 4, thumbH};
        fillRoundedRect(rt, thumb.d2d(), 2, withAlpha(sc.onSurface, 0.3f));
    }
}

Widget* Scrollable::hitTest(float x, float y) {
    if (!pointInRect(x, y, bounds_.d2d())) return nullptr;
    return child ? child->hitTest(x, y) : nullptr;
}

bool Scrollable::onMouseWheel(const MouseEvent& e) {
    scrollOffset -= e.wheel * 40.0f;
    clampOffset();
    if (child) child->layout({bounds_.x, bounds_.y - scrollOffset, bounds_.w, contentHeight_});
    markDirty();
    return true;
}

bool Scrollable::onMouseDown(const MouseEvent& e) {
    if (!child) return false;
    auto* h = child->hitTest(e.x, e.y);
    return h ? h->onMouseDown(e) : false;
}
bool Scrollable::onMouseUp(const MouseEvent& e) {
    if (!child) return false;
    auto* h = child->hitTest(e.x, e.y);
    return h ? h->onMouseUp(e) : false;
}
bool Scrollable::onMouseMove(const MouseEvent& e) {
    if (!child) return false;
    auto* h = child->hitTest(e.x, e.y);
    return h ? h->onMouseMove(e) : false;
}

// ============================================================
// Scrollbar
// ============================================================
Size Scrollbar::measure(const Size& max) { return {4, max.h}; }

void Scrollbar::draw(ID2D1RenderTarget* rt) {
    if (contentHeight <= bounds_.h) return;
    float maxOff = contentHeight - bounds_.h;
    float thumbH = bounds_.h * (bounds_.h / contentHeight);
    if (thumbH < 24) thumbH = 24;
    float thumbY = bounds_.y + (maxOff > 0
                    ? (scrollOffset / maxOff) * (bounds_.h - thumbH) : 0);
    Rect thumb = {bounds_.x, thumbY, 4, thumbH};
    fillRoundedRect(rt, thumb.d2d(), 2, color);
}

// ============================================================
// ListView
// ============================================================
void ListView::rebuild() {
    auto col = std::make_shared<Column>();
    col->spacing = spacing;
    for (int i = 0; i < itemCount; i++) {
        auto w = itemBuilder ? itemBuilder(i) : nullptr;
        if (w) col->addChild(w);
    }
    scroll_->child = col;
    builtCount_ = itemCount;
}

Size ListView::measure(const Size& max) {
    if (builtCount_ != itemCount) rebuild();
    return scroll_->measure(max);
}

void ListView::layout(const Rect& b) {
    if (builtCount_ != itemCount) rebuild();
    scroll_->layout(b);
    bounds_ = b;
}

void ListView::draw(ID2D1RenderTarget* rt) { scroll_->draw(rt); }

Widget* ListView::hitTest(float x, float y) { return scroll_->hitTest(x, y); }

bool ListView::onMouseWheel(const MouseEvent& e) {
    return scroll_->onMouseWheel(e);
}

bool ListView::onMouseDown(const MouseEvent& e) { return scroll_->onMouseDown(e); }
bool ListView::onMouseUp(const MouseEvent& e) { return scroll_->onMouseUp(e); }
bool ListView::onMouseMove(const MouseEvent& e) { return scroll_->onMouseMove(e); }

// ============================================================
// GridView
// ============================================================
void GridView::computeCols(float viewportW) {
    int c = std::max(1, static_cast<int>(viewportW / maxCrossAxisExtent));
    cols_ = c;
    crossAxisExtent_ = viewportW / c;
}

void GridView::rebuild() {
    auto col = std::make_shared<Column>();
    col->spacing = spacing;
    float itemW = crossAxisExtent_;
    float itemH = (childAspectRatio > 0) ? itemW / childAspectRatio : itemW;
    int cols = cols_;
    for (int i = 0; i < itemCount; i += cols) {
        auto row = std::make_shared<Row>();
        row->spacing = spacing;
        for (int c = 0; c < cols && i + c < itemCount; c++) {
            auto w = itemBuilder ? itemBuilder(i + c) : nullptr;
            if (w) {
                auto sb = std::make_shared<SizedBox>(itemW, itemH, w);
                row->addChild(sb);
            }
        }
        col->addChild(row);
    }
    scroll_->child = col;
    builtCount_ = itemCount;
}

Size GridView::measure(const Size& max) {
    computeCols(max.w);
    if (builtCount_ != itemCount) rebuild();
    return scroll_->measure(max);
}

void GridView::layout(const Rect& b) {
    float oldExtent = crossAxisExtent_;
    computeCols(b.w);
    if (std::fabs(oldExtent - crossAxisExtent_) > 0.5f || builtCount_ != itemCount) {
        rebuild();
    }
    scroll_->layout(b);
    bounds_ = b;
}

void GridView::draw(ID2D1RenderTarget* rt) { scroll_->draw(rt); }

Widget* GridView::hitTest(float x, float y) { return scroll_->hitTest(x, y); }

bool GridView::onMouseWheel(const MouseEvent& e) {
    return scroll_->onMouseWheel(e);
}

bool GridView::onMouseDown(const MouseEvent& e) { return scroll_->onMouseDown(e); }
bool GridView::onMouseUp(const MouseEvent& e) { return scroll_->onMouseUp(e); }
bool GridView::onMouseMove(const MouseEvent& e) { return scroll_->onMouseMove(e); }

// ============================================================
// DialogManager + Dialog
// ============================================================
DialogManager& DialogManager::instance() {
    static DialogManager inst;
    return inst;
}

void DialogManager::push(std::shared_ptr<Dialog> d) {
    stack_.push_back(std::move(d));
    WindowManager::instance().requestRedraw();
}

void DialogManager::pop() {
    if (!stack_.empty()) {
        stack_.pop_back();
        WindowManager::instance().requestRedraw();
    }
}

std::shared_ptr<Dialog> DialogManager::top() const {
    return stack_.empty() ? nullptr : stack_.back();
}

void DialogManager::arrangeAndDraw(ID2D1RenderTarget* rt) {
    if (stack_.empty()) return;
    auto d = stack_.back();
    int sw = WindowManager::instance().width();
    int sh = WindowManager::instance().height();
    d->measure({static_cast<float>(sw), static_cast<float>(sh)});
    d->layout({0, 0, static_cast<float>(sw), static_cast<float>(sh)});
    d->draw(rt);
}

Widget* DialogManager::hitTest(float x, float y) {
    if (stack_.empty()) return nullptr;
    return stack_.back()->hitTest(x, y);
}

bool DialogManager::dispatchMouseDown(const MouseEvent& e) {
    if (stack_.empty()) return false;
    return stack_.back()->onMouseDown(e);
}
bool DialogManager::dispatchMouseUp(const MouseEvent& e) {
    if (stack_.empty()) return false;
    return stack_.back()->onMouseUp(e);
}
bool DialogManager::dispatchMouseMove(const MouseEvent& e) {
    if (stack_.empty()) return false;
    return stack_.back()->onMouseMove(e);
}
bool DialogManager::dispatchMouseWheel(const MouseEvent& e) {
    if (stack_.empty()) return false;
    return stack_.back()->onMouseWheel(e);
}
bool DialogManager::dispatchChar(wchar_t ch) {
    if (stack_.empty()) return false;
    return stack_.back()->onChar(ch);
}
bool DialogManager::dispatchKey(const KeyEvent& e) {
    if (stack_.empty()) return false;
    return stack_.back()->onKey(e);
}

// ---- Dialog ----
Size Dialog::measure(const Size& max) { return {max.w, max.h}; }

void Dialog::layout(const Rect& b) {
    bounds_ = b;
    lastScreenW_ = b.w; lastScreenH_ = b.h;
    // 测量子项以确定卡片高度
    float padX = 24, padTop = 24, padBot = 16;
    float titleH = title.empty() ? 0 : 28;
    float contentH = content ? content->measure({dialogWidth - padX * 2, 1e5f}).h : 0;
    float actionsH = 40;
    float cardH = padTop + titleH + (title.empty() ? 0 : 8) + contentH +
                  (content ? 16 : 0) + actionsH + padBot;
    float cardX = (b.w - dialogWidth) / 2;
    float cardY = (b.h - cardH) / 2;
    cardRect_ = {b.x + cardX, b.y + cardY, dialogWidth, cardH};

    float y = cardRect_.y + padTop;
    if (!title.empty()) {
        titleRect_ = {cardRect_.x + padX, y, dialogWidth - padX * 2, titleH};
        y += titleH + 8;
    }
    if (content) {
        contentRect_ = {cardRect_.x + padX, y, dialogWidth - padX * 2, contentH};
        content->layout(contentRect_);
        y += contentH + 16;
    }
    actionsRect_ = {cardRect_.x + padX, y, dialogWidth - padX * 2, actionsH};
    // 布局 actions（右对齐排列）
    float ax = actionsRect_.right();
    float ay = actionsRect_.y;
    for (int i = static_cast<int>(actions.size()) - 1; i >= 0; i--) {
        Size s = actions[i]->measure({dialogWidth, 40});
        ax -= s.w;
        actions[i]->layout({ax, ay + (actionsH - s.h) / 2, s.w, s.h});
        ax -= 8;
    }
}

void Dialog::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    // 半透明遮罩
    rt->FillRectangle(bounds_.d2d(),
                      BrushCache::get(rt, D2D1::ColorF(0, 0, 0, 0.5f)));
    // 卡片背景
    fillRoundedRect(rt, cardRect_.d2d(), 16, sc.surface);
    // 标题
    if (!title.empty()) {
        Text t(title, 20, sc.onSurface, DWRITE_FONT_WEIGHT_MEDIUM);
        t.layout(titleRect_);
        t.draw(rt);
    }
    // 内容
    if (content) content->draw(rt);
    // 动作按钮
    for (auto& a : actions) a->draw(rt);
}

Widget* Dialog::hitTest(float x, float y) {
    // 卡片内：委托子项
    if (pointInRect(x, y, cardRect_.d2d())) {
        if (content) {
            auto* h = content->hitTest(x, y);
            if (h) return h;
        }
        for (auto& a : actions) {
            auto* h = a->hitTest(x, y);
            if (h) return h;
        }
        return this;
    }
    // 遮罩区：吞掉事件（模态）
    return this;
}

bool Dialog::onMouseDown(const MouseEvent&) { return true; }
bool Dialog::onMouseUp(const MouseEvent&) { return true; }
bool Dialog::onMouseMove(const MouseEvent&) { return true; }

void Dialog::show() {
    auto self = std::static_pointer_cast<Dialog>(shared_from_this());
    DialogManager::instance().push(self);
}
void Dialog::close() { DialogManager::instance().pop(); }

// ============================================================
// LyricsView
// ============================================================
Size LyricsView::measure(const Size& max) { return {max.w, max.h}; }

void LyricsView::layout(const Rect& b) { bounds_ = b; }

void LyricsView::ensureAutoScroll() {
    if (currentIndex < 0 || currentIndex >= static_cast<int>(lines.size())) return;
    if (!autoScroll_) return;
    float lineH = lineHeight;
    targetOffset_ = currentIndex * lineH + lineH / 2 - bounds_.h / 2;
    float diff = targetOffset_ - scrollOffset_;
    if (std::fabs(diff) > 0.5f) {
        scrollOffset_ += diff * 0.2f;
        markDirty();
    } else {
        scrollOffset_ = targetOffset_;
        autoScroll_ = false;
    }
}

void LyricsView::setCurrentIndex(int idx) {
    if (idx == currentIndex) return;
    currentIndex = idx;
    autoScroll_ = true;
    markDirty();
}

void LyricsView::draw(ID2D1RenderTarget* rt) {
    ensureAutoScroll();
    auto& sc = ThemeManager::instance().current();
    rt->PushAxisAlignedClip(bounds_.d2d(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float cy = bounds_.y + bounds_.h / 2;
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        float y = bounds_.y - scrollOffset_ + i * lineHeight + lineHeight / 2;
        if (y < bounds_.y - lineHeight || y > bounds_.bottom() + lineHeight) continue;
        bool cur = (i == currentIndex);
        float fs = cur ? lineHeight * 0.46f : lineHeight * 0.36f;
        D2D1_COLOR_F col = cur ? sc.primary : sc.onSurfaceVariant;
        auto weight = cur ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        Text t(lines[i].text, fs, col, weight);
        t.textAlignment = DWRITE_TEXT_ALIGNMENT_CENTER;
        Size s = t.measure({bounds_.w, lineHeight});
        t.layout({bounds_.x, y - s.h / 2, bounds_.w, s.h});
        // 距离中心越远越淡
        float dist = std::fabs(y - cy) / (bounds_.h / 2);
        if (!cur && dist > 0.3f) {
            float alpha = std::max(0.25f, 1.0f - dist * 0.9f);
            t.color = withAlpha(sc.onSurfaceVariant, alpha);
        }
        t.draw(rt);
    }
    rt->PopAxisAlignedClip();
}

bool LyricsView::onMouseWheel(const MouseEvent& e) {
    scrollOffset_ += e.wheel * 40.0f;
    autoScroll_ = false;  // 手动滚动后停止自动跟随
    float maxOff = static_cast<float>(lines.size()) * lineHeight - bounds_.h;
    if (maxOff < 0) maxOff = 0;
    if (scrollOffset_ < 0) scrollOffset_ = 0;
    if (scrollOffset_ > maxOff) scrollOffset_ = maxOff;
    markDirty();
    return true;
}

// ============================================================
// VideoView（mpv wid 嵌入挂载点）
// ============================================================
// 子窗口过程：仅处理 WM_ERASEBKGND 返回 1（避免闪烁，mpv 自行绘制）
static LRESULT CALLBACK MpvEmbedWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(h, msg, w, l);
}

VideoView::VideoView() = default;

VideoView::~VideoView() {
    releaseHwnd();
}

Size VideoView::measure(const Size& max) { return {max.w, max.h}; }

void VideoView::ensureHwnd() {
    if (hwnd_) return;
    HWND parent = WindowManager::instance().hwnd();
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE);
    static const wchar_t* kClass = L"MEPlayerMpvEmbed";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = MpvEmbedWndProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = kClass;
        RegisterClassW(&wc);
        registered = true;
    }
    hwnd_ = CreateWindowExW(0, kClass, L"",
                            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, 1, 1,
                            parent, nullptr, hInst, nullptr);
}

void VideoView::releaseHwnd() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void VideoView::layout(const Rect& b) {
    bounds_ = b;
    ensureHwnd();
    if (hwnd_) {
        int x = static_cast<int>(b.x);
        int y = static_cast<int>(b.y);
        int w = static_cast<int>(b.w);
        int h = static_cast<int>(b.h);
        SetWindowPos(hwnd_, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW);
    }
}

void VideoView::draw(ID2D1RenderTarget* rt) {
    // wid 模式下视频由 mpv 直接绘制到子窗口，这里只画黑底兜底
    // （子窗口已覆盖该区域，通常不会触发）
    rt->FillRectangle(bounds_.d2d(), BrushCache::get(rt, bgColor));
}

Widget* VideoView::hitTest(float x, float y) {
    return pointInRect(x, y, bounds_.d2d()) ? this : nullptr;
}

// ============================================================
// MiniPlayerBar
// ============================================================
Size MiniPlayerBar::measure(const Size& max) {
    return {max.w, std::min(64.0f, max.h > 0 ? max.h : 64.0f)};
}

void MiniPlayerBar::layout(const Rect& b) {
    bounds_ = b;
    // 音乐模式控制按钮区（右侧）
    if (kind == MiniBarKind::Music) {
        float bw = 36;
        float gap = 4;
        float totalW = bw * 3 + gap * 2;
        float x = b.right() - 16 - totalW;
        float y = b.y + (b.h - bw) / 2;
        prevRect_ = {x, y, bw, bw};
        playRect_ = {x + bw + gap, y, bw, bw};
        nextRect_ = {x + 2 * (bw + gap), y, bw, bw};
    }
    progressRect_ = {b.x, b.bottom() - 2, b.w, 2};
}

void MiniPlayerBar::draw(ID2D1RenderTarget* rt) {
    auto& sc = ThemeManager::instance().current();
    // 背景
    fillRoundedRect(rt, bounds_.d2d(), 0, sc.surface);
    if (hovered_) {
        fillRoundedRect(rt, bounds_.d2d(), 0, withAlpha(sc.onSurface, 0.04f));
    }
    switch (kind) {
    case MiniBarKind::Music:  drawMusic(rt, sc); break;
    case MiniBarKind::Video:  drawVideo(rt, sc); break;
    case MiniBarKind::Iptv:   drawIptv(rt, sc); break;
    default: break;
    }
}

void MiniPlayerBar::drawMusic(ID2D1RenderTarget* rt, const ColorScheme& sc) {
    auto& gp = GlobalPlayer::instance();
    // 封面（左侧 48x48 圆角）
    float cover = 48;
    Rect cr = {bounds_.x + 12, bounds_.y + (bounds_.h - cover) / 2, cover, cover};
    fillRoundedRect(rt, cr.d2d(), 6, sc.surfaceVariant);
    Icon musicIc(IconKind::Music, 24, sc.onSurfaceVariant);
    musicIc.layout({cr.x + 12, cr.y + 12, 24, 24});
    musicIc.draw(rt);
    // 标题 + 艺术家
    const auto& meta = gp.currentMeta();
    std::wstring title = meta.title.empty() ? L"未播放" :
        std::wstring(meta.title.begin(), meta.title.end());
    Text tt(title, 14, sc.onSurface, DWRITE_FONT_WEIGHT_MEDIUM);
    Size ts = tt.measure({bounds_.w - cover - 200, 22});
    tt.layout({cr.right() + 12, bounds_.y + 12, ts.w, ts.h});
    tt.draw(rt);
    std::wstring artist = meta.artist.empty() ? L"" :
        std::wstring(meta.artist.begin(), meta.artist.end());
    if (!artist.empty()) {
        Text at(artist, 12, sc.onSurfaceVariant);
        Size as = at.measure({bounds_.w - cover - 200, 18});
        at.layout({cr.right() + 12, bounds_.y + 34, as.w, as.h});
        at.draw(rt);
    }
    // 控制按钮
    bool playing = gp.musicIsPlaying();
    Icon prev(IconKind::Prev, 22, sc.onSurface);
    prev.layout({prevRect_.x + 7, prevRect_.y + 7, 22, 22}); prev.draw(rt);
    Icon play(playing ? IconKind::Pause : IconKind::Play, 22, sc.primary);
    play.layout({playRect_.x + 7, playRect_.y + 7, 22, 22}); play.draw(rt);
    Icon next(IconKind::Next, 22, sc.onSurface);
    next.layout({nextRect_.x + 7, nextRect_.y + 7, 22, 22}); next.draw(rt);
    // 进度条
    double dur = gp.musicDuration();
    double pos = gp.musicPosition();
    float v = (dur > 0) ? static_cast<float>(pos / dur) : 0;
    ProgressBar pb;
    pb.value = v;
    pb.layout(progressRect_);
    pb.draw(rt);
}

void MiniPlayerBar::drawVideo(ID2D1RenderTarget* rt, const ColorScheme& sc) {
    auto& gp = GlobalPlayer::instance();
    const auto& snap = gp.videoSnapshot();
    // 视频 icon
    Icon vic(IconKind::Video, 24, sc.primary);
    vic.layout({bounds_.x + 16, bounds_.y + (bounds_.h - 24) / 2, 24, 24});
    vic.draw(rt);
    std::wstring title = snap.title.empty() ? L"视频" :
        std::wstring(snap.title.begin(), snap.title.end());
    Text tt(title, 14, sc.onSurface, DWRITE_FONT_WEIGHT_MEDIUM);
    Size ts = tt.measure({bounds_.w - 80, 22});
    tt.layout({bounds_.x + 52, bounds_.y + (bounds_.h - ts.h) / 2, ts.w, ts.h});
    tt.draw(rt);
    // 进度条
    float v = (snap.duration > 0) ? static_cast<float>(snap.position / snap.duration) : 0;
    ProgressBar pb;
    pb.value = v;
    pb.layout(progressRect_);
    pb.draw(rt);
}

void MiniPlayerBar::drawIptv(ID2D1RenderTarget* rt, const ColorScheme& sc) {
    auto& gp = GlobalPlayer::instance();
    const auto& snap = gp.iptvSnapshot();
    Icon iic(IconKind::Iptv, 24, sc.primary);
    iic.layout({bounds_.x + 16, bounds_.y + (bounds_.h - 24) / 2, 24, 24});
    iic.draw(rt);
    std::wstring ch = snap.channelName.empty() ? L"IPTV" :
        std::wstring(snap.channelName.begin(), snap.channelName.end());
    Text ct(ch, 14, sc.onSurface, DWRITE_FONT_WEIGHT_MEDIUM);
    Size cs = ct.measure({bounds_.w - 80, 22});
    ct.layout({bounds_.x + 52, bounds_.y + 12, cs.w, cs.h});
    ct.draw(rt);
    std::wstring grp = snap.groupName.empty() ? L"" :
        std::wstring(snap.groupName.begin(), snap.groupName.end());
    if (!grp.empty()) {
        Text gt(grp, 12, sc.onSurfaceVariant);
        Size gs = gt.measure({bounds_.w - 80, 18});
        gt.layout({bounds_.x + 52, bounds_.y + 34, gs.w, gs.h});
        gt.draw(rt);
    }
}

Widget* MiniPlayerBar::hitTest(float x, float y) {
    return pointInRect(x, y, bounds_.d2d()) ? this : nullptr;
}

bool MiniPlayerBar::onMouseDown(const MouseEvent& e) {
    pressed_ = true;
    float ax = e.x + bounds_.x;
    float ay = e.y + bounds_.y;
    if (kind == MiniBarKind::Music) {
        if (pointInRect(ax, ay, prevRect_.d2d())) { GlobalPlayer::instance().prev(); markDirty(); return true; }
        if (pointInRect(ax, ay, playRect_.d2d())) { GlobalPlayer::instance().togglePlay(); markDirty(); return true; }
        if (pointInRect(ax, ay, nextRect_.d2d())) { GlobalPlayer::instance().next(); markDirty(); return true; }
    }
    return true;
}

bool MiniPlayerBar::onMouseUp(const MouseEvent&) {
    bool w = pressed_; pressed_ = false; markDirty();
    if (w && onClick) onClick();
    return true;
}

bool MiniPlayerBar::onMouseMove(const MouseEvent&) { handleHover(); return true; }

}  // namespace meplayer
