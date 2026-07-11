// 音乐播放页实现
#include "modules/music/player/music_player_page.h"
#include "app/theme.h"
#include "app/window_manager.h"
#include "app/global_player.h"
#include "util/string_util.h"

#include <wincodec.h>
#include <algorithm>
#include <cmath>

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

// 定时器回调：仅触发主窗口重绘（驱动进度条/歌词连续刷新）
static VOID CALLBACK redrawTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    WindowManager::instance().requestRedraw();
}

// ============================================================
// MemoryCover：从内存字节解码封面并绘制（WIC）
// 与 music_library_page.cpp 中实现一致（文件局部，避免跨编译单元依赖）
// ============================================================
class MemoryCover : public Widget {
public:
    std::vector<uint8_t> bytes;
    float size = 220;
    MemoryCover(std::vector<uint8_t> b, float sz) : bytes(std::move(b)), size(sz) {}
    Size measure(const Size&) override { return {size, size}; }
    void draw(ID2D1RenderTarget* rt) override {
        auto& sc = ThemeManager::instance().current();
        if (!bitmap_ && !tried_) { tried_ = true; decode(rt); }
        if (bitmap_) {
            rt->DrawBitmap(bitmap_.Get(), bounds_.d2d(), 1.0f,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            fillRoundedRect(rt, bounds_.d2d(), 12, sc.surfaceVariant);
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
// QueueTile：队列项（高亮当前播放）
// ============================================================
class QueueTile : public InteractiveWidget {
public:
    std::wstring title;
    std::wstring subtitle;
    bool current = false;
    std::function<void()> onTap;

    Size measure(const Size& max) override {
        return {max.w, std::min(48.0f, max.h > 0 ? max.h : 48.0f)};
    }
    void layout(const Rect& b) override {
        bounds_ = b;
        titleRect_ = {b.x + 14, b.y + 7, b.w - 28, 20};
        subRect_   = {b.x + 14, b.y + 26, b.w - 28, 16};
    }
    void draw(ID2D1RenderTarget* rt) override {
        auto& sc = ThemeManager::instance().current();
        if (current) {
            fillRoundedRect(rt, bounds_.d2d(), 6, sc.primaryContainer);
        } else if (hovered_ || pressed_) {
            float a = pressed_ ? 0.10f : 0.06f;
            fillRoundedRect(rt, bounds_.d2d(), 6, withAlpha(sc.onSurface, a));
        }
        Text t(title, 13, current ? sc.onPrimaryContainer : sc.onSurface,
               DWRITE_FONT_WEIGHT_MEDIUM);
        t.layout(titleRect_); t.draw(rt);
        if (!subtitle.empty()) {
            Text st(subtitle, 11, current ? sc.onPrimaryContainer : sc.onSurfaceVariant);
            st.layout(subRect_); st.draw(rt);
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
    Rect titleRect_, subRect_;
};

// ============================================================
// MusicPlayerPage
// ============================================================
MusicPlayerPage::MusicPlayerPage(OnBack onBack) : onBack_(std::move(onBack)) {}

MusicPlayerPage::~MusicPlayerPage() {
    killRedrawTimer();
}

void MusicPlayerPage::onActive() {
    // 注册全局播放器变更回调：用 weak_ptr 守卫，避免页面析构后悬空
    auto self = std::weak_ptr<Widget>(shared_from_this());
    GlobalPlayer::instance().addChangedCb([self]() {
        if (auto sp = self.lock()) {
            // 页面仍存活：标记需重建并触发重绘
            auto* page = static_cast<MusicPlayerPage*>(sp.get());
            page->needsRebuild_ = true;
            page->markDirty();
        }
    });

    ensureRedrawTimer();
    needsRebuild_ = true;
    markDirty();
}

void MusicPlayerPage::onInactive() {
    killRedrawTimer();
}

void MusicPlayerPage::ensureRedrawTimer() {
    if (!redrawTimer_) redrawTimer_ = SetTimer(nullptr, 0, 33, &redrawTimerProc);
}
void MusicPlayerPage::killRedrawTimer() {
    if (redrawTimer_) { KillTimer(nullptr, redrawTimer_); redrawTimer_ = 0; }
}

// ============================================================
// rebuildUI：构建静态结构（左封面 / 中歌词+控制 / 右队列）
// ============================================================
void MusicPlayerPage::rebuildUI() {
    auto& sc = ThemeManager::instance().current();
    auto& gp = GlobalPlayer::instance();
    auto root = std::make_shared<Container>();
    root->padding = all(0);
    root->color = sc.background;

    auto row = std::make_shared<Row>();
    row->spacing = 0;
    row->crossAlign = CrossAxisAlignment::Stretch;

    // ============ 左侧 300px _CoverPanel ============
    coverPanel_ = std::make_shared<Container>();
    coverPanel_->color = sc.surface;
    coverPanel_->padding = all(0);
    auto coverCol = std::make_shared<Column>();
    coverCol->spacing = 12;
    coverCol->mainAlign = MainAxisAlignment::Start;
    coverCol->crossAlign = CrossAxisAlignment::Center;
    // 顶部弹性（让封面垂直居中偏上）
    coverCol->addChild(std::make_shared<SizedBox>());
    coverCol->flex[0] = 0.8f;
    // 封面占位（refreshDynamic 中按 currentMeta().coverBytes 替换）
    auto coverBox = std::make_shared<SizedBox>(220, 220);
    coverCol->addChild(coverBox);
    // 歌名（refreshDynamic 中刷新）
    auto titleText = std::make_shared<Text>(L"无歌曲", 18, sc.onSurface,
                                            DWRITE_FONT_WEIGHT_MEDIUM);
    titleText->textAlignment = DWRITE_TEXT_ALIGNMENT_CENTER;
    auto titleWrap = std::make_shared<SizedBox>(280, 26,
        std::make_shared<Center>(titleText));
    coverCol->addChild(titleWrap);
    // 艺术家
    auto artistText = std::make_shared<Text>(L"", 13, sc.onSurfaceVariant);
    artistText->textAlignment = DWRITE_TEXT_ALIGNMENT_CENTER;
    auto artistWrap = std::make_shared<SizedBox>(280, 20,
        std::make_shared<Center>(artistText));
    coverCol->addChild(artistWrap);
    // 注：封面/标题/艺术家在 refreshDynamic 中按当前曲目动态重建
    coverPanel_->child = coverCol;
    row->addChild(std::make_shared<SizedBox>(300, -1, coverPanel_));   // index 0

    // ============ 中间 _RightPanel ============
    auto rightCol = std::make_shared<Column>();
    rightCol->spacing = 0;
    rightCol->crossAlign = CrossAxisAlignment::Stretch;

    // 歌词区（弹性）
    lyrics_ = std::make_shared<LyricsView>();
    lyrics_->lines = gp.currentMeta().lrcLines;
    lyrics_->currentIndex = gp.currentLrcIdx();
    auto lyricsWrap = std::make_shared<Container>();
    lyricsWrap->padding = sym(24, 16);
    lyricsWrap->child = lyrics_;
    rightCol->addChild(lyricsWrap);   // index 0
    rightCol->flex[0] = 1.0f;

    // 控制栏 _ControlsBar
    auto ctrlCol = std::make_shared<Column>();
    ctrlCol->spacing = 8;
    ctrlCol->crossAlign = CrossAxisAlignment::Stretch;
    // 进度条
    progress_ = std::make_shared<ProgressBar>();
    progress_->value = 0;
    auto progWrap = std::make_shared<Container>();
    progWrap->padding = sym(24, 0);
    progWrap->child = progress_;
    ctrlCol->addChild(progWrap);   // index 0
    // 时间行
    auto timeRow = std::make_shared<Row>();
    timeRow->spacing = 8;
    timeRow->crossAlign = CrossAxisAlignment::Center;
    timeRow->mainAlign = MainAxisAlignment::Start;
    curTimeText_ = std::make_shared<Text>(L"0:00", 12, sc.onSurfaceVariant);
    timeRow->addChild(std::make_shared<Padding>(sym(24, 0), curTimeText_));
    timeRow->addChild(std::make_shared<SizedBox>());   // 弹性
    timeRow->flex[timeRow->children.size() - 1] = 1.0f;
    totalTimeText_ = std::make_shared<Text>(L"0:00", 12, sc.onSurfaceVariant);
    timeRow->addChild(std::make_shared<Padding>(sym(24, 0), totalTimeText_));
    ctrlCol->addChild(timeRow);   // index 1
    // 按钮行（循环 / 上一首 / 播放暂停 / 下一首 / 速度 + 音量）
    auto btnRow = std::make_shared<Row>();
    btnRow->spacing = 12;
    btnRow->crossAlign = CrossAxisAlignment::Center;
    btnRow->mainAlign = MainAxisAlignment::Center;
    // 循环模式
    repeatBtn_ = std::make_shared<IconButton>(IconKind::Refresh, [this]{
        GlobalPlayer::instance().cycleRepeat();
    });
    btnRow->addChild(repeatBtn_);
    // 上一首
    btnRow->addChild(std::make_shared<IconButton>(IconKind::Prev, [this]{
        GlobalPlayer::instance().prev();
    }));
    // 播放/暂停
    playBtn_ = std::make_shared<IconButton>(IconKind::Play, [this]{
        GlobalPlayer::instance().togglePlay();
    });
    playBtn_->iconSize = 32;
    playBtn_->buttonSize = 56;
    playBtn_->iconColor = sc.primary;
    btnRow->addChild(playBtn_);
    // 下一首
    btnRow->addChild(std::make_shared<IconButton>(IconKind::Next, [this]{
        GlobalPlayer::instance().next();
    }));
    // 速度
    speedBtn_ = std::make_shared<TextButton>(L"1.0x", [this]{
        auto& gp2 = GlobalPlayer::instance();
        double cur = gp2.musicSpeed();
        // 循环：0.5 → 0.75 → 1.0 → 1.25 → 1.5 → 2.0 → 0.5
        static const double speeds[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
        const int n = sizeof(speeds) / sizeof(speeds[0]);
        int idx = 0;
        for (int i = 0; i < n; i++) {
            if (std::fabs(speeds[i] - cur) < 0.01) { idx = i; break; }
        }
        idx = (idx + 1) % n;
        gp2.setSpeed(speeds[idx]);
    });
    btnRow->addChild(speedBtn_);
    ctrlCol->addChild(std::make_shared<Padding>(sym(0, 8), btnRow));   // index 2
    // 音量行
    auto volRow = std::make_shared<Row>();
    volRow->spacing = 8;
    volRow->crossAlign = CrossAxisAlignment::Center;
    volRow->mainAlign = MainAxisAlignment::Start;
    volRow->addChild(std::make_shared<Padding>(sym(24, 0),
        std::make_shared<Icon>(IconKind::Volume, 18, sc.onSurfaceVariant)));
    volumeSlider_ = std::make_shared<Slider>();
    volumeSlider_->value = gp.musicVolume() / 100.0f;
    volumeSlider_->onChanged = [](float v) {
        GlobalPlayer::instance().setVolume(static_cast<int>(v * 100));
    };
    auto volWrap = std::make_shared<SizedBox>(160, 24, volumeSlider_);
    volRow->addChild(volWrap);
    ctrlCol->addChild(std::make_shared<Padding>(sym(0, 8), volRow));   // index 3
    // 底部留白
    ctrlCol->addChild(std::make_shared<SizedBox>(0, 16));

    rightCol->addChild(ctrlCol);   // index 1
    row->addChild(rightCol);   // index 1
    row->flex[1] = 1.0f;

    // ============ 右侧 320px _QueuePanel ============
    auto queuePanel = std::make_shared<Container>();
    queuePanel->color = sc.surface;
    queuePanel->padding = all(0);
    auto queueCol = std::make_shared<Column>();
    queueCol->spacing = 0;
    // 标题
    auto qHead = std::make_shared<Container>();
    qHead->padding = sym(16, 14);
    qHead->child = std::make_shared<Text>(L"播放队列", 15, sc.onSurface,
                                          DWRITE_FONT_WEIGHT_MEDIUM);
    queueCol->addChild(qHead);
    // 队列列表
    queueCol_ = std::make_shared<Column>();
    queueCol_->spacing = 0;
    queueScroll_ = std::make_shared<Scrollable>(queueCol_);
    queueCol->addChild(queueScroll_);
    queueCol->flex[queueCol->children.size() - 1] = 1.0f;
    queuePanel->child = queueCol;
    row->addChild(std::make_shared<SizedBox>(320, -1, queuePanel));   // index 2

    root->child = row;
    root_ = root;

    // 初始填充动态内容
    refreshDynamic();
    refreshQueue();
}

// ============================================================
// refreshDynamic：每帧/回调时刷新动态内容（避免重建整棵树）
// ============================================================
void MusicPlayerPage::refreshDynamic() {
    if (!root_) return;
    auto& gp = GlobalPlayer::instance();
    auto& sc = ThemeManager::instance().current();

    // 进度条 / 时间
    double pos = gp.musicPosition();
    double dur = gp.musicDuration();
    if (progress_) {
        float v = (dur > 0) ? static_cast<float>(pos / dur) : 0.0f;
        if (v < 0) v = 0; if (v > 1) v = 1;
        progress_->value = v;
    }
    if (curTimeText_) curTimeText_->setText(utf8_to_wide(format_time(pos)));
    if (totalTimeText_) totalTimeText_->setText(utf8_to_wide(format_time(dur)));

    // 歌词索引
    int li = gp.currentLrcIdx();
    if (lyrics_) {
        // 歌词内容变更（切歌时）才重设 lines
        if (lyrics_->lines.size() != gp.currentMeta().lrcLines.size() &&
            !gp.currentMeta().lrcLines.empty()) {
            lyrics_->setLines(gp.currentMeta().lrcLines);
        }
        if (li != lyrics_->currentIndex) lyrics_->setCurrentIndex(li);
    }

    // 播放/暂停图标
    bool playing = gp.musicIsPlaying();
    if (playBtn_) {
        IconKind want = playing ? IconKind::Pause : IconKind::Play;
        if (playBtn_->iconKind != want) {
            playBtn_->iconKind = want;
            playBtn_->markDirty();
        }
    }

    // 循环模式按钮（用 iconColor 区分；无专用图标，借 Refresh + 颜色提示）
    if (repeatBtn_) {
        // 列表=primary，随机=secondary，单曲=error 红
        D2D1_COLOR_F c;
        switch (gp.repeatMode()) {
        case RepeatMode::List:     c = sc.primary; break;
        case RepeatMode::Shuffle:  c = sc.secondary; break;
        case RepeatMode::One:      c = sc.error; break;
        default:                   c = sc.onSurfaceVariant; break;
        }
        if (repeatBtn_->iconColor.r != c.r || repeatBtn_->iconColor.g != c.g ||
            repeatBtn_->iconColor.b != c.b) {
            repeatBtn_->iconColor = c;
            repeatBtn_->markDirty();
        }
    }

    // 速度按钮
    double sp = gp.musicSpeed();
    if (speedBtn_) {
        std::wstring lbl;
        if (std::fabs(sp - 0.5)  < 0.01) lbl = L"0.5x";
        else if (std::fabs(sp - 0.75) < 0.01) lbl = L"0.75x";
        else if (std::fabs(sp - 1.0)  < 0.01) lbl = L"1.0x";
        else if (std::fabs(sp - 1.25) < 0.01) lbl = L"1.25x";
        else if (std::fabs(sp - 1.5)  < 0.01) lbl = L"1.5x";
        else if (std::fabs(sp - 2.0)  < 0.01) lbl = L"2.0x";
        else {
            wchar_t buf[16];
            swprintf(buf, 16, L"%.2fx", sp);
            lbl = buf;
        }
        if (speedBtn_->label != lbl) speedBtn_->label = lbl;
    }

    // 音量滑块（避免拖拽中回写）
    int vol = gp.musicVolume();
    if (volumeSlider_ && std::fabs(volumeSlider_->value * 100 - vol) > 1.5f) {
        volumeSlider_->value = vol / 100.0f;
        volumeSlider_->markDirty();
    }

    // 封面 / 标题 / 艺术家 变更时重建 coverPanel_ 的封面/标题部分
    auto& meta = gp.currentMeta();
    bool metaChanged = (meta.title != lastTitle_) || (meta.artist != lastArtist_) ||
                       (lastIndex_ != gp.musicIndex());
    // 封面字节指针变化时也重建（用 size + 首 16 字节做 key）
    std::string coverKey;
    if (!meta.coverBytes.empty()) {
        coverKey.reserve(20);
        coverKey.append(reinterpret_cast<const char*>(meta.coverBytes.data()),
                        std::min<size_t>(16, meta.coverBytes.size()));
        coverKey += ":" + std::to_string(meta.coverBytes.size());
    }
    if (metaChanged || coverKey != lastCoverKey_) {
        lastTitle_ = meta.title;
        lastArtist_ = meta.artist;
        lastCoverKey_ = coverKey;
        lastIndex_ = gp.musicIndex();

        // 重建 coverPanel_ 的子树（封面 + 标题 + 艺术家）
        auto coverCol = std::make_shared<Column>();
        coverCol->spacing = 12;
        coverCol->mainAlign = MainAxisAlignment::Start;
        coverCol->crossAlign = CrossAxisAlignment::Center;
        coverCol->addChild(std::make_shared<SizedBox>());
        coverCol->flex[0] = 0.8f;
        // 封面
        WidgetPtr coverChild;
        if (!meta.coverBytes.empty()) {
            coverChild = std::make_shared<MemoryCover>(meta.coverBytes, 220);
        } else {
            auto ph = std::make_shared<Container>();
            ph->color = sc.surfaceVariant;
            ph->radius = 12;
            ph->child = std::make_shared<Center>(std::make_shared<Icon>(
                IconKind::MusicNote, 80, sc.onSurfaceVariant));
            coverChild = std::make_shared<SizedBox>(220, 220, ph);
        }
        coverCol->addChild(coverChild);
        // 标题
        std::wstring titleW = meta.title.empty() ? L"(未知)" : utf8_to_wide(meta.title);
        auto titleText = std::make_shared<Text>(titleW, 18, sc.onSurface,
                                                DWRITE_FONT_WEIGHT_MEDIUM);
        titleText->textAlignment = DWRITE_TEXT_ALIGNMENT_CENTER;
        titleText->wrap = false;
        coverCol->addChild(std::make_shared<SizedBox>(280, 26,
            std::make_shared<Center>(titleText)));
        // 艺术家
        std::wstring artistW = meta.artist.empty() ? L"" : utf8_to_wide(meta.artist);
        auto artistText = std::make_shared<Text>(artistW, 13, sc.onSurfaceVariant);
        artistText->textAlignment = DWRITE_TEXT_ALIGNMENT_CENTER;
        artistText->wrap = false;
        coverCol->addChild(std::make_shared<SizedBox>(280, 20,
            std::make_shared<Center>(artistText)));
        coverPanel_->child = coverCol;
        // 立即对新的子树布局（draw 中替换 child，需手动 layout 才能正确绘制）
        if (coverPanel_->bounds_.w > 0) {
            coverPanel_->layout(coverPanel_->bounds_);
        }
        needsRebuild_ = false;   // 仅子树替换，不必整树重建
    }
}

// ============================================================
// refreshQueue：重建播放队列列表
// ============================================================
void MusicPlayerPage::refreshQueue() {
    if (!queueCol_) return;
    auto& gp = GlobalPlayer::instance();
    auto& sc = ThemeManager::instance().current();
    auto& list = gp.playlist();
    int cur = gp.musicIndex();
    queueCol_->children.clear();
    queueCol_->flex.clear();
    if (list.empty()) {
        queueCol_->addChild(std::make_shared<Center>(std::make_shared<Text>(
            L"队列为空", 13, sc.onSurfaceVariant)));
        if (queueScroll_ && queueScroll_->bounds_.w > 0) {
            queueScroll_->layout(queueScroll_->bounds_);
        }
        return;
    }
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        auto t = std::make_shared<QueueTile>();
        t->title = utf8_to_wide(list[i].name);
        // 副标题用文件名（去扩展）
        std::wstring pathW = utf8_to_wide(list[i].path);
        t->subtitle = base_name(pathW);
        t->current = (i == cur);
        int idx = i;
        t->onTap = [idx]() { GlobalPlayer::instance().playAt(idx); };
        queueCol_->addChild(t);
    }
    // 立即重新布局队列列表（draw 中替换 children，需手动 layout）
    if (queueScroll_ && queueScroll_->bounds_.w > 0) {
        queueScroll_->layout(queueScroll_->bounds_);
    }
}

// ============================================================
// measure / layout / draw / 事件
// ============================================================
Size MusicPlayerPage::measure(const Size& max) {
    if (needsRebuild_) { needsRebuild_ = false; rebuildUI(); }
    return root_ ? root_->measure(max) : Size{max.w, max.h};
}

void MusicPlayerPage::layout(const Rect& b) {
    bounds_ = b;
    if (needsRebuild_) { needsRebuild_ = false; rebuildUI(); }
    if (root_) root_->layout(b);
}

void MusicPlayerPage::draw(ID2D1RenderTarget* rt) {
    // 队列变化检测：playlist 大小或当前索引变化时重建队列
    auto& gp = GlobalPlayer::instance();
    auto& list = gp.playlist();
    int cur = gp.musicIndex();
    if ((int)list.size() != lastQueueSize_ || cur != lastQueueCur_) {
        lastQueueSize_ = (int)list.size();
        lastQueueCur_ = cur;
        refreshQueue();
    }

    refreshDynamic();
    if (root_) root_->draw(rt);
}

Widget* MusicPlayerPage::hitTest(float x, float y) {
    return root_ ? root_->hitTest(x, y) : nullptr;
}

bool MusicPlayerPage::onMouseWheel(const MouseEvent& e) {
    return root_ ? root_->onMouseWheel(e) : false;
}
bool MusicPlayerPage::onMouseDown(const MouseEvent& e) {
    return root_ ? root_->onMouseDown(e) : false;
}
bool MusicPlayerPage::onMouseUp(const MouseEvent& e) {
    return root_ ? root_->onMouseUp(e) : false;
}
bool MusicPlayerPage::onMouseMove(const MouseEvent& e) {
    return root_ ? root_->onMouseMove(e) : false;
}

}  // namespace meplayer
