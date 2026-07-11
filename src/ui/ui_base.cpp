#include "ui/ui_base.h"
#include "app/window_manager.h"
#include <algorithm>
#include <cmath>

namespace meplayer {

void Widget::markDirty() {
    WindowManager::instance().requestRedraw();
}

// ---- Flex measure/layout ----
Size Flex::measure(const Size& max) {
    bool horiz = direction == Axis::Horizontal;
    float innerMaxW = std::max(0.0f, max.w - padding.l - padding.r);
    float innerMaxH = std::max(0.0f, max.h - padding.t - padding.b);
    float mainMax = horiz ? innerMaxW : innerMaxH;
    float crossMax = horiz ? innerMaxH : innerMaxW;

    // 计算 flex 总和和非 flex 子项占用
    float flexSum = 0, usedMain = 0, maxCross = 0;
    std::vector<Size> sizes(children.size());
    for (size_t i = 0; i < children.size(); i++) {
        float f = 0;
        auto it = flex.find(static_cast<int>(i));
        if (it != flex.end()) f = it->second;
        if (f > 0) {
            flexSum += f;
            sizes[i] = {0,0};
        } else {
            Size s = children[i]->measure({horiz ? innerMaxW : innerMaxW, horiz ? innerMaxH : innerMaxH});
            if (horiz) usedMain += s.w;
            else       usedMain += s.h;
            float c = horiz ? s.h : s.w;
            if (c > maxCross) maxCross = c;
            sizes[i] = s;
        }
        if (i < children.size()-1) usedMain += spacing;
    }
    // 分配 flex 空间
    float free = mainMax - usedMain;
    if (flexSum > 0 && free > 0) {
        for (size_t i = 0; i < children.size(); i++) {
            auto it = flex.find(static_cast<int>(i));
            if (it == flex.end() || it->second <= 0) continue;
            float alloc = free * (it->second / flexSum);
            Size s = children[i]->measure(horiz ? Size{alloc, crossMax} : Size{crossMax, alloc});
            if (horiz) { sizes[i].w = alloc; if (s.h > maxCross) maxCross = s.h; }
            else       { sizes[i].h = alloc; if (s.w > maxCross) maxCross = s.w; }
        }
    }
    float mainTotal = usedMain;
    if (flexSum > 0) mainTotal = mainMax;  // flex 填满
    Size inner = horiz ? Size{mainTotal, maxCross} : Size{maxCross, mainTotal};
    return { inner.w + padding.l + padding.r, inner.h + padding.t + padding.b };
}

void Flex::layout(const Rect& b) {
    bounds_ = b;
    bool horiz = direction == Axis::Horizontal;
    Rect inner{ b.x + padding.l, b.y + padding.t,
                std::max(0.0f, b.w - padding.l - padding.r),
                std::max(0.0f, b.h - padding.t - padding.b) };
    float mainSize = horiz ? inner.w : inner.h;
    float crossSize = horiz ? inner.h : inner.w;

    // 先 measure 所有子项拿到尺寸
    std::vector<Size> sizes(children.size());
    float flexSum = 0, usedMain = 0;
    for (size_t i = 0; i < children.size(); i++) {
        float f = 0;
        auto it = flex.find(static_cast<int>(i));
        if (it != flex.end()) f = it->second;
        if (f > 0) { flexSum += f; sizes[i] = {0,0}; continue; }
        sizes[i] = children[i]->measure(horiz ? Size{mainSize, crossSize} : Size{crossSize, mainSize});
        usedMain += horiz ? sizes[i].w : sizes[i].h;
    }
    usedMain += spacing * (children.empty() ? 0 : children.size() - 1);

    // flex 分配
    float free = mainSize - usedMain;
    if (flexSum > 0 && free > 0) {
        for (size_t i = 0; i < children.size(); i++) {
            auto it = flex.find(static_cast<int>(i));
            if (it == flex.end() || it->second <= 0) continue;
            float alloc = free * (it->second / flexSum);
            sizes[i] = children[i]->measure(horiz ? Size{alloc, crossSize} : Size{crossSize, alloc});
            if (horiz) sizes[i].w = alloc; else sizes[i].h = alloc;
        }
    }

    // 计算主轴起始位置（根据对齐）
    float pos = 0;
    float totalMain = 0;
    for (auto& s : sizes) totalMain += horiz ? s.w : s.h;
    totalMain += spacing * (children.empty() ? 0 : children.size() - 1);

    switch (mainAlign) {
    case MainAxisAlignment::Start:        pos = 0; break;
    case MainAxisAlignment::Center:       pos = (mainSize - totalMain) / 2; break;
    case MainAxisAlignment::End:          pos = mainSize - totalMain; break;
    case MainAxisAlignment::SpaceBetween:
        pos = 0;
        if (children.size() > 1) {
            // spacing 被 space 替代
            totalMain -= spacing * (children.size() - 1);
        }
        break;
    case MainAxisAlignment::SpaceAround:
        pos = children.empty() ? 0 : (mainSize - totalMain) / children.size() / 2;
        break;
    case MainAxisAlignment::SpaceEvenly:
        pos = children.empty() ? 0 : (mainSize - totalMain) / (children.size() + 1);
        break;
    }

    // 布局每个子项
    float stepSpacing = spacing;
    if (mainAlign == MainAxisAlignment::SpaceBetween && children.size() > 1) {
        stepSpacing = (mainSize - totalMain) / (children.size() - 1);
    } else if (mainAlign == MainAxisAlignment::SpaceAround && children.size() > 0) {
        stepSpacing = (mainSize - totalMain) / children.size();
    } else if (mainAlign == MainAxisAlignment::SpaceEvenly && children.size() > 0) {
        stepSpacing = (mainSize - totalMain) / (children.size() + 1);
    }

    for (size_t i = 0; i < children.size(); i++) {
        Size s = sizes[i];
        float cross = 0;
        switch (crossAlign) {
        case CrossAxisAlignment::Start:  cross = 0; break;
        case CrossAxisAlignment::Center: cross = (crossSize - (horiz ? s.h : s.w)) / 2; break;
        case CrossAxisAlignment::End:    cross = crossSize - (horiz ? s.h : s.w); break;
        case CrossAxisAlignment::Stretch:
            if (horiz) s.h = crossSize; else s.w = crossSize;
            cross = 0;
            break;
        }
        Rect r;
        if (horiz) {
            r = {inner.x + pos, inner.y + cross, s.w, s.h};
            pos += s.w + stepSpacing;
        } else {
            r = {inner.x + cross, inner.y + pos, s.w, s.h};
            pos += s.h + stepSpacing;
        }
        children[i]->layout(r);
    }
}

// ---- ClipRRect ----
void ClipRRect::draw(ID2D1RenderTarget* rt) {
    if (!child) return;
    ComPtr<ID2D1Layer> layer;
    rt->CreateLayer(nullptr, layer.GetAddressOf());
    D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters(
        bounds_.d2d(),
        nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(),
        1.0f, nullptr,
        D2D1_LAYER_OPTIONS_NONE);
    ComPtr<ID2D1PathGeometry> geo;
    WindowManager::instance().d2dFactory()->CreatePathGeometry(geo.GetAddressOf());
    ComPtr<ID2D1GeometrySink> sink;
    geo->Open(sink.GetAddressOf());
    D2D1_RECT_F r = bounds_.d2d();
    sink->BeginFigure(D2D1::Point2F(r.left + radius, r.top), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(r.right, r.top),
        D2D1::SizeF(radius, radius), 90, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(r.right, r.bottom),
        D2D1::SizeF(radius, radius), 90, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(r.left, r.bottom),
        D2D1::SizeF(radius, radius), 90, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(r.left, r.top),
        D2D1::SizeF(radius, radius), 90, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    params.geometricMask = geo.Get();
    rt->PushLayer(params, layer.Get());
    child->draw(rt);
    rt->PopLayer();
}

// ---- BrushCache ----
std::map<uint32_t, ComPtr<ID2D1SolidColorBrush>> BrushCache::cache_;

static uint32_t packColor(D2D1_COLOR_F c) {
    uint8_t a = static_cast<uint8_t>(c.a * 255);
    uint8_t r = static_cast<uint8_t>(c.r * 255);
    uint8_t g = static_cast<uint8_t>(c.g * 255);
    uint8_t b = static_cast<uint8_t>(c.b * 255);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

ID2D1SolidColorBrush* BrushCache::get(ID2D1RenderTarget* rt, D2D1_COLOR_F color) {
    uint32_t key = packColor(color);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.Get();
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(color, brush.GetAddressOf());
    cache_[key] = brush;
    return brush.Get();
}

void BrushCache::clear() { cache_.clear(); }

// ---- TextFormatCache ----
std::map<std::wstring, ComPtr<IDWriteTextFormat>> TextFormatCache::cache_;

IDWriteTextFormat* TextFormatCache::get(IDWriteFactory* dw, float size,
                                         const wchar_t* family,
                                         DWRITE_FONT_WEIGHT weight) {
    wchar_t key[128];
    swprintf_s(key, L"%s|%g|%d", family, size, static_cast<int>(weight));
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> fmt;
    dw->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", fmt.GetAddressOf());
    cache_[key] = fmt;
    return fmt.Get();
}

void TextFormatCache::clear() { cache_.clear(); }

}  // namespace meplayer
