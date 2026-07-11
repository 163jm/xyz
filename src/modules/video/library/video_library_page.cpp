#include "modules/video/library/video_library_page.h"
#include "ui/ui_widgets.h"
#include "core/storage.h"
#include "core/video_thumbnail.h"
#include "app/data_dir.h"
#include "app/global_player.h"
#include "util/string_util.h"
#include <windows.h>
#include <shlobj.h>
#include <memory>
#include <algorithm>

namespace meplayer {

VideoLibraryPage::VideoLibraryPage(OnPlayVideo onPlay) : onPlayVideo_(std::move(onPlay)) {
    scanPaths_ = Storage::instance().getVideoScanPaths();
    auto cache = Storage::instance().getVideoThumbCache();
    for (auto& kv : cache) thumbCache_[kv.first] = kv.second;
}

void VideoLibraryPage::onActive() {
    if (allFiles_.empty() && !scanPaths_.empty()) scanAll();
    needsRebuild_ = true;
}

Size VideoLibraryPage::measure(const Size& max) { return {max.w, max.h}; }

void VideoLibraryPage::layout(const Rect& b) {
    bounds_ = b;
    if (needsRebuild_) rebuild();
    if (root_) root_->layout(b);
}

void VideoLibraryPage::draw(ID2D1RenderTarget* rt) {
    if (root_) root_->draw(rt);
}

Widget* VideoLibraryPage::hitTest(float x, float y) {
    return root_ ? root_->hitTest(x, y) : nullptr;
}

bool VideoLibraryPage::onMouseWheel(const MouseEvent& e) {
    return root_ ? root_->onMouseWheel(e) : false;
}
bool VideoLibraryPage::onMouseDown(const MouseEvent& e) {
    return root_ ? root_->onMouseDown(e) : false;
}
bool VideoLibraryPage::onMouseUp(const MouseEvent& e) {
    return root_ ? root_->onMouseUp(e) : false;
}
bool VideoLibraryPage::onMouseMove(const MouseEvent& e) {
    return root_ ? root_->onMouseMove(e) : false;
}

std::vector<MediaItem> VideoLibraryPage::displayFiles() {
    if (currentPath_.empty()) return allFiles_;
    auto it = filesByPath_.find(currentPath_);
    return (it != filesByPath_.end()) ? it->second : std::vector<MediaItem>{};
}

void VideoLibraryPage::scanAll() {
    filesByPath_.clear();
    allFiles_.clear();
    for (auto& p : scanPaths_) {
        auto items = LocalScanner::scanVideos(p);
        filesByPath_[p] = items;
        for (auto& it : items) allFiles_.push_back(it);
    }
    // 为缺失缩略图的入队
    for (auto& it : allFiles_) {
        std::string u8path = util::wide_to_utf8(it.path);
        if (thumbCache_.find(u8path) == thumbCache_.end()) {
            VideoThumbnail::instance().enqueue(u8path,
                [this](const std::string& vp, const std::string& tp) {
                    if (!tp.empty()) {
                        thumbCache_[vp] = tp;
                        Storage::instance().setVideoThumb(vp, tp);
                        requestRedraw();
                    }
                });
        }
    }
    needsRebuild_ = true;
}

// Win32 选择文件夹对话框
static std::wstring browseFolder() {
    BROWSEINFOW bi = {};
    bi.lpszTitle = L"选择视频扫描路径";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH];
    if (!SHGetPathFromIDListW(pidl, path)) return L"";
    CoTaskMemFree(pidl);
    return path;
}

void VideoLibraryPage::addScanPath() {
    auto path = browseFolder();
    if (path.empty()) return;
    if (std::find(scanPaths_.begin(), scanPaths_.end(), path) != scanPaths_.end()) return;
    scanPaths_.push_back(path);
    Storage::instance().addVideoScanPath(path);
    scanAll();
}

void VideoLibraryPage::playItem(const MediaItem& item) {
    std::string url = util::wide_to_utf8(item.path);
    std::string title = util::wide_to_utf8(item.name);
    GlobalPlayer::instance().stopMusicForOtherPlayback();
    if (onPlayVideo_) onPlayVideo_(url, title, true);
}

void VideoLibraryPage::playNetwork() {
    // 简化：弹出 Dialog 输入 URL（实际用 DialogManager）
    // 这里直接调用 onPlayVideo_，URL 为空时由 App 层弹对话框
    if (onPlayVideo_) onPlayVideo_("", "", false);
}

