// 音乐库页实现
#include "modules/music/library/music_library_page.h"
#include "app/theme.h"
#include "app/window_manager.h"
#include "app/global_player.h"
#include "util/string_util.h"

#include <shobjidl.h>          // IFileOpenDialog（选择文件夹）
#include <wincodec.h>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace meplayer {

// ============================================================
// 本文件内部绘制辅助
// ============================================================
static void fillRoundedRect(ID2D1RenderTarget* rt, const D2D1_RECT_F& r,
                            float radius, D2D1_COLOR_F c) {
    auto* b = BrushCache::get(rt, c);
    rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
}
static inline bool pointInRect(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
static bool containsCI(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    if (hay.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (std::towlower(hay[i + j]) != std::towlower(needle[j])) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

// 选择文件夹（IFileOpenDialog + FOS_PICKFOLDERS）
static std::wstring pickFolder(HWND owner) {
    std::wstring result;
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg)))) return result;
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS);
    if (SUCCEEDED(dlg->Show(owner))) {
        ComPtr<IShellItem> item;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = path;
                CoTaskMemFree(path);
            }
        }
    }
    return result;
}

// ============================================================
// MemoryCover：从内存字节解码封面并绘制（WIC）
// ============================================================
class MemoryCover : public Widget {
public:
    std::vector<uint8_t> bytes;
    float size = 48;
    MemoryCover(std::vector<uint8_t> b, float sz) : bytes(std::move(b)), size(sz) {}
    Size measure(const Size&) override { return {size, size}; }
    void draw(ID2D1RenderTarget* rt) override {
        auto& sc = ThemeManager::instance().current();
        if (!bitmap_ && !tried_) { tried_ = true; decode(rt); }
        if (bitmap_) {
            rt->DrawBitmap(bitmap_.Get(), bounds_.d2d(), 1.0f,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            fillRoundedRect(rt, bounds_.d2d(), 6, sc.surfaceVariant);
            Icon ic(IconKind::MusicNote, size * 0.5f, sc.onSurfaceVariant);
            ic.layout(bounds_); ic.draw(rt);
        }
    }
    Widget* hitTest(float, float) override { return nullptr; }
private:
    ComPtr<ID2D1Bitmap> bitmap_;
    bool tried_ = false;
    void decode(ID2D1RenderTarget* rt) {
        if (bytes.empty()) return;
        ComPtr<IWICImagingFactory> wic;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return;
        ComPtr<IWICStream> stream;
        if (FAILED(wic->CreateStream(&stream))) return;
        if (FAILED(stream->InitializeFromMemory(bytes.data(),
                static_cast<DWORD>(bytes.size())))) return;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr,
                WICDecodeMetadataCacheOnLoad, &decoder))) return;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) return;
        ComPtr<IWICFormatConverter> conv;
        if (FAILED(wic->CreateFormatConverter(&conv))) return;
        if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut))) return;
        rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &bitmap_);
    }
};

// ============================================================
// SongTile：封面 + 标题 + 艺术家·专辑 + 格式徽章，可点击
// ============================================================
class SongTile : public InteractiveWidget {
public:
    WidgetPtr cover;            // 48x48
    std::wstring title;
    std::wstring subtitle;
    std::wstring badge;         // 格式（如 MP3）
    std::function<void()> onTap;

