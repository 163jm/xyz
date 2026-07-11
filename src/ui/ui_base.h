// UI 基础框架：Widget 基类 + 布局系统
// Direct2D 自绘控件库，复刻 Flutter Material 3 风格
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <variant>

namespace meplayer {

using Microsoft::WRL::ComPtr;

// 前置
class Widget;
using WidgetPtr = std::shared_ptr<Widget>;

// ---- 几何 ----
struct EdgeInsets { float l=0, t=0, r=0, b=0; };
struct Size { float w=0, h=0; };
struct Offset { float x=0, y=0; };

inline EdgeInsets all(float v) { return {v,v,v,v}; }
inline EdgeInsets sym(float h, float v) { return {h,v,h,v}; }
inline EdgeInsets only(float l,float t,float r,float b) { return {l,t,r,b}; }

// ---- 简化的 Rect（与 D2D1_RECT_F 互转）----
struct Rect { float x=0,y=0,w=0,h=0;
    float right() const { return x+w; }
    float bottom() const { return y+h; }
    D2D1_RECT_F d2d() const { return D2D1::RectF(x, y, x+w, y+h); }
};

// ---- 主轴对齐 ----
enum class MainAxisAlignment { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
enum class CrossAxisAlignment { Start, Center, End, Stretch };
enum class Axis { Horizontal, Vertical };

// ---- 事件类型 ----
enum class MouseBtn { None, Left, Right, Middle };
struct MouseEvent {
    float x, y;            // 相对 widget 左上
    MouseBtn button;
    int wheel;             // 鼠标滚轮（>0 上滚）
    bool ctrl, shift;
};
struct KeyEvent {
    WPARAM key;            // VK_xxx
    bool ctrl, shift, alt;
};

// ---- Widget 基类 ----
class Widget : public std::enable_shared_from_this<Widget> {
public:
    Widget() = default;
    virtual ~Widget() = default;

    // 测量：给定约束（最大宽高），返回期望尺寸
    virtual Size measure(const Size& max) { return {std::min(max.w,0.0f), std::min(max.h,0.0f)}; }
    // 布局：确定最终位置和尺寸
    virtual void layout(const Rect& bounds) { bounds_ = bounds; }
    // 绘制
    virtual void draw(ID2D1RenderTarget* rt) {}
    // 命中测试：返回接收事件的 widget（this 或子项）
    virtual Widget* hitTest(float x, float y) { return nullptr; }

    // 事件（返回 true 表示已处理）
    virtual bool onMouseDown(const MouseEvent&) { return false; }
    virtual bool onMouseUp(const MouseEvent&) { return false; }
    virtual bool onMouseMove(const MouseEvent&) { return false; }
    virtual bool onMouseWheel(const MouseEvent&) { return false; }
    virtual bool onKey(const KeyEvent&) { return false; }
    virtual bool onChar(wchar_t) { return false; }

    Rect bounds_;  // 最终位置（layout 阶段设置）

    // 标记需要重绘
    void markDirty();
    // 是否可见（控制绘制）
    bool visible = true;
    // 是否参与交互（控制命中测试），默认跟随visible
    // 设为true可使invisible控件仍接收事件（用于透明点击层）
    bool interactive = true;
    // 是否参与布局测量
    bool needsLayout = true;
};

// ---- 单子节点容器 ----
class SingleChildWidget : public Widget {
public:
    WidgetPtr child;
    explicit SingleChildWidget(WidgetPtr c = nullptr) : child(std::move(c)) {}

    Size measure(const Size& max) override {
        if (!child) return {0,0};
        return child->measure(max);
    }
    void layout(const Rect& b) override {
        bounds_ = b;
        if (child) child->layout(b);
    }
    void draw(ID2D1RenderTarget* rt) override {
        if (child) child->draw(rt);
    }
    Widget* hitTest(float x, float y) override {
        if (!child) return nullptr;
        return child->hitTest(x, y);
    }

