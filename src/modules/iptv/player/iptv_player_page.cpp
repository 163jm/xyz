// IPTV 播放页实现
#include "modules/iptv/player/iptv_player_page.h"
#include "app/theme.h"
#include "app/window_manager.h"
#include "app/app_settings.h"
#include "app/global_player.h"
#include "core/mpv_options.h"
#include "util/string_util.h"

#include <winhttp.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>
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

// WinHTTP 下载 M3U
static std::string downloadM3u(const std::string& url) {
    if (url.empty()) return {};
    std::wstring wurl = utf8_to_wide(url);
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {}, extra[1024] = {};
    uc.lpszHostName = host;   uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path;   uc.dwUrlPathLength  = 4096;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) return {};
    std::wstring fullpath = uc.lpszUrlPath;
    if (uc.dwExtraInfoLength > 0 && uc.lpszExtraInfo) fullpath += uc.lpszExtraInfo;

    HINTERNET hSession = WinHttpOpen(L"MEPlayer/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return {};
    INTERNET_PORT port = uc.nPort ? uc.nPort : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return {}; }
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", fullpath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return {};
    }
    std::string body;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read) || read == 0) break;
            body.append(buf.data(), read);
        }
    }
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return body;
}

static std::string readLocalFile(const std::wstring& path) {
    if (path.empty()) return {};
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

// ============================================================
// VideoArea：视频 + 悬停显隐的顶/底栏 + 缓冲圈
// ============================================================
class VideoArea : public Widget {
public:
    std::shared_ptr<VideoView> video;
    std::shared_ptr<Container> topBar;
    std::shared_ptr<Container> bottomBar;
    bool overlayVisible = true;
    DWORD lastMoveTick = 0;

    void layout(const Rect& b) override {
        bounds_ = b;
        if (video) video->layout(b);
        if (topBar)    topBar->layout({b.x, b.y, b.w, 52});
        if (bottomBar) bottomBar->layout({b.x, b.bottom() - 64, b.w, 64});
    }
    void draw(ID2D1RenderTarget* rt) override {
        if (video) video->draw(rt);
        // 自动隐藏
        if (overlayVisible && GetTickCount() - lastMoveTick > 3000) overlayVisible = false;
        if (overlayVisible) {
            if (topBar) topBar->draw(rt);
            if (bottomBar) bottomBar->draw(rt);
        }
        // 缓冲圈
        if (video && video->backend && video->backend->isBuffering()) {
            auto& sc = ThemeManager::instance().current();
            float cx = bounds_.x + bounds_.w / 2;
            float cy = bounds_.y + bounds_.h / 2;
            float r = 22;
            auto* b = BrushCache::get(rt, D2D1::ColorF(1, 1, 1, 0.9f));
            D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);
            rt->DrawEllipse(e, b, 3);
            // 简易旋转指示：实心点
            DWORD t = GetTickCount() / 100;
            float ang = (t % 62) / 62.0f * 6.2831853f;
            float dx = cx + cosf(ang) * r;
            float dy = cy + sinf(ang) * r;
            D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(dx, dy), 4, 4);
            auto* wb = BrushCache::get(rt, sc.primary);
            rt->FillEllipse(dot, wb);
        }
    }
    Widget* hitTest(float x, float y) override {
        if (!pointInRect(x, y, bounds_.d2d())) return nullptr;
        if (overlayVisible) {
            if (topBar)    { if (auto* h = topBar->hitTest(x, y)) return h; }
            if (bottomBar) { if (auto* h = bottomBar->hitTest(x, y)) return h; }
        }
        return this;   // 视频区本体
    }
    bool onMouseMove(const MouseEvent&) override {
        overlayVisible = true;
        lastMoveTick = GetTickCount();
        markDirty();
        return true;
    }
    bool onMouseDown(const MouseEvent&) override {
        // 单击视频区切换显隐
        overlayVisible = !overlayVisible;
        lastMoveTick = GetTickCount();
        markDirty();
        return true;
    }
};

// ============================================================
// IptvPlayerPage
// ============================================================
IptvPlayerPage::IptvPlayerPage(std::string url, std::string channelName,
                               std::string groupName, int sourceIndex,
                               OnBack onBack)
    : initialUrl_(std::move(url)), channelName_(std::move(channelName)),
      groupName_(std::move(groupName)), initialSourceIdx_(sourceIndex),
      onBack_(std::move(onBack)) {
    sources_ = Storage::instance().getIptvSources();
}

IptvPlayerPage::~IptvPlayerPage() {
    killRedrawTimer();
    if (WindowManager::instance().isFullScreen()) {
        WindowManager::instance().setFullScreen(false);
    }
    if (backend_) backend_->dispose();
}

