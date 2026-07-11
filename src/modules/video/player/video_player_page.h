// 视频播放页：VideoView + 播放列表 + 控制栏
// 对应原项目 video_player_page.dart
#pragma once
#include "modules/page.h"
#include "core/mpv_backend.h"
#include "ui/ui_widgets.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace meplayer {

class VideoPlayerPage : public Page {
public:
    struct Args {
        std::string url;
        std::string title;
        bool        isLocal = false;
        std::vector<std::string> playlist;
        int          startIndex = 0;
        double       resumePosition = 0;  // 续播进度（秒）
    };

    using OnBack = std::function<void()>;
    explicit VideoPlayerPage(const Args& args, OnBack onBack = nullptr);
    ~VideoPlayerPage() override;

    std::wstring title() const override { return L"视频播放"; }
    void onActive() override;
    void onInactive() override;

    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseWheel(const MouseEvent& e) override;
    bool onKey(const KeyEvent& e) override;

private:
    Args args_;
    OnBack onBack_;
    WidgetPtr root_;
    bool needsRebuild_ = true;

    std::unique_ptr<MpvBackend> backend_;
    std::vector<std::string> playlist_;
    int currentIndex_ = 0;
    std::shared_ptr<VideoView> video_;

    bool controlsVisible_ = true;
    bool fullscreen_ = false;

    void rebuild();
    void applyMpvOptions();
    void playIndex(int idx);
    void togglePlay();
    void seekRelative(double delta);
    void changeVolume(int delta);
    void toggleFullscreen();
    void autoHideControls();
    void reportStateToGlobal();
};

}  // namespace meplayer
