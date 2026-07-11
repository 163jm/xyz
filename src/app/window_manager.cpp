#include "app/window_manager.h"
#include "util/string_util.h"
#include <windowsx.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace meplayer {

WindowManager& WindowManager::instance() {
    static WindowManager inst;
    return inst;
}

WindowManager::WindowManager() {}

bool WindowManager::init(HINSTANCE hInst) {
    hinst_ = hInst;

    // 注册窗口类
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"MEPlayerMainWnd";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));  // IDI_APP_ICON
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    // DPI
    dpi_ = static_cast<float>(GetDpiForSystem());

    // 创建窗口
    int sw = GetSystemMetricsForDpi(SM_CXSCREEN, static_cast<UINT>(dpi_));
    int sh = GetSystemMetricsForDpi(SM_CYSCREEN, static_cast<UINT>(dpi_));
    int w = static_cast<int>(1280 * dpi_ / 96.0f);
    int h = static_cast<int>(800 * dpi_ / 96.0f);
    int x = (sw - w) / 2, y = (sh - h) / 2;

    hwnd_ = CreateWindowExW(
        0, wc.lpszClassName, L"MEPlayer",
        WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd_) return false;

    width_ = w; height_ = h;

    if (!createD3D()) return false;
    if (!createD2D()) return false;

    // 最小尺寸 900x600
    RECT rcMin = {0, 0, static_cast<LONG>(900 * dpi_ / 96), static_cast<LONG>(600 * dpi_ / 96)};
    AdjustWindowRectExForDpi(&rcMin, WS_OVERLAPPEDWINDOW, FALSE, 0, static_cast<UINT>(dpi_));
    return true;
}

bool WindowManager::createD3D() {
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, 2, D3D11_SDK_VERSION,
        &d3d_device_, nullptr, &d3d_context_);
    if (FAILED(hr)) {
        // 回退无 debug
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &d3d_device_, nullptr, &d3d_context_);
        if (FAILED(hr)) return false;
    }
    return true;
}

bool WindowManager::createD2D() {
    D2D1_FACTORY_OPTIONS opts = {};
#ifdef _DEBUG
    opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
            opts, d2d_factory_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dw_factory_.GetAddressOf())))) return false;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    // 渲染目标固定使用 96 DPI（即 1 DIP = 1 物理像素），不让 D2D 做二次缩放。
    // 原因：本项目的整个布局系统（Rect/Row/Column/NavigationRail 等）、窗口尺寸计算
    // （width_/height_，均来自 WM_SIZE 的物理像素）、以及鼠标事件坐标
    // （GET_X_LPARAM/GET_Y_LPARAM，也是物理像素）全部按"1 单位=1 物理像素"处理。
    // 若把渲染目标 DPI 设为系统真实 DPI（如 150% 缩放下的 144），
    // D2D 会在绘制时把所有坐标再按 dpi_/96 放大一次，
    // 造成"绘制出来的视觉位置"和"布局/命中测试用的坐标"不一致：
    // 界面看起来错位，点击判定也对不上按钮实际显示的位置。
    // 我们已经在创建窗口、处理 WM_DPICHANGED 时手动按 dpi_ 换算过物理像素尺寸，
    // 布局系统本身并不感知 DPI，因此渲染目标应保持 96，交给我们自己统一按物理像素处理。
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_HARDWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
        D2D1::HwndRenderTargetProperties(hwnd_, size, D2D1_PRESENT_OPTIONS_NONE);
    return SUCCEEDED(d2d_factory_->CreateHwndRenderTarget(props, hwndProps, &rt_));
}

void WindowManager::discardDeviceResources() {
    rt_.Reset();
}

