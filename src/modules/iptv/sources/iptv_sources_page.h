// IPTV 源管理页：GridView 展示源卡片 + 编辑对话框 + WinHTTP 刷新
// 对应原项目 iptv_tab_page.dart
#pragma once
#include "modules/page.h"
#include "core/storage.h"
#include <functional>
#include <memory>
#include <vector>

namespace meplayer {

class IptvSourcesPage : public Page {
public:
    // 点击某个源时回调（HomePage 注入，跳转 IPTV 播放页）
    using OnPlaySource = std::function<void(const Storage::IptvSource&)>;

    explicit IptvSourcesPage(OnPlaySource onPlay = nullptr);

    std::wstring title() const override { return L"IPTV 源管理"; }
    void onActive() override;

    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseWheel(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;

    // ---- 控制器接口（供编辑对话框调用）----
    void addSource(const Storage::IptvSource& src);
    void updateSource(const Storage::IptvSource& src);
    void removeSource(int id);
    void refreshSource(int sourceId);

private:
    OnPlaySource onPlaySource_;
    WidgetPtr root_;
    bool needsRebuild_ = true;

    std::vector<Storage::IptvSource> sources_;

    void reloadSources();
    void rebuildUI();
    void scheduleRebuild() { needsRebuild_ = true; markDirty(); }
    void showEditDialog(const Storage::IptvSource* existing);
    WidgetPtr buildSourceCard(int idx);
    void refreshAllAuto();
};

}  // namespace meplayer