void IptvPlayerPage::onActive() {
    // 视频/IPTV 开始播放时暂停音乐
    GlobalPlayer::instance().stopMusicForOtherPlayback();

    if (!video_) video_ = std::make_shared<VideoView>();
    backend_ = std::make_unique<MpvBackend>();
    // 属性变化时刷新（缓冲/暂停态）
    backend_->setPropertyChangedCb([this](const std::string&) {
        markDirty();
    });
    needsRebuild_ = true;
    ensureRedrawTimer();
    markDirty();
}

void IptvPlayerPage::onInactive() {
    killRedrawTimer();
    if (fullscreen_) setFullScreen(false);
    reportStateToGlobal();
    if (backend_) { backend_->dispose(); backend_.reset(); }
}

static VOID CALLBACK redrawTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    WindowManager::instance().requestRedraw();
}

void IptvPlayerPage::ensureRedrawTimer() {
    if (!redrawTimer_) redrawTimer_ = SetTimer(nullptr, 0, 33, &redrawTimerProc);
}
void IptvPlayerPage::killRedrawTimer() {
    if (redrawTimer_) { KillTimer(nullptr, redrawTimer_); redrawTimer_ = 0; }
}

void IptvPlayerPage::applyMpvOptions() {
    if (!backend_) return;
    auto opt = MpvOptions::profile(static_cast<int>(AppSettings::instance().mpvProfile()));
    // 强制去交错 + TS 容错（IPTV 直播流）
    auto de = MpvOptions::deinterlace();
    auto ts = MpvOptions::tsResilience();
    for (auto& kv : de.options) opt.options[kv.first] = kv.second;
    for (auto& kv : ts.options)  opt.options[kv.first] = kv.second;
    for (auto& c : de.vfCommands) opt.vfCommands.push_back(c);
    // 硬解
    opt.options["hwdec"] = MpvOptions::hwdecValue(AppSettings::instance().hardwareDecode());
    backend_->applyOptions(opt.options);
    backend_->applyVfCommands(opt.vfCommands);
}

void IptvPlayerPage::loadSource(int idx) {
    if (idx < 0 || idx >= static_cast<int>(sources_.size())) return;
    sourceIndex_ = idx;
    auto& src = sources_[idx];
    loading_ = true;
    markDirty();

    std::string content;
    if (src.type == "file") {
        content = readLocalFile(utf8_to_wide(src.filePath));
    } else {
        content = downloadM3u(src.url);
    }
    loading_ = false;

    if (content.empty()) {
        allChannels_.clear(); grouped_.clear(); groupNames_.clear();
        scheduleRebuild();
        return;
    }
    allChannels_ = M3uParser::parse(content);
    grouped_ = M3uParser::groupBy(allChannels_);
    groupNames_ = M3uParser::groupNames(allChannels_);

    // 浏览分组：优先目标分组，否则首个
    if (!groupName_.empty() && grouped_.count(groupName_)) {
        browseGroup_ = groupName_;
    } else if (!groupNames_.empty()) {
        browseGroup_ = groupNames_[0];
    } else {
        browseGroup_.clear();
    }

    // 自动匹配目标频道
    std::string targetGroup = (!groupName_.empty() && grouped_.count(groupName_))
        ? groupName_ : browseGroup_;
    selectChannelInternal(targetGroup, channelName_, true);
    scheduleRebuild();
}

void IptvPlayerPage::selectChannelInternal(const std::string& group,
                                           const std::string& channel,
                                           bool autoPlay) {
    auto it = grouped_.find(group);
    if (it == grouped_.end() || it->second.empty()) return;
    auto& chs = it->second;

    // 收集同名频道所有 URL 作为多源列表
    streamUrls_.clear();
    std::string resolved = channel;
    if (!channel.empty()) {
        for (auto& c : chs) {
            if (c.name == channel) streamUrls_.push_back(c.url);
        }
    }
    if (streamUrls_.empty()) {
        // 未指定或未命中：取首个频道
        resolved = chs[0].name;
        for (auto& c : chs) {
            if (c.name == resolved) streamUrls_.push_back(c.url);
        }
    }
    if (streamUrls_.empty()) return;

    streamIndex_ = 0;
    playingChannel_ = resolved;
    playingGroup_ = group;
    browseGroup_ = group;

    if (autoPlay) playStream(streamUrls_[streamIndex_]);
    reportStateToGlobal();
}

void IptvPlayerPage::playStream(const std::string& url) {
    if (!backend_ || url.empty()) return;
    backend_->open(url);
    applyMpvOptions();   // open 会重置滤镜链，重新应用
    markDirty();
}

