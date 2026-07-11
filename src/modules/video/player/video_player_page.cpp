#include "modules/video/player/video_player_page.h"
#include "ui/ui_widgets.h"
#include "app/global_player.h"
#include "app/app_settings.h"
#include "app/window_manager.h"
#include "core/storage.h"
#include "core/mpv_options.h"
#include "util/string_util.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

namespace meplayer {

using Microsoft::WRL::ComPtr;

VideoPlayerPage::VideoPlayerPage(const Args& args, OnBack onBack)
    : args_(args), onBack_(std::move(onBack)) {
    playlist_ = args.playlist;
    if (playlist_.empty()) playlist_.push_back(args.url);
    currentIndex_ = args.startIndex;
    if (currentIndex_ < 0 || currentIndex_ >= (int)playlist_.size()) currentIndex_ = 0;
}

VideoPlayerPage::~VideoPlayerPage() {
    onInactive();
}

void VideoPlayerPage::onActive() {
    if (backend_) return;  // 已初始化

    GlobalPlayer::instance().stopMusicForOtherPlayback();

    // 确保 VideoView 子窗口已创建（wid 嵌入目标）
    if (needsRebuild_) rebuild();
    if (video_) video_->ensureHwnd();
    HWND videoHwnd = video_ ? video_->hwnd() : nullptr;

    backend_ = std::make_unique<MpvBackend>();
    if (!backend_->init(videoHwnd, WindowManager::instance().hwnd())) {
        backend_.reset();
        return;
    }

    applyMpvOptions();

    // 属性变化回调刷新 UI
    backend_->setPropertyChangedCb([this](const std::string&) {
        requestRedraw();
    });

    playIndex(currentIndex_);
}

void VideoPlayerPage::onInactive() {
    if (backend_) {
        reportStateToGlobal();
        backend_->dispose();
        backend_.reset();
    }
    if (fullscreen_) toggleFullscreen();
}

void VideoPlayerPage::applyMpvOptions() {
    if (!backend_) return;
    auto& s = AppSettings::instance();
    auto opts = MpvOptions::profile(static_cast<int>(s.mpvProfile()));
    // 硬解
    opts.options["hwdec"] = MpvOptions::hwdecValue(s.hardwareDecode());
    // 去交错
    if (s.deinterlace()) {
        auto di = MpvOptions::deinterlace();
        for (auto& kv : di.options) opts.options[kv.first] = kv.second;
    }
    // TS 容错
    if (s.tsResilience()) {
        auto ts = MpvOptions::tsResilience();
        for (auto& kv : ts.options) opts.options[kv.first] = kv.second;
    }
    backend_->applyOptions(opts.options);
}

void VideoPlayerPage::playIndex(int idx) {
    if (idx < 0 || idx >= (int)playlist_.size()) return;
    currentIndex_ = idx;
    std::string url = playlist_[idx];
    if (backend_) backend_->open(url);
    Storage::instance().addRecentFile(url);
    autoHideControls();
}

void VideoPlayerPage::togglePlay() {
    if (backend_) backend_->playPause();
}

void VideoPlayerPage::seekRelative(double delta) {
    if (backend_) backend_->seek(backend_->position() + delta);
}

void VideoPlayerPage::changeVolume(int delta) {
    if (!backend_) return;
    int v = backend_->volume() + delta;
    v = std::max(0, std::min(100, v));
    backend_->setVolume(v);
}

void VideoPlayerPage::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    WindowManager::instance().setFullScreen(fullscreen_);
    needsRebuild_ = true;
    requestRedraw();
}

void VideoPlayerPage::autoHideControls() {
    controlsVisible_ = true;
    requestRedraw();
    // 简化：不做定时隐藏，实际可用 SetTimer
}

void VideoPlayerPage::reportStateToGlobal() {
    if (!backend_) return;
    VideoSnapshot snap;
    if (currentIndex_ >= 0 && currentIndex_ < (int)playlist_.size()) {
        snap.url = playlist_[currentIndex_];
    }
    snap.title = args_.title;
    snap.isLocal = args_.isLocal;
    snap.position = backend_->position();
    snap.duration = backend_->duration();
    snap.playlist = playlist_;
    snap.startIndex = currentIndex_;
    GlobalPlayer::instance().saveVideoSnapshot(snap);
}

Size VideoPlayerPage::measure(const Size& max) { return {max.w, max.h}; }

void VideoPlayerPage::layout(const Rect& b) {
    bounds_ = b;
    if (needsRebuild_) rebuild();
    if (root_) root_->layout(b);
}

void VideoPlayerPage::draw(ID2D1RenderTarget* rt) {
    if (root_) root_->draw(rt);
}

Widget* VideoPlayerPage::hitTest(float x, float y) {
    return root_ ? root_->hitTest(x, y) : nullptr;
}