    // 事件转发：容器自身不处理，交给命中的子控件
    // 坐标转换为相对于子控件的坐标
    bool onMouseDown(const MouseEvent& e) override {
        if (!child) return false;
        auto* h = child->hitTest(e.x, e.y);
        if (!h) return false;
        MouseEvent relE = e;
        relE.x -= h->bounds_.x;
        relE.y -= h->bounds_.y;
        return h->onMouseDown(relE);
    }
    bool onMouseUp(const MouseEvent& e) override {
        if (!child) return false;
        auto* h = child->hitTest(e.x, e.y);
        if (!h) return false;
        MouseEvent relE = e;
        relE.x -= h->bounds_.x;
        relE.y -= h->bounds_.y;
        return h->onMouseUp(relE);
    }
    bool onMouseMove(const MouseEvent& e) override {
        if (!child) return false;
        auto* h = child->hitTest(e.x, e.y);
        if (!h) return false;
        MouseEvent relE = e;
        relE.x -= h->bounds_.x;
        relE.y -= h->bounds_.y;
        return h->onMouseMove(relE);
    }
    bool onMouseWheel(const MouseEvent& e) override {
        if (!child) return false;
        auto* h = child->hitTest(e.x, e.y);
        if (!h) return false;
        MouseEvent relE = e;
        relE.x -= h->bounds_.x;
        relE.y -= h->bounds_.y;
        return h->onMouseWheel(relE);
    }
};

// ---- 多子节点容器 ----
class MultiChildWidget : public Widget {
public:
    std::vector<WidgetPtr> children;
    void addChild(WidgetPtr c) { children.push_back(std::move(c)); }

    Widget* hitTest(float x, float y) override {
        // 从后往前（顶层优先），跳过非交互子项
        for (int i = static_cast<int>(children.size())-1; i >= 0; i--) {
            if (!children[i]->interactive) continue;
            auto* h = children[i]->hitTest(x, y);
            if (h) return h;
        }
        return nullptr;
    }
    void draw(ID2D1RenderTarget* rt) override {
        for (auto& c : children) if (c->visible) c->draw(rt);
    }

    // 事件转发：容器自身不处理，交给命中的子控件（从后往前，顶层优先）
    // 注意：将绝对坐标转换为相对于命中子控件的坐标
    bool onMouseDown(const MouseEvent& e) override {
        auto* h = hitTest(e.x, e.y);
        if (!h) return false;
        // 转换为相对坐标
        MouseEvent relE = e;
        relE.x -= h->bounds_.x;
        relE.y -= h->bounds_.y;
        return h->onMouseDown(relE);
    }
    bool onMouseUp(const MouseEvent& e) override {
        auto* h = hitTest(e.x, e.y);
        if (!h) return false;
        MouseEvent relE = e;
        relE.x -= h->bounds_.x;
        relE.y -= h->bounds_.y;
        return h->onMouseUp(relE);
    }
    bool onMouseMove(const MouseEvent& e) override {
        auto* h = hitTest(e.x, e.y);
        if (!h) return false;
        MouseEvent relE = e;
        relE.x -= h->bounds_.x;
        relE.y -= h->bounds_.y;
        return h->onMouseMove(relE);
    }
    bool onMouseWheel(const MouseEvent& e) override {
        auto* h = hitTest(e.x, e.y);
        if (!h) return false;
        MouseEvent relE = e;
        relE.x -= h->bounds_.x;
        relE.y -= h->bounds_.y;
        return h->onMouseWheel(relE);
    }
};

// ---- Padding ----
class Padding : public SingleChildWidget {
public:
    EdgeInsets pad;
    Padding(EdgeInsets p, WidgetPtr c) : SingleChildWidget(std::move(c)), pad(p) {}

