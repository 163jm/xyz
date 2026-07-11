// 视频库页：侧栏(扫描路径) + 工具栏 + 网格
// 对应原项目 video_tab_page.dart
#pragma once
#include "modules/page.h"
#include "core/local_scanner.h"
#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <string>

namespace meplayer {

class VideoLibraryPage : public Page {
public:
    // 播放某视频后回调（HomePage 注入，跳转视频播放页）
    using OnPlayVideo = std::function<void(const std::string& url,
                                           const std::string& title,
                                           bool isLocal)>;
    explicit VideoLibraryPage(OnPlayVideo onPlay = nullptr);

    std::wstring title() const override { return L"视频"; }
    void onActive() override;

    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseWheel(const MouseEvent& e) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;

private:
    OnPlayVideo onPlayVideo_;
    WidgetPtr root_;
    bool needsRebuild_ = true;

    std::vector<std::wstring> scanPaths_;
    std::map<std::wstring, std::vector<MediaItem>> filesByPath_;
    std::vector<MediaItem> allFiles_;
    std::map<std::string, std::string> thumbCache_;  // videoPath(UTF-8) → 相对 appdata 路径
    std::wstring currentPath_;  // 当前选中的扫描路径，空=全部
    int sortMode_ = 0;          // 0=名称 1=时间 2=大小

    void scanAll();
    void rebuild();
    std::vector<MediaItem> displayFiles();
    void addScanPath();
    void playItem(const MediaItem& item);
    void playNetwork();
};

}  // namespace meplayer
