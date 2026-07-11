#include "app/tray_service.h"
#include "app/window_manager.h"
#include "util/string_util.h"

namespace meplayer {

#define WM_TRAYICON (WM_USER + 100)
#define IDM_SHOW 2001
#define IDM_EXIT 2002

TrayService& TrayService::instance() {
    static TrayService inst;
    return inst;
}

TrayService::TrayService() {
    taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");
}

bool TrayService::init(HWND hwnd, HINSTANCE hInst) {
    hwnd_ = hwnd;
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = static_cast<HICON>(LoadImageW(
        hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    wcscpy_s(nid_.szTip, L"MEPlayer");
    Shell_NotifyIconW(NIM_ADD, &nid_);
    added_ = true;
    return true;
}

void TrayService::shutdown() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        added_ = false;
    }
}

void TrayService::handleTrayMessage(WPARAM wp, LPARAM lp) {
    if (wp != nid_.uID) return;
    switch (LOWORD(lp)) {
    case WM_LBUTTONUP:
        WindowManager::instance().show();
        break;
    case WM_RBUTTONUP:
        showMenu();
        break;
    }
}

void TrayService::showMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_SHOW, L"\x663e\x793a\x4e3b\x7a97\x53e3");  // 显示主窗口
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"\x9000\x51fa");  // 退出

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (cmd == IDM_SHOW) {
        WindowManager::instance().show();
    } else if (cmd == IDM_EXIT) {
        exitApp();
    }
}

void TrayService::exitApp() {
    shutdown();
    auto& wm = WindowManager::instance();
    wm.setPreventClose(false);
    if (wm.hwnd()) DestroyWindow(wm.hwnd());
}

}  // namespace meplayer