void VideoLibraryPage::rebuild() {
    needsRebuild_ = false;
    auto& sc = ThemeManager::instance().current();

    auto row = std::make_shared<Row>();

    // 左侧侧栏 200px
    auto sideCol = std::make_shared<Column>();
    sideCol->spacing = 4;
    auto sideTitle = std::make_shared<Text>(L"扫描路径", 14, sc.onSurfaceVariant);
    sideCol->addChild(std::make_shared<Padding>(sym(16, 12), sideTitle));

    auto allBtn = std::make_shared<ListTile>();
    allBtn->title = L"全部视频";
    allBtn->onTap = [this]() { currentPath_ = L""; needsRebuild_ = true; requestRedraw(); };
    sideCol->addChild(allBtn);

    for (auto& p : scanPaths_) {
        auto name = util::base_name(p);
        auto tile = std::make_shared<ListTile>();
        tile->title = name;
        tile->onTap = [this, p]() { currentPath_ = p; needsRebuild_ = true; requestRedraw(); };
        sideCol->addChild(tile);
    }

    auto addBtn = std::make_shared<IconButton>(IconKind::Add);
    addBtn->onClick = [this]() { addScanPath(); };
    sideCol->addChild(std::make_shared<Padding>(sym(16, 8), addBtn));

    auto sideWrap = std::make_shared<Container>();
    sideWrap->color = sc.surfaceVariant;
    sideWrap->child = std::make_shared<Scrollable>(sideCol);
    row->addChild(sideWrap);

    // 右侧内容区
    auto contentCol = std::make_shared<Column>();

    // 工具栏
    auto toolbar = std::make_shared<Row>();
    toolbar->mainAlign = MainAxisAlignment::SpaceBetween;
    toolbar->padding = all(8);
    auto leftInfo = std::make_shared<Row>();
    leftInfo->spacing = 12;
    leftInfo->addChild(std::make_shared<Text>(
        currentPath_.empty() ? L"全部视频" : util::base_name(currentPath_),
        18, sc.onSurface, DWRITE_FONT_WEIGHT_BOLD));
    auto count = displayFiles().size();
    leftInfo->addChild(std::make_shared<Text>(std::to_wstring(count) + L" 个视频",
                                              13, sc.onSurfaceVariant));
    toolbar->addChild(leftInfo);

    auto tools = std::make_shared<Row>();
    tools->spacing = 8;
    auto refreshBtn = std::make_shared<IconButton>(IconKind::Refresh);
    refreshBtn->onClick = [this]() { scanAll(); requestRedraw(); };
    tools->addChild(refreshBtn);
    auto sortBtn = std::make_shared<IconButton>(IconKind::Sort);
    sortBtn->onClick = [this]() { sortMode_ = (sortMode_ + 1) % 3; needsRebuild_ = true; requestRedraw(); };
    tools->addChild(sortBtn);
    toolbar->addChild(tools);
    contentCol->addChild(toolbar);

    // 网格
    auto files = displayFiles();
    // 排序
    std::sort(files.begin(), files.end(), [this](const MediaItem& a, const MediaItem& b) {
        if (sortMode_ == 0) return a.name < b.name;
        if (sortMode_ == 1) return a.modifiedMs > b.modifiedMs;
        return a.size > b.size;
    });

    auto grid = std::make_shared<GridView>();
    grid->maxCrossAxisExtent = 240;
    grid->childAspectRatio = 16.0f / 10.0f;
    grid->itemCount = static_cast<int>(files.size());
    grid->itemBuilder = [this, files](int i) -> WidgetPtr {
        auto& item = files[i];
        std::string u8path = util::wide_to_utf8(item.path);
        // 卡片：缩略图 + 底部渐变 + 文件名
        auto card = std::make_shared<Card>();
        card->radius = 8;
        auto stack = std::make_shared<Stack>();

        // 缩略图
        WidgetPtr thumbWidget;
        auto it = thumbCache_.find(u8path);
        if (it != thumbCache_.end() && !it->second.empty()) {
            std::wstring abs = AppDataDir::instance().toAbsolute(
                util::utf8_to_wide(it->second));
            auto img = std::make_shared<ImageIcon>(abs);
            thumbWidget = img;
        } else {
            // 占位：深色背景 + 图标
            auto placeholder = std::make_shared<Container>();
            placeholder->color = ThemeManager::instance().current().surfaceVariant;
            auto icon = std::make_shared<Icon>(IconKind::Video);
            icon->size = 32;
            icon->color = ThemeManager::instance().current().onSurfaceVariant;
            placeholder->child = std::make_shared<Center>(icon);
            thumbWidget = placeholder;
        }
        stack->addChild(thumbWidget);

        // 底部渐变 + 文件名
        auto bottomBar = std::make_shared<Container>();
        bottomBar->color = D2D1::ColorF(0, 0, 0, 0.6f);
        auto nameText = std::make_shared<Text>(item.name, 12,
            D2D1::ColorF(1, 1, 1, 1), DWRITE_FONT_WEIGHT_NORMAL);
        nameText->wrap = false;
        bottomBar->child = std::make_shared<Padding>(sym(8, 6), nameText);
        // 置底
        auto bottomWrap = std::make_shared<Column>();
        bottomWrap->mainAlign = MainAxisAlignment::End;
        bottomWrap->addChild(bottomBar);
        stack->addChild(bottomWrap);

        card->child = stack;

        // 点击播放：用 Container 包裹，hitTest 返回 this
        auto clickArea = std::make_shared<ListTile>();
        clickArea->title = item.name;
        clickArea->onTap = [this, item]() { playItem(item); };
        // 覆盖显示为 card
        clickArea->visible = false;  // 隐藏默认绘制
        auto overlay = std::make_shared<Stack>();
        overlay->addChild(card);
        overlay->addChild(clickArea);  // 不可见但接收事件（需在 card 之后，作为顶层命中）
        return overlay;
    };
    contentCol->addChild(grid);
    contentCol->flex[1] = 1.0f;

    row->addChild(contentCol);
    row->flex[1] = 1.0f;

    root_ = row;
}

}  // namespace meplayer
