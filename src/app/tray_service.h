// 系统托盘服务
#pragma once
#include <windows.h>
#include <shellapi.h>
#include <functional>

namespace meplayer {

class TrayService {
public:
    static TrayService& instance();

    bool init(HWND hwnd, HINSTANCE hInst);
    void shutdown();

    // 单击托盘 → 显示窗口；右键 → 菜单
    void handleTrayMessage(WPARAM wp, LPARAM lp);

    // 退出应用（销毁托盘 + 真正关闭窗口）
    void exitApp();

private:
    TrayService();
    void showMenu();

    HWND        hwnd_ = nullptr;
    UINT        taskbar_created_msg_ = 0;
    NOTIFYICONDATAW nid_ = {};
    bool        added_ = false;
};

}  // namespace meplayer