void IptvPlayerPage::reportStateToGlobal() {
    IptvSnapshot snap;
    snap.url = streamUrls_.empty() ? "" : streamUrls_[streamIndex_];
    snap.channelName = playingChannel_;
    snap.groupName = playingGroup_;
    snap.sourceIndex = sourceIndex_;
    GlobalPlayer::instance().saveIptvSnapshot(snap);
}

void IptvPlayerPage::setFullScreen(bool full) {
    if (fullscreen_ == full) return;
    fullscreen_ = full;
    WindowManager::instance().setFullScreen(full);
    scheduleRebuild();
}

// ============================================================
// 面板构建
// ============================================================
WidgetPtr IptvPlayerPage::buildGroupColumn() {
    auto col = std::make_shared<Column>();
    col->spacing = 0;
    auto& sc = ThemeManager::instance().current();
    // 标题
    auto head = std::make_shared<Container>();
    head->padding = sym(8, 8);
    head->color = sc.surfaceVariant;
    head->child = std::make_shared<Text>(L"分组", 12, sc.onSurfaceVariant,
                                         DWRITE_FONT_WEIGHT_MEDIUM);
    col->addChild(head);
    for (auto& g : groupNames_) {
        bool active = (g == browseGroup_);
        auto tile = std::make_shared<ListTile>();
        tile->title = utf8_to_wide(g);
        tile->fontSize = 13;
        tile->onTap = [this, g]() { browseGroup_ = g; scheduleRebuild(); };
        auto wrap = std::make_shared<Container>();
        wrap->padding = all(0);
        wrap->color = active ? sc.primaryContainer : D2D1::ColorF(0, 0, 0, 0);
        wrap->child = tile;
        col->addChild(wrap);
    }
    return col;
}

WidgetPtr IptvPlayerPage::buildChannelColumn() {
    auto col = std::make_shared<Column>();
    col->spacing = 0;
    auto& sc = ThemeManager::instance().current();
    auto head = std::make_shared<Container>();
    head->padding = sym(8, 8);
    head->color = sc.surfaceVariant;
    head->child = std::make_shared<Text>(utf8_to_wide(browseGroup_), 12,
        sc.onSurfaceVariant, DWRITE_FONT_WEIGHT_MEDIUM);
    col->addChild(head);

    auto it = grouped_.find(browseGroup_);
    if (it != grouped_.end()) {
        // 去重频道名（同名合并为多源）
        std::vector<std::string> seen;
        for (auto& c : it->second) {
            if (std::find(seen.begin(), seen.end(), c.name) != seen.end()) continue;
            seen.push_back(c.name);
            bool active = (c.name == playingChannel_);
            auto tile = std::make_shared<ListTile>();
            tile->title = utf8_to_wide(c.name);
            tile->fontSize = 13;
            tile->leading = std::make_shared<Icon>(IconKind::Iptv, 18,
                active ? sc.primary : sc.onSurfaceVariant);
            tile->onTap = [this, g = browseGroup_, name = c.name]() {
                selectChannelInternal(g, name, true);
                scheduleRebuild();
            };
            auto wrap = std::make_shared<Container>();
            wrap->padding = all(0);
            wrap->color = active ? withAlpha(sc.primary, 0.12f) : D2D1::ColorF(0, 0, 0, 0);
            wrap->child = tile;
            col->addChild(wrap);
        }
    }
    return col;
}

WidgetPtr IptvPlayerPage::buildSourceColumn() {
    auto col = std::make_shared<Column>();
    col->spacing = 0;
    auto& sc = ThemeManager::instance().current();
    auto head = std::make_shared<Container>();
    head->padding = sym(8, 8);
    head->color = sc.surfaceVariant;
    head->child = std::make_shared<Text>(L"源", 12, sc.onSurfaceVariant,
                                         DWRITE_FONT_WEIGHT_MEDIUM);
    col->addChild(head);
    for (size_t i = 0; i < sources_.size(); i++) {
        bool active = (static_cast<int>(i) == sourceIndex_);
        auto tile = std::make_shared<ListTile>();
        tile->title = utf8_to_wide(sources_[i].name);
        tile->fontSize = 12;
        tile->onTap = [this, idx = static_cast<int>(i)]() {
            // 切源后清空续播目标，自动选首频道
            channelName_.clear(); groupName_.clear();
            loadSource(idx);
        };
        auto wrap = std::make_shared<Container>();
        wrap->padding = all(0);
        wrap->color = active ? sc.primaryContainer : D2D1::ColorF(0, 0, 0, 0);
        wrap->child = tile;
        col->addChild(wrap);
    }
    return col;
}