    Size measure(const Size& max) override {
        return {max.w, std::min(60.0f, max.h > 0 ? max.h : 60.0f)};
    }
    void layout(const Rect& b) override {
        bounds_ = b;
        float padX = 12;
        float coverSize = 48;
        coverRect_ = {b.x + padX, b.y + (b.h - coverSize) / 2, coverSize, coverSize};
        float badgeW = badge.empty() ? 0 : 44;
        float textX = coverRect_.right() + 12;
        float textW = b.right() - padX - badgeW - textX;
        titleRect_ = {textX, b.y + 10, textW, 22};
        subRect_   = {textX, b.y + 32, textW, 18};
        badgeRect_ = {b.right() - padX - badgeW, b.y + (b.h - 18) / 2, badgeW, 18};
        if (cover) cover->layout(coverRect_);
    }
    void draw(ID2D1RenderTarget* rt) override {
        auto& sc = ThemeManager::instance().current();
        if (hovered_ || pressed_) {
            float a = pressed_ ? 0.10f : 0.06f;
            fillRoundedRect(rt, bounds_.d2d(), 8, withAlpha(sc.onSurface, a));
        }
        if (cover) cover->draw(rt);
        Text t(title, 14, sc.onSurface, DWRITE_FONT_WEIGHT_MEDIUM);
        t.layout(titleRect_); t.draw(rt);
        if (!subtitle.empty()) {
            Text st(subtitle, 12, sc.onSurfaceVariant);
            st.layout(subRect_); st.draw(rt);
        }
        if (!badge.empty()) {
            fillRoundedRect(rt, badgeRect_.d2d(), 4, withAlpha(sc.primary, 0.12f));
            Text bt(badge, 10, sc.primary);
            bt.textAlignment = DWRITE_TEXT_ALIGNMENT_CENTER;
            bt.layout(badgeRect_); bt.draw(rt);
        }
    }
    Widget* hitTest(float x, float y) override {
        return pointInRect(x, y, bounds_.d2d()) ? this : nullptr;
    }
    bool onMouseDown(const MouseEvent&) override { pressed_ = true; markDirty(); return true; }
    bool onMouseUp(const MouseEvent&) override {
        bool w = pressed_; pressed_ = false; markDirty();
        if (w && onTap) onTap();
        return true;
    }
    bool onMouseMove(const MouseEvent&) override { handleHover(); return true; }
private:
    Rect coverRect_, titleRect_, subRect_, badgeRect_;
};

// ============================================================
// MusicLibraryPage
// ============================================================
MusicLibraryPage::MusicLibraryPage(OnPlayMusic onPlay) : onPlayMusic_(std::move(onPlay)) {
    scanPaths_ = Storage::instance().getMusicScanPaths();
    scanAll();
    recomputeDisplay();
}

void MusicLibraryPage::scanAll() {
    filesByPath_.clear();
    allFiles_.clear();
    for (auto& p : scanPaths_) {
        auto items = LocalScanner::scanMusics(p);
        filesByPath_[p] = items;
        for (auto& it : items) allFiles_.push_back(it);
    }
}

void MusicLibraryPage::onActive() {
    // 重新扫描（路径可能变更）
    auto paths = Storage::instance().getMusicScanPaths();
    if (paths != scanPaths_) {
        scanPaths_ = paths;
        scanAll();
        recomputeDisplay();
    }
    scheduleRebuild();
}

void MusicLibraryPage::selectPath(const std::wstring& p) {
    selectedPath_ = p;
    drilled_ = false;
    drillGroup_.clear();
    recomputeDisplay();
    scheduleRebuild();
}

void MusicLibraryPage::setCategory(Category c) {
    if (category_ == c) return;
    category_ = c;
    drilled_ = false;
    drillGroup_.clear();
    recomputeDisplay();
    scheduleRebuild();
}

void MusicLibraryPage::cycleSongSort() {
    songSort_ = static_cast<SongSort>((static_cast<int>(songSort_) + 1) % 3);
    recomputeDisplay(); scheduleRebuild();
}
void MusicLibraryPage::toggleSongAsc() {
    songAsc_ = !songAsc_; recomputeDisplay(); scheduleRebuild();
}
void MusicLibraryPage::cycleGroupSort() {
    groupSort_ = static_cast<GroupSort>((static_cast<int>(groupSort_) + 1) % 2);
    recomputeDisplay(); scheduleRebuild();
}
void MusicLibraryPage::toggleGroupAsc() {
    groupAsc_ = !groupAsc_; recomputeDisplay(); scheduleRebuild();
}

void MusicLibraryPage::addScanPath() {
    std::wstring p = pickFolder(WindowManager::instance().hwnd());
    if (p.empty()) return;
    Storage::instance().addMusicScanPath(p);
    scanPaths_ = Storage::instance().getMusicScanPaths();
    scanAll();
    recomputeDisplay();
    scheduleRebuild();
}