    Size measure(const Size& max) override {
        Size inner = {max.w - pad.l - pad.r, max.h - pad.t - pad.b};
        Size s = child ? child->measure(inner) : Size{0,0};
        return {s.w + pad.l + pad.r, s.h + pad.t + pad.b};
    }
    void layout(const Rect& b) override {
        bounds_ = b;
        if (child) {
            Rect inner = {b.x + pad.l, b.y + pad.t,
                          b.w - pad.l - pad.r, b.h - pad.t - pad.b};
            child->layout(inner);
        }
    }
};

// ---- Center ----
class Center : public SingleChildWidget {
public:
    explicit Center(WidgetPtr c = nullptr) : SingleChildWidget(std::move(c)) {}
    Size measure(const Size& max) override { return child ? child->measure(max) : Size{0,0}; }
    void layout(const Rect& b) override {
        bounds_ = b;
        if (!child) return;
        Size s = child->measure({b.w, b.h});
        child->layout({b.x + (b.w - s.w)/2, b.y + (b.h - s.h)/2, s.w, s.h});
    }
};

// ---- SizedBox ----
class SizedBox : public SingleChildWidget {
public:
    float w = -1, h = -1;  // -1 表示自适应
    SizedBox(float width, float height, WidgetPtr c = nullptr)
        : SingleChildWidget(std::move(c)), w(width), h(height) {}
    explicit SizedBox(WidgetPtr c = nullptr) : SingleChildWidget(std::move(c)) {}
    Size measure(const Size& max) override {
        float mw = (w >= 0) ? std::min(w, max.w) : max.w;
        float mh = (h >= 0) ? std::min(h, max.h) : max.h;
        if (child) return child->measure({mw, mh});
        return {w >= 0 ? std::min(w,max.w) : 0, h >= 0 ? std::min(h,max.h) : 0};
    }
    void layout(const Rect& b) override {
        bounds_ = b;
        if (child) child->layout(b);
    }
};

// ---- Row / Column（Flex 布局）----
class Flex : public MultiChildWidget {
public:
    Axis direction = Axis::Horizontal;
    MainAxisAlignment mainAlign = MainAxisAlignment::Start;
    CrossAxisAlignment crossAlign = CrossAxisAlignment::Center;
    float spacing = 0;
    EdgeInsets padding;
    // Expanded 子项的 flex 比例（按索引）
    std::map<int, float> flex;

    Flex(Axis dir) : direction(dir) {}
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
};

class Row : public Flex {
public:
    Row() : Flex(Axis::Horizontal) {}
};
class Column : public Flex {
public:
    Column() : Flex(Axis::Vertical) {}
};

// ---- Stack（层叠）----
class Stack : public MultiChildWidget {
public:
    void layout(const Rect& b) override {
        bounds_ = b;
        for (auto& c : children) c->layout(b);
    }
};

// ---- Expanded 标记（通过 flex map 实现，无需独立类）----
// 用法：flex->children.push_back(...) 后 flex->flex[index] = 1.0f;

// ---- ClipRRect（圆角裁剪）----
class ClipRRect : public SingleChildWidget {
public:
    float radius = 0;
    ClipRRect(float r, WidgetPtr c) : SingleChildWidget(std::move(c)), radius(r) {}
    void draw(ID2D1RenderTarget* rt) override;
};

// ---- 画刷缓存辅助 ----
class BrushCache {
public:
    static ID2D1SolidColorBrush* get(ID2D1RenderTarget* rt, D2D1_COLOR_F color);
    static void clear();
private:
    static std::map<uint32_t, ComPtr<ID2D1SolidColorBrush>> cache_;
};

// 文本格式缓存
class TextFormatCache {
public:
    static IDWriteTextFormat* get(IDWriteFactory* dw, float size,
                                  const wchar_t* family = L"Microsoft YaHei",
                                  DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
    static void clear();
private:
    static std::map<std::wstring, ComPtr<IDWriteTextFormat>> cache_;
};

}  // namespace meplayer