void WindowManager::runMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void WindowManager::setFullScreen(bool full) {
    if (fullscreen_ == full) return;
    fullscreen_ = full;
    if (full) {
        GetWindowPlacement(hwnd_, &saved_placement_);
        LONG style = GetWindowLongW(hwnd_, GWL_STYLE);
        SetWindowLongW(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowPos(hwnd_, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(hwnd_, GWL_STYLE,
            GetWindowLongW(hwnd_, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd_, &saved_placement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

void WindowManager::show()   { ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); }
void WindowManager::hide()   { ShowWindow(hwnd_, SW_HIDE); }
void WindowManager::focus()  { ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); }
void WindowManager::quit()   { PostQuitMessage(0); }

LRESULT WindowManager::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto& wm = instance();
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (wm.rt_) {
            wm.rt_->BeginDraw();
            wm.rt_->Clear(D2D1::ColorF(D2D1::ColorF::White));
            if (wm.render_cb_) wm.render_cb_();
            wm.rt_->EndDraw();
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: {
        if (wm.rt_) {
            D2D1_SIZE_U size = D2D1::SizeU(LOWORD(lp), HIWORD(lp));
            if (size.width > 0 && size.height > 0) {
                wm.rt_->Resize(size);
                wm.width_ = size.width;
                wm.height_ = size.height;
            }
        }
        return 0;
    }
    case WM_DISPLAYCHANGE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = static_cast<LONG>(900 * wm.dpi_ / 96);
        mmi->ptMinTrackSize.y = static_cast<LONG>(600 * wm.dpi_ / 96);
        return 0;
    }
    case WM_CLOSE:
        if (wm.prevent_close_) {
            wm.hide();  // 最小化到托盘
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_DPICHANGED: {
        wm.dpi_ = static_cast<float>(HIWORD(wp));
        RECT* rc = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, rc->left, rc->top,
            rc->right - rc->left, rc->bottom - rc->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        SetCapture(hwnd);
        MouseEvent e{};
        e.x = static_cast<float>(GET_X_LPARAM(lp));
        e.y = static_cast<float>(GET_Y_LPARAM(lp));
        e.button = (msg == WM_LBUTTONDOWN) ? MouseBtn::Left
                 : (msg == WM_RBUTTONDOWN) ? MouseBtn::Right : MouseBtn::Middle;
        e.wheel = 0;
        e.ctrl = (wp & MK_CONTROL) != 0;
        e.shift = (wp & MK_SHIFT) != 0;
        if (wm.mouse_down_cb_) wm.mouse_down_cb_(e);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        ReleaseCapture();
        MouseEvent e{};
        e.x = static_cast<float>(GET_X_LPARAM(lp));
        e.y = static_cast<float>(GET_Y_LPARAM(lp));
        e.button = (msg == WM_LBUTTONUP) ? MouseBtn::Left
                 : (msg == WM_RBUTTONUP) ? MouseBtn::Right : MouseBtn::Middle;
        e.wheel = 0;
        e.ctrl = (wp & MK_CONTROL) != 0;
        e.shift = (wp & MK_SHIFT) != 0;
        if (wm.mouse_up_cb_) wm.mouse_up_cb_(e);
        return 0;
    }
    case WM_MOUSEMOVE: {
        MouseEvent e{};
        e.x = static_cast<float>(GET_X_LPARAM(lp));
        e.y = static_cast<float>(GET_Y_LPARAM(lp));
        e.button = MouseBtn::None;
        e.wheel = 0;
        e.ctrl = (wp & MK_CONTROL) != 0;
        e.shift = (wp & MK_SHIFT) != 0;
        if (wm.mouse_move_cb_) wm.mouse_move_cb_(e);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        MouseEvent e{};
        e.x = static_cast<float>(pt.x);
        e.y = static_cast<float>(pt.y);
        e.button = MouseBtn::None;
        e.wheel = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        e.ctrl = (LOWORD(wp) & MK_CONTROL) != 0;
        e.shift = (LOWORD(wp) & MK_SHIFT) != 0;
        if (wm.mouse_wheel_cb_) wm.mouse_wheel_cb_(e);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace meplayer