void MusicLibraryPage::recomputeDisplay() {
    // 候选列表
    std::vector<MediaItem> cand;
    if (selectedPath_.empty()) {
        cand = allFiles_;
    } else {
        auto it = filesByPath_.find(selectedPath_);
        if (it != filesByPath_.end()) cand = it->second;
    }

    // 搜索过滤
    if (!searchQuery_.empty()) {
        std::vector<MediaItem> f;
        for (auto& m : cand) {
            std::wstring t = utf8_to_wide(m.title);
            std::wstring ar = utf8_to_wide(m.artist);
            std::wstring al = utf8_to_wide(m.album);
            if (containsCI(t, searchQuery_) || containsCI(ar, searchQuery_) ||
                containsCI(al, searchQuery_) || containsCI(m.name, searchQuery_)) {
                f.push_back(m);
            }
        }
        cand = std::move(f);
    }

    if (category_ == Category::Song) {
        // 排序
        auto less = [this](const MediaItem& a, const MediaItem& b) {
            int r = 0;
            switch (songSort_) {
            case SongSort::Title:  r = a.title.compare(b.title); if (!r) r = a.name.compare(b.name); break;
            case SongSort::Artist: r = a.artist.compare(b.artist); if (!r) r = a.title.compare(b.title); break;
            case SongSort::Time:   r = (a.modifiedMs < b.modifiedMs) ? -1 : (a.modifiedMs > b.modifiedMs ? 1 : 0); break;
            }
            return songAsc_ ? r < 0 : r > 0;
        };
        std::sort(cand.begin(), cand.end(), less);
        displayList_ = std::move(cand);
        groupEntries_.clear();
    } else {
        // 专辑/艺术家分组
        std::map<std::string, std::pair<int, int64_t>> agg;  // name -> (count, maxModified)
        for (auto& m : cand) {
            std::string key = (category_ == Category::Album) ? m.album : m.artist;
            if (key.empty()) key = "(未知)";
            auto& e = agg[key];
            e.first++;
            if (m.modifiedMs > e.second) e.second = m.modifiedMs;
        }
        groupEntries_.clear();
        for (auto& kv : agg) groupEntries_.push_back({kv.first, kv.second.first});

        // 分组排序：Name 按名称字典序；Time 用该组最大修改时间（agg 中保存）
        std::sort(groupEntries_.begin(), groupEntries_.end(),
            [this, &agg](const GroupEntry& a, const GroupEntry& b) {
                int r;
                if (groupSort_ == GroupSort::Name) {
                    r = a.name.compare(b.name);
                } else {
                    int64_t ta = agg[a.name].second, tb = agg[b.name].second;
                    r = (ta < tb) ? -1 : (ta > tb ? 1 : 0);
                }
                return groupAsc_ ? r < 0 : r > 0;
            });

        if (drilled_ && !drillGroup_.empty()) {
            // 下钻：显示该分组下的歌曲
            std::vector<MediaItem> drilled;
            for (auto& m : cand) {
                std::string key = (category_ == Category::Album) ? m.album : m.artist;
                if (key.empty()) key = "(未知)";
                if (key == drillGroup_) drilled.push_back(m);
            }
            std::sort(drilled.begin(), drilled.end(),
                [this](const MediaItem& a, const MediaItem& b) {
                    int r = a.title.compare(b.title);
                    if (!r) r = a.name.compare(b.name);
                    return songAsc_ ? r < 0 : r > 0;
                });
            displayList_ = std::move(drilled);
        } else {
            displayList_.clear();
        }
    }

    // 重建封面路径列表
    displayPaths_.clear();
    for (auto& m : displayList_) displayPaths_.push_back(wide_to_utf8(m.path));
    coverUpTo_ = 0;
    ensureCovers();
}

void MusicLibraryPage::ensureCovers() {
    if (displayPaths_.empty()) return;
    CoverCache::instance().ensureLoaded(displayPaths_, coverUpTo_, 200,
        [this]() { markDirty(); });
}

void MusicLibraryPage::maybeLoadNextBatch() {
    if (!listScroll_ || displayPaths_.empty()) return;
    float itemH = 60.0f;
    float visEnd = (listScroll_->scrollOffset + listAreaH_) / itemH;
    if (visEnd + 50 > coverUpTo_.load()) {
        CoverCache::instance().loadNextBatch(displayPaths_, coverUpTo_, 200,
            [this]() { markDirty(); });
    }
}

