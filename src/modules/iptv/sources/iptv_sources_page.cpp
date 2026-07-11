// IPTV 源管理页实现
#include "modules/iptv/sources/iptv_sources_page.h"
#include "app/theme.h"
#include "app/window_manager.h"
#include "util/string_util.h"

#include <winhttp.h>
#include <commdlg.h>          // GetOpenFileNameW
#include <shobjidl.h>         // IFileOpenDialog（备用）
#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>

namespace meplayer {

// ============================================================
// 本文件内部绘制辅助（与 ui_widgets.cpp 中保持一致，文件静态）
// ============================================================
static void fillRoundedRect(ID2D1RenderTarget* rt, const D2D1_RECT_F& r,
                            float radius, D2D1_COLOR_F c) {
    auto* b = BrushCache::get(rt, c);
    rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
}
static void drawRoundedRectBorder(ID2D1RenderTarget* rt, const D2D1_RECT_F& r,
                                  float radius, D2D1_COLOR_F c, float width) {
    auto* b = BrushCache::get(rt, c);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b, width);
}
static inline bool pointInRect(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

// ============================================================
// WinHTTP 下载 M3U 内容（仅下载文本，不解析）
// ============================================================
static std::string downloadM3u(const std::string& url) {
    if (url.empty()) return {};
    std::wstring wurl = utf8_to_wide(url);

    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {}, extra[1024] = {};
    uc.lpszHostName = host;        uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path;        uc.dwUrlPathLength  = 4096;
    uc.lpszExtraInfo = extra;      uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) return {};

    std::wstring fullpath = uc.lpszUrlPath;
    if (uc.dwExtraInfoLength > 0 && uc.lpszExtraInfo) fullpath += uc.lpszExtraInfo;

    HINTERNET hSession = WinHttpOpen(L"MEPlayer/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
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
        DWORD avail = 0;
        for (;;) {
            avail = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read)) break;
            if (read == 0) break;
            body.append(buf.data(), read);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return body;
}

