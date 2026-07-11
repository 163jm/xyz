// 设置页：外观/播放/关于
// 对应原项目 settings_page.dart
#pragma once
#include "modules/page.h"
#include <memory>

namespace meplayer {

class SettingsPage : public Page {
public:
    SettingsPage();
    ~SettingsPage() override;

    std::wstring title() const override { return L"设置"; }
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
    WidgetPtr root_;
    bool needsRebuild_ = true;
    void rebuild();
};

}  // namespace meplayer