void MusicLibraryPage::play(const MediaItem& item, int index) {
    std::vector<GlobalPlayer::PlayItem> list;
    list.reserve(displayList_.size());
    for (auto& m : displayList_) {
        list.push_back({wide_to_utf8(m.path), wide_to_utf8(m.name)});
    }
    GlobalPlayer::instance().playPlaylist(list, index);
    if (onPlayMusic_) onPlayMusic_();
}

WidgetPtr MusicLibraryPage::buildSongTile(int idx) {
    auto& m = displayList_[idx];
    auto tile = std::make_shared<SongTile>();
    std::string pathU8 = wide_to_utf8(m.path);
    if (CoverCache::instance().has(pathU8)) {
        tile->cover = std::make_shared<MemoryCover>(CoverCache::instance().get(pathU8), 48);
    } else {
        auto ph = std::make_shared<Container>();
        ph->color = ThemeManager::instance().current().surfaceVariant;
        ph->radius = 6;
        ph->child = std::make_shared<Center>(std::make_shared<Icon>(
            IconKind::MusicNote, 24, ThemeManager::instance().current().onSurfaceVariant));
        tile->cover = ph;
    }
    tile->title = m.title.empty() ? m.name : utf8_to_wide(m.title);
    std::wstring sub;
    if (!m.artist.empty()) sub += utf8_to_wide(m.artist);
    if (!m.album.empty()) {
        if (!sub.empty()) sub += L" · ";
        sub += utf8_to_wide(m.album);
    }
    tile->subtitle = sub;
    std::wstring ext = ext_of(m.path);
    if (!ext.empty()) {
        std::wstring b = ext.substr(1);
        for (auto& c : b) c = static_cast<wchar_t>(std::towupper(c));
        tile->badge = b;
    }
    tile->onTap = [this, idx]() { play(displayList_[idx], idx); };
    return tile;
}

WidgetPtr MusicLibraryPage::buildGroupTile(int idx) {
    auto& g = groupEntries_[idx];
    auto tile = std::make_shared<ListTile>();
    tile->title = utf8_to_wide(g.name);
    tile->subtitle = std::to_wstring(g.count) + L" 首";
    tile->fontSize = 14;
    tile->leading = std::make_shared<Icon>(
        category_ == Category::Album ? IconKind::Music : IconKind::Music,
        22, ThemeManager::instance().current().primary);
    tile->trailing = std::make_shared<Icon>(IconKind::ChevronRight, 20,
        ThemeManager::instance().current().onSurfaceVariant);
    tile->onTap = [this, name = g.name]() {
        drillGroup_ = name;
        drilled_ = true;
        recomputeDisplay();
        scheduleRebuild();
    };
    return tile;
}

