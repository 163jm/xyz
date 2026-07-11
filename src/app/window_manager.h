// 主窗口管理 + D2D/D3D11 渲染设备
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <functional>
#include <string>
#include <memory>

namespace meplayer {

using Microsoft::WRL::ComPtr;

class UiWindow;  // 前置

class WindowManager {
public:
    static WindowManager& instance();

    bool init(HINSTANCE hInst);
    void runMessageLoop();   // 主消息循环

    HWND hwnd() const { return hwnd_; }
    HINSTANCE hinst() const { return hinst_; }

    // D2D 资源访问（UI 渲染用）
    ID2D1Factory*           d2dFactory() const { return d2d_factory_.Get(); }
    IDWriteFactory*         dwFactory()  const { return dw_factory_.Get(); }
    ID2D1HwndRenderTarget*  rt()         const { return rt_.Get(); }
    ID3D11Device*           d3dDevice()  const { return d3d_device_.Get(); }
    ID3D11DeviceContext*    d3dContext() const { return d3d_context_.Get(); }

    // 全屏切换
    void setFullScreen(bool full);
    bool isFullScreen() const { return fullscreen_; }

    // 关闭拦截：最小化到托盘而非退出
    void setPreventClose(bool v) { prevent_close_ = v; }
    bool preventClose() const { return prevent_close_; }

    // 显示/隐藏/聚焦
    void show();
    void hide();
    void focus();

    // 退出应用
    void quit();

    // 渲染回调（由 UiWindow 设置）
    using RenderCb = std::function<void()>;
    void setRenderCallback(RenderCb cb) { render_cb_ = cb; }
    void requestRedraw() { InvalidateRect(hwnd_, nullptr, FALSE); }

    // 窗口尺寸
    int width() const { return width_; }
    int height() const { return height_; }

    // DPI
    float dpi() const { return dpi_; }

private:
    WindowManager();
    bool createDeviceResources();
    void discardDeviceResources();
    bool createD3D();
    bool createD2D();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    HWND        hwnd_ = nullptr;
    HINSTANCE   hinst_ = nullptr;
    int         width_ = 1280, height_ = 800;
    float       dpi_ = 96.0f;
    bool        fullscreen_ = false;
    bool        prevent_close_ = true;
    WINDOWPLACEMENT saved_placement_ = {sizeof(WINDOWPLACEMENT)};

    ComPtr<ID2D1Factory>          d2d_factory_;
    ComPtr<IDWriteFactory>        dw_factory_;
    ComPtr<ID2D1HwndRenderTarget> rt_;
    ComPtr<ID3D11Device>          d3d_device_;
    ComPtr<ID3D11DeviceContext>   d3d_context_;
    ComPtr<IDXGISwapChain>        swapchain_;
    RenderCb                      render_cb_;
};

}  // namespace meplayer