WidgetPtr IptvPlayerPage::buildChannelPanel() {
    auto& sc = ThemeManager::instance().current();
    auto panel = std::make_shared<Container>();
    panel->color = sc.surface;
    panel->padding = all(0);

    auto row = std::make_shared<Row>();
    row->spacing = 0;
    row->crossAlign = CrossAxisAlignment::Stretch;

    // 第1列：分组 80px
    if (!groupScroll_) groupScroll_ = std::make_shared<Scrollable>();
    groupScroll_->child = buildGroupColumn();
    auto gWrap = std::make_shared<SizedBox>(80, -1, groupScroll_);
    row->addChild(gWrap);   // index 0

    // 第2列：频道 弹性
    if (!channelScroll_) channelScroll_ = std::make_shared<Scrollable>();
    channelScroll_->child = buildChannelColumn();
    row->addChild(channelScroll_);   // index 1
    row->flex[1] = 1.0f;

    // 第3列：源 72px
    if (!sourceScroll_) sourceScroll_ = std::make_shared<Scrollable>();
    sourceScroll_->child = buildSourceColumn();
    auto sWrap = std::make_shared<SizedBox>(72, -1, sourceScroll_);
    row->addChild(sWrap);   // index 2

    panel->child = row;
    return panel;
}

WidgetPtr IptvPlayerPage::buildVideoArea() {
    if (!video_) video_ = std::make_shared<VideoView>();
    auto area = std::make_shared<VideoArea>();
    area->video = video_;

    // 顶栏：返回 + 频道名 + 分组
    auto topBar = std::make_shared<Container>();
    topBar->padding = sym(8, 6);
    topBar->color = D2D1::ColorF(0, 0, 0, 0.45f);
    auto topRow = std::make_shared<Row>();
    topRow->spacing = 8;
    topRow->crossAlign = CrossAxisAlignment::Center;
    auto backBtn = std::make_shared<IconButton>(IconKind::ChevronLeft, [this]{
        if (onBack_) onBack_();
    });
    backBtn->iconColor = D2D1::ColorF(1, 1, 1, 1);
    topRow->addChild(backBtn);
    std::wstring title = utf8_to_wide(playingChannel_);
    if (!playingGroup_.empty()) title += L"  ·  " + utf8_to_wide(playingGroup_);
    auto titleText = std::make_shared<Text>(title, 14, D2D1::ColorF(1, 1, 1, 1),
                                            DWRITE_FONT_WEIGHT_MEDIUM);
    topRow->addChild(titleText);
    topBar->child = topRow;
    area->topBar = topBar;

    // 底栏：播放/暂停 + 音量 + 全屏
    auto bottomBar = std::make_shared<Container>();
    bottomBar->padding = sym(12, 8);
    bottomBar->color = D2D1::ColorF(0, 0, 0, 0.5f);
    auto botRow = std::make_shared<Row>();
    botRow->spacing = 8;
    botRow->crossAlign = CrossAxisAlignment::Center;
    bool playing = backend_ && backend_->isPlaying();
    auto playBtn = std::make_shared<IconButton>(playing ? IconKind::Pause : IconKind::Play,
        [this]{ if (backend_) backend_->playPause(); markDirty(); });
    playBtn->iconColor = D2D1::ColorF(1, 1, 1, 1);
    botRow->addChild(playBtn);

    auto volIcon = std::make_shared<Icon>(IconKind::Volume, 20, D2D1::ColorF(1, 1, 1, 1));
    botRow->addChild(volIcon);
    auto volSlider = std::make_shared<Slider>();
    volSlider->value = backend_ ? backend_->volume() / 100.0f : 1.0f;
    volSlider->onChanged = [this](float v) {
        if (backend_) backend_->setVolume(static_cast<int>(v * 100));
        markDirty();
    };
    auto volWrap = std::make_shared<SizedBox>(120, 36, volSlider);
    botRow->addChild(volWrap);

    // 源切换提示（同频道多源）
    if (streamUrls_.size() > 1) {
        std::wstring si = L"源 " + std::to_wstring(streamIndex_ + 1) +
                          L"/" + std::to_wstring(streamUrls_.size());
        auto siText = std::make_shared<Text>(si, 12, D2D1::ColorF(1, 1, 1, 0.8f));
        botRow->addChild(siText);
        auto nextSrcBtn = std::make_shared<IconButton>(IconKind::Refresh, [this]{
            if (streamUrls_.empty()) return;
            streamIndex_ = (streamIndex_ + 1) % static_cast<int>(streamUrls_.size());
            playStream(streamUrls_[streamIndex_]);
            reportStateToGlobal();
            scheduleRebuild();
        });
        nextSrcBtn->iconColor = D2D1::ColorF(1, 1, 1, 1);
        botRow->addChild(nextSrcBtn);
    }

    // 占位弹性
    botRow->addChild(std::make_shared<SizedBox>());
    botRow->flex[botRow->children.size() - 1] = 1.0f;

    auto fsBtn = std::make_shared<IconButton>(IconKind::Fullscreen, [this]{
        setFullScreen(!fullscreen_);
    });
    fsBtn->iconColor = D2D1::ColorF(1, 1, 1, 1);
    botRow->addChild(fsBtn);
    bottomBar->child = botRow;
    area->bottomBar = bottomBar;

    area->overlayVisible = true;
    area->lastMoveTick = GetTickCount();
    return area;
}

