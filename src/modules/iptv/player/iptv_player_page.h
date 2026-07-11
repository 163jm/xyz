// IPTV 播放页：分屏 视频 + 三列频道/分组/源面板，独立 MpvBackend
// 对应原项目 iptv_player_page.dart
#pragma once
#include "modules/page.h"
#include "core/mpv_backend.h"
#include "core/m3u_parser.h"
#include "core/storage.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace meplayer {

class IptvPlayerPage : public Page {
public:
    // url: M3U 源地址（用于下载/读取）；channelName/groupName: 续播目标（空=首个）
    // sourceIndex: 当前 IPTV 源索引（用于源列与切源）
    using OnBack = std::function<void()>;

    IptvPlayerPage(std::string url, std::string channelName,
                   std::string groupName, int sourceIndex,
                   OnBack onBack = nullptr);
    ~IptvPlayerPage() override;

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
    bool onKey(const KeyEvent& e) override;

private:
    // 入参 / 续播
    std::string initialUrl_;
    std::string channelName_;     // 目标频道（续播）
    std::string groupName_;       // 目标分组（续播）
    int         initialSourceIdx_;
    OnBack      onBack_;

    WidgetPtr root_;
    bool needsRebuild_ = true;

    // ---- 控制器状态 ----
    std::unique_ptr<MpvBackend> backend_;
    std::vector<Storage::IptvSource> sources_;
    std::vector<M3uChannel> allChannels_;
    std::map<std::string, std::vector<M3uChannel>> grouped_;
    std::vector<std::string> groupNames_;
    std::string browseGroup_;     // 当前浏览分组
    std::string playingGroup_;    // 当前播放分组
    std::string playingChannel_;  // 当前播放频道名
    std::vector<std::string> streamUrls_;  // 同频道多源
    int streamIndex_ = 0;
    int sourceIndex_ = 0;
    bool loading_ = false;        // M3U 加载中

    // ---- 子组件引用 ----
    std::shared_ptr<class VideoView> video_;
    std::shared_ptr<class Scrollable> groupScroll_;
    std::shared_ptr<class Scrollable> channelScroll_;
    std::shared_ptr<class Scrollable> sourceScroll_;
    bool overlayVisible_ = true;
    bool fullscreen_ = false;
    UINT_PTR redrawTimer_ = 0;

    // ---- 控制器方法 ----
    void loadSource(int idx);
    void selectChannelInternal(const std::string& group,
                               const std::string& channel, bool autoPlay);
    void playStream(const std::string& url);
    void reportStateToGlobal();
    void applyMpvOptions();
    void rebuildUI();
    void scheduleRebuild() { needsRebuild_ = true; markDirty(); }
    void setFullScreen(bool full);
    void ensureRedrawTimer();
    void killRedrawTimer();

    // 面板构建
    WidgetPtr buildGroupColumn();
    WidgetPtr buildChannelColumn();
    WidgetPtr buildSourceColumn();
    WidgetPtr buildVideoArea();
    WidgetPtr buildChannelPanel();
};

}  // namespace meplayer
