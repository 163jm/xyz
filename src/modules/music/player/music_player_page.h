// 音乐播放页：封面 + 歌词 + 控制栏 + 播放队列
// 对应原项目 music_player_page.dart
// 所有状态从 GlobalPlayer::instance() 读取，不持有独立播放器。
// 通过 GlobalPlayer::addChangedCb 注册回调刷新；定时器驱动进度条重绘。
#pragma once
#include "modules/page.h"
#include <memory>
#include <functional>

namespace meplayer {

class MusicPlayerPage : public Page {
public:
    // 退出回调（首页注入，用于返回上一页 / 切到音乐库页）
    using OnBack = std::function<void()>;

    explicit MusicPlayerPage(OnBack onBack = nullptr);
    ~MusicPlayerPage() override;

    std::wstring title() const override { return L"正在播放"; }
    void onActive() override;
    void onInactive() override;

    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseWheel(const MouseEvent& e) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;

private:
    OnBack onBack_;
    WidgetPtr root_;
    bool needsRebuild_ = true;

    // 子组件引用（跨帧持久，便于刷新内容）
    std::shared_ptr<class Container> coverPanel_;
    std::shared_ptr<class LyricsView> lyrics_;
    std::shared_ptr<class ProgressBar> progress_;
    std::shared_ptr<class Text> curTimeText_;
    std::shared_ptr<class Text> totalTimeText_;
    std::shared_ptr<class Slider> volumeSlider_;
    std::shared_ptr<class IconButton> playBtn_;
    std::shared_ptr<class IconButton> repeatBtn_;
    std::shared_ptr<class TextButton> speedBtn_;
    std::shared_ptr<class Scrollable> queueScroll_;
    std::shared_ptr<class Column> queueCol_;

    // 重绘定时器（驱动进度条/歌词滚动连续刷新）
    UINT_PTR redrawTimer_ = 0;
    // 缓存上次刷新用的元数据标识，避免每帧全量重建
    std::string lastCoverKey_;
    int lastIndex_ = -1;
    std::string lastTitle_;
    std::string lastArtist_;
    // 队列变化检测（避免每帧重建队列列表）
    int lastQueueSize_ = -1;
    int lastQueueCur_ = -1;

    void rebuildUI();
    void scheduleRebuild() { needsRebuild_ = true; markDirty(); }
    void refreshDynamic();          // 每帧/回调时刷新动态内容（进度/歌词/按钮态）
    void refreshQueue();            // 队列变化时重建队列列表
    void ensureRedrawTimer();
    void killRedrawTimer();
};

}  // namespace meplayer
