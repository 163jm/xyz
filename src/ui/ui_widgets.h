// UI 控件库：Direct2D 自绘控件，复刻 Flutter Material 3 风格
// 包含文本/图标/按钮/容器/导航/输入/滚动/列表/对话框/特殊控件
#pragma once
#include "ui_base.h"
#include "app/theme.h"
#include "app/window_manager.h"
#include "app/global_player.h"
#include <d2d1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>

namespace meplayer {

// ---- 主题管理器（单例）----
// 持有当前 ColorScheme，供所有控件 draw 时取色
class ThemeManager {
public:
    static ThemeManager& instance();
    // 由种子色 + 明暗重新生成方案
    void update(uint32_t seed, bool dark);
    ColorScheme& current();
private:
    ThemeManager();
    ColorScheme scheme_;
};

// ---- 图标种类 ----
enum class IconKind {
    Play, Pause, Next, Prev, Video, Music, Iptv, Settings,
    Search, Refresh, Add, Close, Fullscreen, Volume, Sort,
    Folder, ChevronRight, ChevronLeft, MusicNote, PictureInPicture
};

// 颜色辅助：替换 alpha 通道
inline D2D1_COLOR_F withAlpha(D2D1_COLOR_F c, float a) {
    return D2D1::ColorF(c.r, c.g, c.b, a);
}

// ---- 焦点管理（单控件聚焦，用于 TextField）----
class FocusManager {
public:
    static Widget* focused() { return focused_; }
    static void setFocused(Widget* w) { focused_ = w; }
    static void clear() { focused_ = nullptr; }
private:
    static Widget* focused_;
};

// 交互控件基类前置（HoverTracker 持有其指针）
class InteractiveWidget;

// 全局悬停追踪：dispatch 层在 hitTest 命中变化时调用 setHovered
// 鼠标离开所有控件时调用 clear()。实现在 ui_widgets.cpp（需 InteractiveWidget 完整）
class HoverTracker {
public:
    static InteractiveWidget* current() { return current_; }
    static void setHovered(InteractiveWidget* w);   // 旧悬停项 onMouseLeave，再登记新项
    static void clear();                            // 鼠标离开所有控件
private:
    static InteractiveWidget* current_;
};

// ---- 交互控件基类：统一 hovered_/pressed_ 与鼠标离开处理 ----
// 子类在 onMouseMove 中调用 handleHover()；鼠标离开由 HoverTracker 触发
class InteractiveWidget : public Widget {
public:
    bool hovered_ = false;
    bool pressed_ = false;

    // 在子类 onMouseMove 中调用：标记悬停并登记到全局 tracker
    void handleHover() {
        if (!hovered_) { hovered_ = true; markDirty(); }
        HoverTracker::setHovered(this);
    }
    // 鼠标离开（由 HoverTracker 调用）
    virtual void onMouseLeave() {
        bool changed = hovered_ || pressed_;
        hovered_ = false;
        pressed_ = false;
        if (changed) markDirty();
    }
};

// ============================================================
// 文本与图标
// ============================================================

// ---- 文本控件 ----
class Text : public Widget {
public:
    std::wstring text;
    float fontSize = 14;
    D2D1_COLOR_F color = D2D1::ColorF(0, 0, 0, 1);
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_TEXT_ALIGNMENT textAlignment = DWRITE_TEXT_ALIGNMENT_LEADING;
    bool wrap = true;

    Text(std::wstring t, float size = 14,
         D2D1_COLOR_F c = D2D1::ColorF(0, 0, 0, 1),
         DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL);
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float /*x*/, float /*y*/) override { return nullptr; }  // 纯展示

    // 重置 layoutMaxW_ 使 ensureLayout 重建 IDWriteTextLayout，避免渲染旧文本
    void setText(const std::wstring& t) { text = t; measuredValid_ = false; layoutMaxW_ = -1; markDirty(); }
private:
    ComPtr<IDWriteTextLayout> layout_;
    float layoutMaxW_ = -1;
    Size measured_;
    bool measuredValid_ = false;
    void ensureLayout(float maxWidth, float maxHeight);
};

