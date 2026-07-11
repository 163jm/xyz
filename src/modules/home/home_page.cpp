#include "modules/home/home_page.h"
#include "modules/settings/settings_page.h"
#include "modules/video/library/video_library_page.h"
#include "modules/iptv/sources/iptv_sources_page.h"
#include "modules/music/library/music_library_page.h"
#include "app/global_player.h"
#include "ui/ui_widgets.h"
#include <memory>

namespace meplayer {

HomePage::HomePage() {
    buildPages();

    // NavigationRail：视频/IPTV/音乐/设置
    rail_ = std::make_shared<NavigationRail>();
    rail_->items = {
        {IconKind::Video,    L"视频", 0},
        {IconKind::Iptv,     L"IPTV", 1},
        {IconKind::Music,    L"音乐", 2},
        {IconKind::Settings, L"设置", 3},
    };
    rail_->selectedIndex = 0;
    rail_->onChanged = [this](int idx) {
        if (idx == current_) return;
        pages_[current_]->onInactive();
        pages_[current_]->visible = false;
        current_ = idx;
        rail_->selectedIndex = idx;
        pages_[current_]->visible = true;
        pages_[current_]->onActive();
        requestRedraw();
    };

    // 底部迷你播放栏
    miniBar_ = std::make_shared<MiniPlayerBar>();
    miniBar_->kind = GlobalPlayer::instance().miniBarKind();
    miniBar_->onClick = [this]() {
        // 根据当前 miniBarKind 跳转对应播放页（由上层 App 处理）
        // 这里暂不实现跳转，仅刷新
        requestRedraw();
    };

    // 全局播放器状态变化时刷新（音乐播放进度等），并同步迷你播放栏应显示的类型
    // 之前这里只 requestRedraw()，从未更新 miniBar_->kind，导致底部迷你播放栏
    // 一直停留在初始值 MiniBarKind::None：既不会在有播放时正确显示，
    // 也会因 measure/draw 未对 None 做特殊处理而画出一条空的灰色背景条。
    GlobalPlayer::instance().addChangedCb([this]() {
        miniBar_->setKind(GlobalPlayer::instance().miniBarKind());
        requestRedraw();
    });

    rebuild();
}

HomePage::~HomePage() {}

void HomePage::buildPages() {
    pages_[0] = std::make_shared<VideoLibraryPage>([this](const std::string& url,
                                                            const std::string& title,
                                                            bool isLocal) {
        // 跳转视频播放页（由 App 层处理）
        if (onPlayVideo) onPlayVideo(url, title, isLocal);
    });
    pages_[1] = std::make_shared<IptvSourcesPage>();
    pages_[2] = std::make_shared<MusicLibraryPage>([this]() {
        if (onPlayMusic) onPlayMusic();
    });
    pages_[3] = std::make_shared<SettingsPage>();
}

void HomePage::rebuild() {
    // 整体布局：Row[NavigationRail(80) + Column[IndexedStack + MiniBar(64)] + 隐藏 VideoView(1)]
    auto contentCol = std::make_shared<Column>();

    // IndexedStack：只显示 current_ 对应的页面
    auto stack = std::make_shared<Stack>();
    for (int i = 0; i < 4; i++) {
        pages_[i]->visible = (i == current_);
        stack->addChild(pages_[i]);
    }
    contentCol->addChild(stack);
    contentCol->flex[0] = 1.0f;

    // 底部播放栏
    contentCol->addChild(miniBar_);

    auto row = std::make_shared<Row>();
    row->addChild(rail_);
    row->flex[0] = 0.0f;  // rail 固定宽

    auto contentWrapper = std::make_shared<SizedBox>();
    contentWrapper->w = -1;  // 自适应填充剩余
    contentWrapper->h = -1;
    contentWrapper->child = contentCol;
    row->addChild(contentWrapper);
    row->flex[1] = 1.0f;

    // 右侧 1x1 隐藏 VideoView（音乐播放器保活）
    auto hidden = std::make_shared<SizedBox>(1, 1);
    row->addChild(hidden);
    row->flex[2] = 0.0f;

    root_ = row;
}

Size HomePage::measure(const Size& max) {
    return {max.w, max.h};
}

void HomePage::layout(const Rect& b) {
    bounds_ = b;
    if (root_) root_->layout(b);
}

void HomePage::draw(ID2D1RenderTarget* rt) {
    if (root_) root_->draw(rt);
}

Widget* HomePage::hitTest(float x, float y) {
    if (root_) return root_->hitTest(x, y);
    return nullptr;
}

bool HomePage::onMouseDown(const MouseEvent& e) {
    return root_ ? root_->onMouseDown(e) : false;
}

bool HomePage::onMouseUp(const MouseEvent& e) {
    return root_ ? root_->onMouseUp(e) : false;
}

bool HomePage::onMouseMove(const MouseEvent& e) {
    return root_ ? root_->onMouseMove(e) : false;
}

bool HomePage::onMouseWheel(const MouseEvent& e) {
    return root_ ? root_->onMouseWheel(e) : false;
}

}  // namespace meplayer