// 读取本地 M3U 文件内容
static std::string readLocalFile(const std::wstring& path) {
    if (path.empty()) return {};
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 选择 M3U 文件（GetOpenFileNameW）
static std::wstring pickM3uFile(HWND owner) {
    wchar_t szFile[2048] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"M3U 播放列表\0*.m3u;*.m3u8\0所有文件\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = 2048;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrDefExt = L"m3u";
    if (GetOpenFileNameW(&ofn)) return szFile;
    return {};
}

// ============================================================
// 源编辑对话框：名称 + 导入方式(远程/文件) + URL/文件 + 自动更新
// ============================================================
class SourceEditDialog : public Dialog {
public:
    // 完成回调：传入构造好的源（id=0 表示新增）
    using OnDone = std::function<void(const Storage::IptvSource&)>;

    SourceEditDialog(const Storage::IptvSource* existing, OnDone done)
        : done_(std::move(done)) {
        title = L"编辑 IPTV 源";
        if (existing) {
            editing_ = *existing;
            nameField_.text = utf8_to_wide(existing->name);
            type_ = existing->type;            // "remote" | "file"
            urlField_.text = utf8_to_wide(existing->type == "remote"
                                         ? existing->url : existing->filePath);
            autoUpdate_ = existing->autoUpdate;
        } else {
            type_ = "remote";
            autoUpdate_ = false;
        }
        urlField_.placeholder = (type_ == "remote")
            ? L"http(s)://.../xxx.m3u" : L"点击右侧按钮选择文件";
        buildContent();
    }

private:
    OnDone done_;
    Storage::IptvSource editing_;
    std::string type_;
    bool autoUpdate_ = false;

    std::shared_ptr<TextField> nameField_ = std::make_shared<TextField>();
    std::shared_ptr<TextField> urlField_ = std::make_shared<TextField>();
    std::shared_ptr<Text> typeLabelValue_;   // 当前类型显示
    std::shared_ptr<Text> autoLabelValue_;   // 自动更新状态显示
    std::shared_ptr<FilledButton> fileBtn_;  // 文件选择按钮
    std::shared_ptr<Container> contentRoot_;

    void buildContent() {
        auto col = std::make_shared<Column>();
        col->spacing = 12;

        // 名称
        auto nameRow = std::make_shared<Text>(L"名称", 13,
            ThemeManager::instance().current().onSurfaceVariant);
        col->addChild(nameRow);
        nameField_->height = 40;
        col->addChild(nameField_);

        // 导入方式
        auto typeRow = std::make_shared<Row>();
        typeRow->spacing = 8;
        typeRow->mainAlign = MainAxisAlignment::Start;
        auto typeLabel = std::make_shared<Text>(L"导入方式：", 13,
            ThemeManager::instance().current().onSurfaceVariant);
        typeRow->addChild(typeLabel);
        typeLabelValue_ = std::make_shared<Text>(
            type_ == "remote" ? L"远程" : L"文件", 13,
            ThemeManager::instance().current().onSurface);
        typeRow->addChild(typeLabelValue_);
        auto btnRemote = std::make_shared<TextButton>(L"远程", [this]{
            type_ = "remote";
            urlField_.placeholder = L"http(s)://.../xxx.m3u";
            urlField_->text.clear();
            typeLabelValue_->setText(L"远程");
            markDirty();
        });
        auto btnFile = std::make_shared<TextButton>(L"文件", [this]{
            type_ = "file";
            urlField_.placeholder = L"点击右侧按钮选择文件";
            typeLabelValue_->setText(L"文件");
            markDirty();
        });
        typeRow->addChild(btnRemote);
        typeRow->addChild(btnFile);
        col->addChild(typeRow);

        // URL / 文件路径
        auto urlRow = std::make_shared<Row>();
        urlRow->spacing = 8;
        urlRow->flex[0] = 1.0f;   // 输入框弹性
        urlField_->height = 40;
        urlRow->addChild(urlField_);
        fileBtn_ = std::make_shared<FilledButton>(L"选择...", [this]{
            std::wstring p = pickM3uFile(WindowManager::instance().hwnd());
            if (!p.empty()) { urlField_->text = p; markDirty(); }
        });
        urlRow->addChild(fileBtn_);
        col->addChild(urlRow);

        // 自动更新
        auto autoRow = std::make_shared<Row>();
        autoRow->spacing = 8;
        autoRow->mainAlign = MainAxisAlignment::Start;
        auto autoBtn = std::make_shared<TextButton>(
            autoUpdate_ ? L"☑ 自动更新" : L"☐ 自动更新", nullptr);
        autoBtn->onClick = [this, autoBtn]() {
            autoUpdate_ = !autoUpdate_;
            autoBtn->label = autoUpdate_ ? L"☑ 自动更新" : L"☐ 自动更新";
            autoLabelValue_->setText(autoUpdate_ ? L"(启动时自动刷新)" : L"");
            markDirty();
        };
        autoRow->addChild(autoBtn);
        autoLabelValue_ = std::make_shared<Text>(
            autoUpdate_ ? L"(启动时自动刷新)" : L"", 12,
            ThemeManager::instance().current().onSurfaceVariant);
        autoRow->addChild(autoLabelValue_);
        col->addChild(autoRow);

        contentRoot_ = std::make_shared<Container>();
        contentRoot_->padding = all(0);
        contentRoot_->child = col;
        content = contentRoot_;

        // 动作按钮
        actions.clear();
        actions.push_back(std::make_shared<TextButton>(L"取消", [this]{
            Dialog::close();
        }));
        actions.push_back(std::make_shared<FilledButton>(L"保存", [this]{
            Storage::IptvSource s = editing_;
            s.name = wide_to_utf8(nameField_->text);
            if (type_ == "remote") {
                s.type = "remote";
                s.url = wide_to_utf8(urlField_->text);
                s.filePath.clear();
            } else {
                s.type = "file";
                s.filePath = wide_to_utf8(urlField_->text);
                s.url.clear();
            }
            s.autoUpdate = autoUpdate_;
            if (done_) done_(s);
            Dialog::close();
        }));
    }
};

// ============================================================
// IptvSourcesPage
// ============================================================
IptvSourcesPage::IptvSourcesPage(OnPlaySource onPlay) : onPlaySource_(std::move(onPlay)) {
    reloadSources();
}

void IptvSourcesPage::reloadSources() {
    sources_ = Storage::instance().getIptvSources();
}

void IptvSourcesPage::onActive() {
    reloadSources();
    refreshAllAuto();
    scheduleRebuild();
}

void IptvSourcesPage::addSource(const Storage::IptvSource& src) {
    Storage::instance().addIptvSource(src);
    reloadSources();
    scheduleRebuild();
    if (src.autoUpdate && src.type == "remote") {
        refreshSource(src.id);
    }
}

void IptvSourcesPage::updateSource(const Storage::IptvSource& src) {
    Storage::instance().updateIptvSource(src);
    reloadSources();
    scheduleRebuild();
}

void IptvSourcesPage::removeSource(int id) {
    Storage::instance().removeIptvSource(id);
    reloadSources();
    scheduleRebuild();
}

void IptvSourcesPage::refreshSource(int sourceId) {
    // 在后台线程下载，完成后仅刷新内存（播放时才解析）
    for (auto& s : sources_) {
        if (s.id == sourceId && s.type == "remote") {
            std::string url = s.url;
            std::thread([url]() {
                downloadM3u(url);   // 仅下载预热，内容不缓存（播放时再下载）
            }).detach();
            return;
        }
    }
}

void IptvSourcesPage::refreshAllAuto() {
    for (auto& s : sources_) {
        if (s.autoUpdate && s.type == "remote") {
            std::string url = s.url;
            std::thread([url]() { downloadM3u(url); }).detach();
        }
    }
}

WidgetPtr IptvSourcesPage::buildSourceCard(int idx) {
    auto& src = sources_[idx];
    auto& sc = ThemeManager::instance().current();

    auto card = std::make_shared<Card>();
    card->radius = 12;
    card->elevation = 2;

    // 类型 / 自动更新徽章
    auto badgeRow = std::make_shared<Row>();
    badgeRow->spacing = 6;
    badgeRow->mainAlign = MainAxisAlignment::Start;
    auto typeBadge = std::make_shared<Container>();
    typeBadge->padding = sym(6, 2);
    typeBadge->radius = 6;
    typeBadge->color = (src.type == "remote") ? sc.primaryContainer
                                              : withAlpha(sc.onSurface, 0.08f);
    typeBadge->child = std::make_shared<Text>(
        src.type == "remote" ? L"远程" : L"文件", 11,
        src.type == "remote" ? sc.onPrimaryContainer : sc.onSurface);
    badgeRow->addChild(typeBadge);
    if (src.autoUpdate) {
        auto autoBadge = std::make_shared<Container>();
        autoBadge->padding = sym(6, 2);
        autoBadge->radius = 6;
        autoBadge->color = withAlpha(sc.primary, 0.15f);
        autoBadge->child = std::make_shared<Text>(L"自动更新", 11, sc.primary);
        badgeRow->addChild(autoBadge);
    }

    std::string loc = (src.type == "remote") ? src.url : src.filePath;

    // 主列：标题 + 副标题(路径) + 徽章 + 操作行
    auto finalCol = std::make_shared<Column>();
    finalCol->spacing = 4;

    // 标题行（可点击播放）
    auto tap = std::make_shared<ListTile>();
    tap->title = utf8_to_wide(src.name);
    tap->subtitle = utf8_to_wide(loc);
    tap->fontSize = 15;
    tap->leading = std::make_shared<Icon>(
        src.type == "remote" ? IconKind::Iptv : IconKind::Folder, 24, sc.primary);
    tap->onTap = [this, src]() { if (onPlaySource_) onPlaySource_(src); };
    finalCol->addChild(tap);

    finalCol->addChild(std::make_shared<Padding>(sym(16, 0), badgeRow));

    // 操作按钮行
    auto opRow = std::make_shared<Row>();
    opRow->spacing = 0;
    opRow->mainAlign = MainAxisAlignment::End;
    if (src.type == "remote") {
        opRow->addChild(std::make_shared<IconButton>(IconKind::Refresh,
            [this, id = src.id]() { refreshSource(id); }));
    }
    opRow->addChild(std::make_shared<IconButton>(IconKind::Settings,
        [this, src]() { showEditDialog(&src); }));
    opRow->addChild(std::make_shared<IconButton>(IconKind::Close,
        [this, id = src.id]() { removeSource(id); }));
    finalCol->addChild(std::make_shared<Padding>(sym(8, 0), opRow));

    card->child = finalCol;
    return card;
}

void IptvSourcesPage::showEditDialog(const Storage::IptvSource* existing) {
    auto dlg = std::make_shared<SourceEditDialog>(existing,
        [this](const Storage::IptvSource& s) {
            if (s.id == 0) addSource(s);
            else            updateSource(s);
        });
    dlg->show();
}

void IptvSourcesPage::rebuildUI() {
    auto& sc = ThemeManager::instance().current();

    auto root = std::make_shared<Container>();
    root->padding = all(0);
    root->color = sc.background;

    auto col = std::make_shared<Column>();
    col->spacing = 0;

    // 顶栏
    auto topBar = std::make_shared<Container>();
    topBar->padding = sym(20, 14);
    topBar->color = sc.surface;
    auto topRow = std::make_shared<Row>();
    topRow->mainAlign = MainAxisAlignment::SpaceBetween;
    topRow->crossAlign = CrossAxisAlignment::Center;
    topRow->addChild(std::make_shared<Text>(L"IPTV 源管理", 22, sc.onSurface,
                                            DWRITE_FONT_WEIGHT_MEDIUM));
    auto importBtn = std::make_shared<FilledButton>(L"导入", [this]{
        showEditDialog(nullptr);
    });
    topRow->addChild(importBtn);
    topBar->child = topRow;
    col->addChild(topBar);

    // 内容区：GridView（弹性填满）
    if (sources_.empty()) {
        auto emptyCol = std::make_shared<Column>();
        emptyCol->spacing = 12;
        emptyCol->mainAlign = MainAxisAlignment::Center;
        emptyCol->crossAlign = CrossAxisAlignment::Center;
        auto ic = std::make_shared<Icon>(IconKind::Iptv, 56, sc.onSurfaceVariant);
        emptyCol->addChild(ic);
        emptyCol->addChild(std::make_shared<Text>(L"还没有 IPTV 源", 16, sc.onSurfaceVariant));
        emptyCol->addChild(std::make_shared<Text>(L"点击右上角“导入”添加", 13, sc.onSurfaceVariant));
        col->addChild(emptyCol);
        col->flex[1] = 1.0f;   // 内容区弹性填满
    } else {
        auto grid = std::make_shared<GridView>();
        grid->itemCount = static_cast<int>(sources_.size());
        grid->maxCrossAxisExtent = 280;
        grid->childAspectRatio = 1.6f;   // 卡片宽高比
        grid->spacing = 12;
        grid->mainPadding = 16;
        grid->itemBuilder = [this](int i) { return buildSourceCard(i); };
        auto gridWrap = std::make_shared<Padding>(all(16), grid);
        col->addChild(gridWrap);
        col->flex[1] = 1.0f;   // 内容区弹性填满
    }

    root->child = col;
    root_ = root;
}

Size IptvSourcesPage::measure(const Size& max) {
    if (needsRebuild_) { needsRebuild_ = false; rebuildUI(); }
    return root_ ? root_->measure(max) : Size{max.w, max.h};
}

void IptvSourcesPage::layout(const Rect& b) {
    bounds_ = b;
    if (needsRebuild_) { needsRebuild_ = false; rebuildUI(); }
    if (root_) root_->layout(b);
}

void IptvSourcesPage::draw(ID2D1RenderTarget* rt) {
    if (root_) root_->draw(rt);
}

Widget* IptvSourcesPage::hitTest(float x, float y) {
    return root_ ? root_->hitTest(x, y) : nullptr;
}

bool IptvSourcesPage::onMouseWheel(const MouseEvent& e) {
    return root_ ? root_->onMouseWheel(e) : false;
}
bool IptvSourcesPage::onMouseDown(const MouseEvent& e) {
    return root_ ? root_->onMouseDown(e) : false;
}
bool IptvSourcesPage::onMouseUp(const MouseEvent& e) {
    return root_ ? root_->onMouseUp(e) : false;
}
bool IptvSourcesPage::onMouseMove(const MouseEvent& e) {
    return root_ ? root_->onMouseMove(e) : false;
}

}  // namespace meplayer