void MusicLibraryPage::rebuildUI() {
    auto& sc = ThemeManager::instance().current();
    auto root = std::make_shared<Container>();
    root->padding = all(0);
    root->color = sc.background;

    auto row = std::make_shared<Row>();
    row->spacing = 0;
    row->crossAlign = CrossAxisAlignment::Stretch;

    // ============ 左侧栏 200px ============
    auto sidebar = std::make_shared<Container>();
    sidebar->color = sc.surface;
    sidebar->padding = all(0);
    auto sideCol = std::make_shared<Column>();
    sideCol->spacing = 0;
    // 标题
    auto sideHead = std::make_shared<Container>();
    sideHead->padding = sym(16, 14);
    sideHead->child = std::make_shared<Text>(L"音乐库", 16, sc.onSurface,
                                             DWRITE_FONT_WEIGHT_MEDIUM);
    sideCol->addChild(sideHead);
    // 全部音乐
    auto allTile = std::make_shared<ListTile>();
    allTile->title = L"全部音乐";
    allTile->subtitle = std::to_wstring(allFiles_.size()) + L" 首";
    allTile->leading = std::make_shared<Icon>(IconKind::Music, 22, sc.primary);
    allTile->onTap = [this]() { selectPath(L""); };
    {
        auto wrap = std::make_shared<Container>();
        wrap->color = selectedPath_.empty() ? sc.primaryContainer : D2D1::ColorF(0,0,0,0);
        wrap->child = allTile;
        sideCol->addChild(wrap);
    }
    // 各扫描路径
    for (auto& p : scanPaths_) {
        auto t = std::make_shared<ListTile>();
        t->title = base_name(p);
        t->subtitle = p;
        t->fontSize = 13;
        t->leading = std::make_shared<Icon>(IconKind::Folder, 22, sc.onSurfaceVariant);
        t->onTap = [this, p]() { selectPath(p); };
        auto wrap = std::make_shared<Container>();
        wrap->color = (selectedPath_ == p) ? sc.primaryContainer : D2D1::ColorF(0,0,0,0);
        wrap->child = t;
        sideCol->addChild(wrap);
    }
    // 弹性占位 + 添加按钮
    sideCol->addChild(std::make_shared<SizedBox>());
    sideCol->flex[sideCol->children.size() - 1] = 1.0f;
    auto addRow = std::make_shared<Container>();
    addRow->padding = sym(12, 8);
    addRow->child = std::make_shared<TextButton>(L"+ 添加文件夹",
        [this]() { addScanPath(); });
    sideCol->addChild(addRow);

    auto sideScroll = std::make_shared<Scrollable>(sideCol);
    sidebar->child = sideScroll;
    row->addChild(std::make_shared<SizedBox>(200, -1, sidebar));   // index 0

    // ============ 右侧 ============
    auto rightCol = std::make_shared<Column>();
    rightCol->spacing = 0;

    // 工具栏
    auto toolbar = std::make_shared<Container>();
    toolbar->color = sc.surface;
    toolbar->padding = sym(16, 10);
    auto tbRow = std::make_shared<Row>();
    tbRow->spacing = 8;
    tbRow->crossAlign = CrossAxisAlignment::Center;
    // 面包屑
    std::wstring crumb = selectedPath_.empty() ? L"全部音乐" : base_name(selectedPath_);
    if (drilled_) {
        crumb += L" / " + utf8_to_wide(drillGroup_);
    }
    tbRow->addChild(std::make_shared<Text>(crumb, 15, sc.onSurface,
                                           DWRITE_FONT_WEIGHT_MEDIUM));
    // 计数
    int cnt = category_ == Category::Song ? static_cast<int>(displayList_.size())
             : drilled_ ? static_cast<int>(displayList_.size())
                        : static_cast<int>(groupEntries_.size());
    tbRow->addChild(std::make_shared<Text>(std::to_wstring(cnt) + L" 项", 12, sc.onSurfaceVariant));
    // 弹性
    tbRow->addChild(std::make_shared<SizedBox>());
    tbRow->flex[tbRow->children.size() - 1] = 1.0f;
    // 搜索框
    auto search = std::make_shared<TextField>();
    search->placeholder = L"搜索";
    search->text = searchQuery_;
    search->height = 36;
    search->onChanged = [this](const std::wstring& q) {
        searchQuery_ = q;
        recomputeDisplay();
        scheduleRebuild();
    };
    tbRow->addChild(std::make_shared<SizedBox>(180, 36, search));
    // 刷新
    tbRow->addChild(std::make_shared<IconButton>(IconKind::Refresh, [this]{
        scanAll(); recomputeDisplay(); scheduleRebuild();
    }));
    // 分类
    tbRow->addChild(std::make_shared<Text>(L"分类", 12, sc.onSurfaceVariant));
    auto catSong = std::make_shared<TextButton>(L"歌曲",
        [this]{ setCategory(Category::Song); });
    auto catAlbum = std::make_shared<TextButton>(L"专辑",
        [this]{ setCategory(Category::Album); });
    auto catArtist = std::make_shared<TextButton>(L"艺术家",
        [this]{ setCategory(Category::Artist); });
    // 高亮当前分类
    auto makeCatBtn = [&](Category c, std::wstring label) -> WidgetPtr {
        if (category_ == c) return std::make_shared<FilledButton>(label, [this,c]{ setCategory(c); });
        return std::make_shared<TextButton>(label, [this,c]{ setCategory(c); });
    };
    tbRow->addChild(makeCatBtn(Category::Song, L"歌曲"));
    tbRow->addChild(makeCatBtn(Category::Album, L"专辑"));
    tbRow->addChild(makeCatBtn(Category::Artist, L"艺术家"));
    (void)catSong; (void)catAlbum; (void)catArtist;
    // 排序
    std::wstring sortLabel;
    if (category_ == Category::Song) {
        const wchar_t* names[] = {L"标题", L"艺术家", L"时间"};
        sortLabel = L"排序:" + std::wstring(names[static_cast<int>(songSort_)]) +
                    (songAsc_ ? L"↑" : L"↓");
    } else {
        const wchar_t* names[] = {L"名称", L"数量"};
        sortLabel = L"排序:" + std::wstring(names[static_cast<int>(groupSort_)]) +
                    (groupAsc_ ? L"↑" : L"↓");
    }
    auto sortBtn = std::make_shared<TextButton>(sortLabel, [this]{
        if (category_ == Category::Song) {
            // 左键切换字段，无法区分；这里点击切换升降序，长按省略
            toggleSongAsc();
        } else {
            toggleGroupAsc();
        }
    });
    tbRow->addChild(sortBtn);
    // 排序字段切换
    auto sortFieldBtn = std::make_shared<TextButton>(L"⇄", [this]{
        if (category_ == Category::Song) cycleSongSort();
        else cycleGroupSort();
    });
    tbRow->addChild(sortFieldBtn);
    toolbar->child = tbRow;
    rightCol->addChild(toolbar);   // index 0

    // 下钻时显示返回按钮
    if (drilled_) {
        auto backBar = std::make_shared<Container>();
        backBar->padding = sym(16, 4);
        backBar->child = std::make_shared<TextButton>(L"‹ 返回", [this]{
            drilled_ = false; drillGroup_.clear();
            recomputeDisplay(); scheduleRebuild();
        });
        rightCol->addChild(backBar);
    }

    // 列表区
    if (!listScroll_) listScroll_ = std::make_shared<Scrollable>();
    auto listCol = std::make_shared<Column>();
    listCol->spacing = 0;
    if (category_ == Category::Song || drilled_) {
        if (displayList_.empty()) {
            auto empty = std::make_shared<Center>(std::make_shared<Text>(
                L"暂无音乐", 14, sc.onSurfaceVariant));
            listCol->addChild(empty);
        } else {
            for (int i = 0; i < static_cast<int>(displayList_.size()); i++) {
                listCol->addChild(buildSongTile(i));
            }
        }
    } else {
        if (groupEntries_.empty()) {
            auto empty = std::make_shared<Center>(std::make_shared<Text>(
                L"暂无分组", 14, sc.onSurfaceVariant));
            listCol->addChild(empty);
        } else {
            for (int i = 0; i < static_cast<int>(groupEntries_.size()); i++) {
                listCol->addChild(buildGroupTile(i));
            }
        }
    }
    listScroll_->child = listCol;
    auto listWrap = std::make_shared<Padding>(sym(8, 0), listScroll_);
    rightCol->addChild(listWrap);
    rightCol->flex[rightCol->children.size() - 1] = 1.0f;

    row->addChild(rightCol);   // index 1
    row->flex[1] = 1.0f;

    root->child = row;
    root_ = root;
}

