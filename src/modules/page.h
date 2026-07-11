// 页面基类：所有模块页面的抽象基类
#pragma once
#include "ui/ui_base.h"
#include "ui/ui_widgets.h"
#include <memory>

namespace meplayer {

class Page : public Widget {
public:
    virtual ~Page() = default;
    // 页面被切换为当前页时调用（可重载做刷新）
    virtual void onActive() {}
    // 页面被切走时调用
    virtual void onInactive() {}
    // 返回页面标题（用于设置页等）
    virtual std::wstring title() const { return L""; }
    // 请求重绘
    void requestRedraw() { markDirty(); }
};

using PagePtr = std::shared_ptr<Page>;

}  // namespace meplayer