// ---- 矢量图标（Direct2D 几何路径自绘，不依赖图标字体）----
class Icon : public Widget {
public:
    IconKind kind;
    float size = 24;
    D2D1_COLOR_F color;

    Icon(IconKind k, float sz = 24,
         D2D1_COLOR_F c = D2D1::ColorF(0, 0, 0, 1));
    Size measure(const Size& max) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float /*x*/, float /*y*/) override { return nullptr; }
private:
    // 按 (kind,size) 缓存几何，避免每帧重建
    static ComPtr<ID2D1PathGeometry> geometryFor(IconKind k, float size);
};

// ---- 图片图标（WIC 解码 JPEG/PNG → ID2D1Bitmap）----
class ImageIcon : public Widget {
public:
    std::wstring path;
    float size = 48;
    D2D1_COLOR_F fallbackColor = D2D1::ColorF(0.6f, 0.6f, 0.6f, 1);

    ImageIcon(std::wstring p, float sz = 48);
    Size measure(const Size& max) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float /*x*/, float /*y*/) override { return nullptr; }
private:
    ComPtr<ID2D1Bitmap> bitmap_;
    bool tried_ = false;
    bool ensureBitmap(ID2D1RenderTarget* rt);
};

// ============================================================
// 按钮
// ============================================================

// ---- 图标按钮（圆形/圆角矩形悬停背景）----
class IconButton : public InteractiveWidget {
public:
    std::function<void()> onClick;
    IconKind iconKind;
    float iconSize = 24;
    D2D1_COLOR_F iconColor;
    float buttonSize = 40;
    bool circular = true;  // true=圆形背景，false=圆角矩形

    IconButton(IconKind k, std::function<void()> cb = nullptr);
    Size measure(const Size& max) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
};

// ---- 文本按钮（Material 3：透明背景 + 悬停 overlay）----
class TextButton : public InteractiveWidget {
public:
    std::function<void()> onClick;
    std::wstring label;
    float fontSize = 14;

    TextButton(std::wstring text, std::function<void()> cb = nullptr);
    Size measure(const Size& max) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
private:
    Size measured_;
};

// ---- 填充按钮（Material 3：primary 背景）----
class FilledButton : public InteractiveWidget {
public:
    std::function<void()> onClick;
    std::wstring label;
    float fontSize = 14;

    FilledButton(std::wstring text, std::function<void()> cb = nullptr);
    Size measure(const Size& max) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
private:
    Size measured_;
};

// ============================================================
// 容器
// ============================================================

// ---- Container：padding + 圆角背景 + 边框 + 单子项 ----
class Container : public SingleChildWidget {
public:
    EdgeInsets padding;
    D2D1_COLOR_F color = D2D1::ColorF(0, 0, 0, 0);   // 透明=不画背景
    float radius = 0;
    float borderWidth = 0;
    D2D1_COLOR_F borderColor = D2D1::ColorF(0, 0, 0, 1);

    Container() = default;
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
};

// ---- Card：Material 3 卡片（圆角12 + surfaceVariant + 海拔阴影）----
class Card : public SingleChildWidget {
public:
    float radius = 12;
    float elevation = 3;  // 阴影层数

    explicit Card(WidgetPtr c = nullptr) : SingleChildWidget(std::move(c)) {}
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
};

// ---- ListTile：leading + title + subtitle + trailing，高度约 56 ----
class ListTile : public InteractiveWidget {
public:
    WidgetPtr leading;
    std::wstring title;
    std::wstring subtitle;   // 空则不画
    WidgetPtr trailing;
    std::function<void()> onTap;
    float fontSize = 14;
    float subtitleSize = 12;

    ListTile() = default;
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
private:
    Rect leadingRect_, titleRect_, subtitleRect_, trailingRect_;
};

// ============================================================
// 导航
// ============================================================

// ---- NavigationRail：垂直导航栏（宽 80）----
struct NavItem {
    IconKind icon;
    std::wstring label;
    int index;
};