bool VideoPlayerPage::onMouseDown(const MouseEvent& e) {
    return root_ ? root_->onMouseDown(e) : false;
}
bool VideoPlayerPage::onMouseUp(const MouseEvent& e) {
    return root_ ? root_->onMouseUp(e) : false;
}
bool VideoPlayerPage::onMouseMove(const MouseEvent& e) {
    return root_ ? root_->onMouseMove(e) : false;
}
bool VideoPlayerPage::onMouseWheel(const MouseEvent& e) {
    if (root_ && root_->onMouseWheel(e)) return true;
    // 滚轮调音量
    if (e.wheel > 0) changeVolume(5);
    else if (e.wheel < 0) changeVolume(-5);
    return true;
}

bool VideoPlayerPage::onKey(const KeyEvent& e) {
    switch (e.key) {
    case VK_SPACE: togglePlay(); return true;
    case VK_LEFT:  seekRelative(-5); return true;
    case VK_RIGHT: seekRelative(5); return true;
    case VK_UP:    changeVolume(5); return true;
    case VK_DOWN:  changeVolume(-5); return true;
    case VK_ESCAPE:
        if (fullscreen_) toggleFullscreen();
        else if (onBack_) onBack_();
        return true;
    case 'F': case 'f': toggleFullscreen(); return true;
    }
    return false;
}

void VideoPlayerPage::rebuild() {
    needsRebuild_ = false;
    auto& sc = ThemeManager::instance().current();

    auto row = std::make_shared<Row>();

    // 左侧视频区
    auto videoArea = std::make_shared<Stack>();
    // VideoView 绑定 backend
    auto videoView = std::make_shared<VideoView>();
    videoView->backend = backend_.get();
    video_ = videoView;
    videoArea->addChild(videoView);

    // 控制栏（悬停显隐，简化为始终显示）
    if (controlsVisible_) {
        auto controlsCol = std::make_shared<Column>();
        controlsCol->mainAlign = MainAxisAlignment::End;

        // 底栏
        auto bottomBar = std::make_shared<Container>();
        bottomBar->color = D2D1::ColorF(0, 0, 0, 0.6f);
        auto bar = std::make_shared<Row>();
        bar->spacing = 8;
        bar->padding = sym(12, 8);

        // 进度条
        auto progress = std::make_shared<ProgressBar>();
        if (backend_ && backend_->duration() > 0) {
            progress->value = static_cast<float>(backend_->position() / backend_->duration());
        }
        bar->addChild(progress);
        bar->flex[0] = 1.0f;

        // 时间
        auto time = std::make_shared<Text>(
            util::utf8_to_wide(util::format_time(backend_ ? backend_->position() : 0) +
                               " / " + util::format_time(backend_ ? backend_->duration() : 0)),
            12, D2D1::ColorF(1, 1, 1, 1));
        bar->addChild(time);

        // 按钮
        auto prevBtn = std::make_shared<IconButton>(IconKind::Prev);
        prevBtn->onClick = [this]() { seekRelative(-10); };
        prevBtn->iconSize = 20;
        bar->addChild(prevBtn);
        auto playBtn = std::make_shared<IconButton>(IconKind::Play);
        playBtn->onClick = [this]() { togglePlay(); };
        playBtn->iconSize = 20;
        bar->addChild(playBtn);
        auto nextBtn = std::make_shared<IconButton>(IconKind::Next);
        nextBtn->onClick = [this]() { seekRelative(10); };
        nextBtn->iconSize = 20;
        bar->addChild(nextBtn);
        auto fsBtn = std::make_shared<IconButton>(IconKind::Fullscreen);
        fsBtn->onClick = [this]() { toggleFullscreen(); };
        fsBtn->iconSize = 20;
        bar->addChild(fsBtn);

        bottomBar->child = bar;
        controlsCol->addChild(bottomBar);
        videoArea->addChild(controlsCol);
    }

    row->addChild(videoArea);
    row->flex[0] = 1.0f;

    // 右侧播放列表（全屏时隐藏）
    if (!fullscreen_) {
        auto listCol = std::make_shared<Column>();
        listCol->padding = all(8);
        listCol->spacing = 4;
        listCol->addChild(std::make_shared<Text>(L"播放列表", 14,
            sc.onSurface, DWRITE_FONT_WEIGHT_BOLD));

        auto listScroll = std::make_shared<Scrollable>(listCol);
        for (int i = 0; i < (int)playlist_.size(); i++) {
            auto name = playlist_[i];
            auto pos = name.find_last_of("/\\");
            if (pos != std::string::npos) name = name.substr(pos + 1);
            auto tile = std::make_shared<ListTile>();
            tile->title = util::utf8_to_wide(name);
            tile->onTap = [this, i]() { playIndex(i); };
            listCol->addChild(tile);
        }

        auto wrap = std::make_shared<Container>();
        wrap->color = sc.surface;
        wrap->child = listScroll;
        auto sized = std::make_shared<SizedBox>(260, -1, wrap);
        row->addChild(sized);
    }

    root_ = row;
}

}  // namespace meplayer