Size MusicLibraryPage::measure(const Size& max) {
    if (needsRebuild_) { needsRebuild_ = false; rebuildUI(); }
    return root_ ? root_->measure(max) : Size{max.w, max.h};
}

void MusicLibraryPage::layout(const Rect& b) {
    bounds_ = b;
    if (needsRebuild_) { needsRebuild_ = false; rebuildUI(); }
    if (root_) root_->layout(b);
    // 记录列表区高度供封面批量判断
    if (listScroll_) listAreaH_ = listScroll_->bounds_.h;
}

void MusicLibraryPage::draw(ID2D1RenderTarget* rt) {
    if (root_) root_->draw(rt);
    maybeLoadNextBatch();
}

Widget* MusicLibraryPage::hitTest(float x, float y) {
    return root_ ? root_->hitTest(x, y) : nullptr;
}

bool MusicLibraryPage::onMouseWheel(const MouseEvent& e) {
    return root_ ? root_->onMouseWheel(e) : false;
}
bool MusicLibraryPage::onMouseDown(const MouseEvent& e) {
    return root_ ? root_->onMouseDown(e) : false;
}
bool MusicLibraryPage::onMouseUp(const MouseEvent& e) {
    return root_ ? root_->onMouseUp(e) : false;
}
bool MusicLibraryPage::onMouseMove(const MouseEvent& e) {
    return root_ ? root_->onMouseMove(e) : false;
}

}  // namespace meplayer