class NavigationRail : public InteractiveWidget {
public:
    std::vector<NavItem> items;
    int selectedIndex = 0;
    std::function<void(int)> onChanged;
    float width = 80;
    float itemHeight = 56;

    NavigationRail() = default;
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    void onMouseLeave() override;
private:
    int itemAt(float y) const;  // 返回索引或 -1
    int hoveredIndex_ = -1;     // 当前悬停项
};

// ============================================================
// 输入
// ============================================================

// ---- Slider：水平滑动条，可拖拽 ----
class Slider : public InteractiveWidget {
public:
    float value = 0;   // 当前值（已换算到 0~1）
    float min = 0;
    float max = 1;
    std::function<void(float)> onChanged;  // 参数：0~1

    Slider() = default;
    Size measure(const Size& max) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
private:
    bool dragging_ = false;
    float thumbRadius_ = 10;
    float trackHeight_ = 4;
    float valueFromX(float x) const;
};

// ---- ProgressBar：仅展示，不可交互 ----
class ProgressBar : public Widget {
public:
    float value = 0;   // 0~1

    ProgressBar() = default;
    Size measure(const Size& max) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float /*x*/, float /*y*/) override { return nullptr; }
};

// ---- TextField：边框 + 光标 + 键盘输入 ----
class TextField : public InteractiveWidget {
public:
    std::wstring text;
    std::wstring placeholder;
    std::function<void(const std::wstring&)> onChanged;
    std::function<void()> onEnter;
    float fontSize = 14;
    float height = 40;

    TextField() = default;
    Size measure(const Size& max) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onChar(wchar_t ch) override;
    bool onKey(const KeyEvent& e) override;
private:
    bool focused_ = false;
    // 光标闪烁（简单：基于 GetTickCount）
    bool cursorVisible() const;
};

// ============================================================
// 滚动
// ============================================================

// ---- Scrollable：垂直滚动容器 ----
class Scrollable : public Widget {
public:
    WidgetPtr child;
    float scrollOffset = 0;
    bool showScrollbar = true;

    explicit Scrollable(WidgetPtr c = nullptr) : child(std::move(c)) {}
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseWheel(const MouseEvent& e) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
private:
    float contentHeight_ = 0;
    float viewportHeight_ = 0;
    void clampOffset();
};

// ---- Scrollbar：通常内嵌于 Scrollable，独立使用时为可视指示 ----
class Scrollbar : public Widget {
public:
    float contentHeight = 0;
    float scrollOffset = 0;
    D2D1_COLOR_F color = D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.6f);

    Scrollbar() = default;
    Size measure(const Size& max) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float /*x*/, float /*y*/) override { return nullptr; }
};

// ============================================================
// 列表与网格
// ============================================================

// ---- ListView：builder 模式，一次性构建所有项放入 Scrollable ----
class ListView : public Widget {
public:
    int itemCount = 0;
    std::function<WidgetPtr(int)> itemBuilder;
    float spacing = 0;

    ListView() { scroll_ = std::make_shared<Scrollable>(); }
    void rebuild();  // 重新构建所有项

    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseWheel(const MouseEvent& e) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
private:
    std::shared_ptr<Scrollable> scroll_;
    int builtCount_ = -1;
};

// ---- GridView：自适应列数网格，配合 Scrollable 垂直滚动 ----
class GridView : public Widget {
public:
    int itemCount = 0;
    std::function<WidgetPtr(int)> itemBuilder;
    float maxCrossAxisExtent = 240;   // 每格最大宽度
    float childAspectRatio = 16.0f / 10.0f;
    float spacing = 8;
    float mainPadding = 8;

    GridView() { scroll_ = std::make_shared<Scrollable>(); }
    void rebuild();

    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseWheel(const MouseEvent& e) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
private:
    std::shared_ptr<Scrollable> scroll_;
    int builtCount_ = -1;
    int cols_ = 1;               // 当前列数
    float crossAxisExtent_ = 0;  // 实际每格宽度
    void computeCols(float viewportW);
};

// ============================================================
// 对话框
// ============================================================

