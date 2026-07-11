// 音乐库页：侧栏(扫描路径) + 工具栏(分类/排序/搜索) + 列表(歌曲/分组)
// 对应原项目 music_tab_page.dart
#pragma once
#include "modules/page.h"
#include "core/local_scanner.h"
#include "core/cover_cache.h"
#include <memory>
#include <vector>
#include <map>
#include <atomic>
#include <functional>
#include <string>

namespace meplayer {

class MusicLibraryPage : public Page {
public:
    // 播放某曲后回调（HomePage 注入，跳转音乐播放页）
    using OnPlayMusic = std::function<void()>;

    explicit MusicLibraryPage(OnPlayMusic onPlay = nullptr);

    std::wstring title() const override { return L"音乐"; }
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
    OnPlayMusic onPlayMusic_;
    WidgetPtr root_;
    bool needsRebuild_ = true;

    std::vector<std::wstring> scanPaths_;
    std::map<std::wstring, std::vector<MediaItem>> filesByPath_;
    std::vector<MediaItem> allFiles_;
    std::wstring selectedPath_;   // 空 = 全部音乐

    enum class Category { Song, Album, Artist } category_ = Category::Song;
    enum class SongSort { Title, Artist, Time } songSort_ = SongSort::Title;
    bool songAsc_ = true;
    enum class GroupSort { Name, Time } groupSort_ = GroupSort::Name;
    bool groupAsc_ = true;
    std::string drillGroup_;      // 下钻的专辑/艺术家名（UTF-8）
    bool drilled_ = false;
    std::wstring searchQuery_;

    // 当前显示列表 + 对应 path（UTF-8，供封面加载）
    std::vector<MediaItem> displayList_;
    std::vector<std::string> displayPaths_;
    std::atomic<int> coverUpTo_{0};

    // 持久滚动容器（保留滚动位置）
    std::shared_ptr<class Scrollable> listScroll_;
    float listAreaH_ = 0;          // 列表可视区高度（用于封面批量加载判断）

    void scanAll();
    void recomputeDisplay();
    void ensureCovers();
    void maybeLoadNextBatch();
    void rebuildUI();
    void scheduleRebuild() { needsRebuild_ = true; markDirty(); }
    void play(const MediaItem& item, int index);

    // 工具栏回调
    void setCategory(Category c);
    void cycleSongSort();
    void toggleSongAsc();
    void cycleGroupSort();
    void toggleGroupAsc();
    void addScanPath();
    void selectPath(const std::wstring& p);

    // 列表项构建
    WidgetPtr buildSongTile(int idx);
    WidgetPtr buildGroupTile(int idx);
    // 分组列表（专辑/艺术家去重）
    struct GroupEntry { std::string name; int count; };
    std::vector<GroupEntry> groupEntries_;
};

}  // namespace meplayer
