// 主页：NavigationRail + IndexedStack + MiniPlayerBar
// 对应原项目 home_page.dart
#pragma once
#include "modules/page.h"
#include <memory>
#include <functional>
#include <string>

namespace meplayer {

class HomePage : public Page {
public:
    HomePage();
    ~HomePage() override;

    // 跳转播放页回调（App 层注入）
    std::function<void(const std::string& url, const std::string& title, bool isLocal)> onPlayVideo;
    std::function<void()> onPlayMusic;

    Size measure(const Size& max) override;
    void layout(const Rect& b) override;
    void draw(ID2D1RenderTarget* rt) override;
    Widget* hitTest(float x, float y) override;
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseWheel(const MouseEvent& e) override;

private:
    void buildPages();
    void rebuild();

    PagePtr pages_[4];
    int current_ = 0;

    WidgetPtr root_;
    std::shared_ptr<NavigationRail> rail_;
    std::shared_ptr<MiniPlayerBar> miniBar_;
};

}  // namespace meplayer