class Dialog;
// 对话框栈管理（模态）：drawAll/hitTest/事件分发由顶层调用
class DialogManager {
public:
    static DialogManager& instance();
    void push(std::shared_ptr<Dialog> d);
    void pop();
    std::shared_ptr<Dialog> top() const;
    bool empty() const { return stack_.empty(); }
    void arrangeAndDraw(ID2D1RenderTarget* rt);  // 布局+绘制栈顶对话框
    Widget* hitTest(float x, float y);
    bool dispatchMouseDown(const MouseEvent& e);
    bool dispatchMouseUp(const MouseEvent& e);
    bool dispatchMouseMove(const MouseEvent& e);
    bool dispatchMouseWheel(const MouseEvent& e);
    bool dispatchChar(wchar_t ch);
    bool dispatchKey(const KeyEvent& e);
private:
    DialogManager() = default;
    std::vector<std::shared_ptr<Dialog>> stack_;
};

// ---- Dialog：模态对话框 ----
// 继承 Widget（其已派生自 enable_shared_from_this<Widget>），
// show() 通过 shared_from_this() + static_pointer_cast<Dialog> 获取自身 shared_ptr
class Dialog : public Widget {
public:
    std::wstring title;
    WidgetPtr content;
    std::vector<WidgetPtr> actions;
    float dialogWidth = 400;

    Dialog() = default;
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseWheel(const MouseEvent& e) override { return true; }  // 吞掉

    void show();   // 入栈
    void close();  // 出栈
private:
    Rect cardRect_;
    Rect titleRect_, contentRect_, actionsRect_;
    float lastScreenW_ = 0, lastScreenH_ = 0;
};

// ============================================================
// 特殊控件
// ============================================================

// ---- LyricsView：歌词滚动视图，当前行高亮放大，自动居中 ----
class LyricsView : public Widget {
public:
    std::vector<LrcLine> lines;
    int currentIndex = -1;
    float lineHeight = 44;

    LyricsView() = default;
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override { return nullptr; }  // 不拦截子项以外
    bool onMouseWheel(const MouseEvent& e) override;
    void setLines(const std::vector<LrcLine>& l) { lines = l; scrollOffset_ = 0; autoScroll_ = true; markDirty(); }
    void setCurrentIndex(int idx);
private:
    float scrollOffset_ = 0;
    float targetOffset_ = 0;
    bool autoScroll_ = true;
    float lastFrameTime_ = 0;
    void ensureAutoScroll();
};

// ---- VideoView：mpv 视频嵌入挂载点（wid 模式）----
// 创建一个 Win32 子窗口作为 mpv 的 wid 嵌入目标。
// layout 时调整子窗口尺寸；mpv 自行渲染到该子窗口。
class VideoView : public Widget {
public:
    MpvBackend* backend = nullptr;
    D2D1_COLOR_F bgColor = D2D1::ColorF(0, 0, 0, 1);

    VideoView();
    ~VideoView() override;
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;

    // 暴露嵌入子窗口句柄供 backend->init(hwnd) 使用
    HWND hwnd() const { return hwnd_; }
    void ensureHwnd();   // 手动触发子窗口创建（layout 前预创建）

private:
    HWND hwnd_ = nullptr;        // 嵌入子窗口
    void releaseHwnd();
};

// ---- MiniPlayerBar：底部播放栏（高 64），按 MiniBarKind 显示不同内容 ----
class MiniPlayerBar : public InteractiveWidget {
public:
    MiniBarKind kind = MiniBarKind::None;
    std::function<void()> onClick;

    MiniPlayerBar() = default;
    void setKind(MiniBarKind k) { kind = k; markDirty(); }
    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
private:
    // 控制按钮区域（音乐模式下的 prev/play/next）
    Rect prevRect_, playRect_, nextRect_;
    Rect progressRect_;
    void drawMusic(ID2D1RenderTarget* rt, const ColorScheme& sc);
    void drawVideo(ID2D1RenderTarget* rt, const ColorScheme& sc);
    void drawIptv(ID2D1RenderTarget* rt, const ColorScheme& sc);
};

}  // namespace meplayer