void IptvPlayerPage::rebuildUI() {
    auto& sc = ThemeManager::instance().current();
    auto root = std::make_shared<Container>();
    root->padding = all(0);
    root->color = sc.background;

    if (fullscreen_) {
        // 全屏：仅视频区
        root->child = buildVideoArea();
    } else {
        auto row = std::make_shared<Row>();
        row->spacing = 0;
        row->crossAlign = CrossAxisAlignment::Stretch;
        // 左侧视频区 7 份
        row->addChild(buildVideoArea());
        row->flex[0] = 7.0f;
        // 右侧频道面板 360px
        row->addChild(std::make_shared<SizedBox>(360, -1, buildChannelPanel()));
        row->flex[1] = 0.0f;
        root->child = row;
    }
    root_ = root;
}

Size IptvPlayerPage::measure(const Size& max) {
    if (needsRebuild_) { needsRebuild_ = false; rebuildUI(); }
    return root_ ? root_->measure(max) : Size{max.w, max.h};
}

void IptvPlayerPage::layout(const Rect& b) {
    bounds_ = b;
    if (needsRebuild_) { needsRebuild_ = false; rebuildUI(); }
    if (root_) root_->layout(b);

    // 首次布局后 video_ RTV 就绪 → 初始化 backend
    if (backend_ && video_ && video_->rtv() && !backend_->handle()) {
        if (backend_->init(WindowManager::instance().d3dDevice(), video_->rtv())) {
            applyMpvOptions();
            // 确定初始源索引
            int idx = initialSourceIdx_;
            if (!initialUrl_.empty()) {
                for (int i = 0; i < static_cast<int>(sources_.size()); i++) {
                    auto& s = sources_[i];
                    if ((s.type == "remote" && s.url == initialUrl_) ||
                        (s.type == "file" && s.filePath == initialUrl_)) {
                        idx = i; break;
                    }
                }
            }
            if (idx < 0 || idx >= static_cast<int>(sources_.size())) idx = 0;
            if (sources_.empty()) {
                // 无源：直接把 url 当作单一流播放
                if (!initialUrl_.empty()) playStream(initialUrl_);
            } else {
                loadSource(idx);
            }
        }
    }
}

void IptvPlayerPage::draw(ID2D1RenderTarget* rt) {
    if (root_) root_->draw(rt);
    // 加载中提示
    if (loading_) {
        auto& sc = ThemeManager::instance().current();
        Text t(L"加载中...", 16, sc.onSurfaceVariant);
        Size s = t.measure({bounds_.w, 40});
        t.layout({bounds_.x + (bounds_.w - s.w) / 2,
                  bounds_.y + 24, s.w, s.h});
        t.draw(rt);
    }
}

Widget* IptvPlayerPage::hitTest(float x, float y) {
    return root_ ? root_->hitTest(x, y) : nullptr;
}

bool IptvPlayerPage::onMouseWheel(const MouseEvent& e) {
    return root_ ? root_->onMouseWheel(e) : false;
}
bool IptvPlayerPage::onMouseDown(const MouseEvent& e) {
    return root_ ? root_->onMouseDown(e) : false;
}
bool IptvPlayerPage::onMouseUp(const MouseEvent& e) {
    return root_ ? root_->onMouseUp(e) : false;
}
bool IptvPlayerPage::onMouseMove(const MouseEvent& e) {
    return root_ ? root_->onMouseMove(e) : false;
}

bool IptvPlayerPage::onKey(const KeyEvent& e) {
    if (e.key == VK_ESCAPE && fullscreen_) { setFullScreen(false); return true; }
    if (e.key == VK_SPACE) { if (backend_) backend_->playPause(); markDirty(); return true; }
    return false;
}

}  // namespace meplayer
