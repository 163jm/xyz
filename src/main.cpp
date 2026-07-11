// MEPlayer 主入口
// 原生 Windows 媒体播放器（libmpv + Direct2D）
// 对应原项目 Flutter Windows 端 main.dart 的启动流程
#include <windows.h>
#include <objbase.h>
#include <shellscalingapi.h>
#include <memory>
#pragma comment(lib, "shcore.lib")

#include "app/window_manager.h"
#include "app/tray_service.h"
#include "app/data_dir.h"
#include "app/app_settings.h"
#include "app/global_player.h"
#include "app/theme.h"
#include "core/storage.h"
#include "ui/ui_widgets.h"
#include "modules/home/home_page.h"
#include "modules/video/player/video_player_page.h"
#include "modules/music/player/music_player_page.h"

namespace meplayer {

// 应用单例：管理页面栈和生命周期
class App {
public:
    static App& instance() { static App a; return a; }

    void init(HINSTANCE hInst) {
        // 1. 数据目录
        AppDataDir::instance().ensureCreated();

        // 2. SQLite 存储
        Storage::instance().init(AppDataDir::instance().dbPath());

        // 3. 设置加载
        AppSettings::instance().load();

        // 4. COM 初始化（D2D/WIC/MF 需要）
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        // 5. 窗口管理器初始化
        WindowManager::instance().init(hInst);

        // 6. 主题初始化
        bool dark = isDarkMode();
        ThemeManager::instance().update(AppSettings::instance().seedColor(), dark);
        AppSettings::instance().addChangedCallback([this]() {
            bool d = isDarkMode();
            ThemeManager::instance().update(AppSettings::instance().seedColor(), d);
            WindowManager::instance().requestRedraw();
        });

        // 7. 托盘
        TrayService::instance().init(WindowManager::instance().hwnd(), hInst);

        // 8. 全局音乐播放器预热
        GlobalPlayer::instance().warmup(WindowManager::instance().hwnd());

        // 9. 主页
        homePage_ = std::make_shared<HomePage>();
        homePage_->onPlayVideo = [this](const std::string& url,
                                        const std::string& title, bool isLocal) {
            // 跳转视频播放页
            VideoPlayerPage::Args args;
            args.url = url;
            args.title = title;
            args.isLocal = isLocal;
            currentPage_ = std::make_shared<VideoPlayerPage>(args, [this]() {
                currentPage_.reset();
                WindowManager::instance().requestRedraw();
            });
            currentPage_->onActive();
            WindowManager::instance().requestRedraw();
        };
        homePage_->onPlayMusic = [this]() {
            currentPage_ = std::make_shared<MusicPlayerPage>([this]() {
                currentPage_.reset();
                WindowManager::instance().requestRedraw();
            });
            currentPage_->onActive();
            WindowManager::instance().requestRedraw();
        };

        // 10. 渲染回调
        WindowManager::instance().setRenderCallback([this]() {
            render();
        });

        // 10.1 鼠标事件回调：转发给当前活动页面（子页面优先于主页）
        WindowManager::instance().setMouseDownCallback([this](const MouseEvent& e) {
            Page* p = activePage();
            if (p && p->onMouseDown(e)) WindowManager::instance().requestRedraw();
        });
        WindowManager::instance().setMouseUpCallback([this](const MouseEvent& e) {
            Page* p = activePage();
            if (p && p->onMouseUp(e)) WindowManager::instance().requestRedraw();
        });
        WindowManager::instance().setMouseMoveCallback([this](const MouseEvent& e) {
            Page* p = activePage();
            if (p && p->onMouseMove(e)) WindowManager::instance().requestRedraw();
        });
        WindowManager::instance().setMouseWheelCallback([this](const MouseEvent& e) {
            Page* p = activePage();
            if (p && p->onMouseWheel(e)) WindowManager::instance().requestRedraw();
        });

        // 11. 显示窗口
        WindowManager::instance().show();
    }

    // 当前应接收输入事件/参与渲染的页面：子页面（视频/音乐播放页等）优先于主页
    Page* activePage() {
        if (currentPage_) return currentPage_.get();
        if (homePage_) return homePage_.get();
        return nullptr;
    }

    void render() {
        auto* rt = WindowManager::instance().rt();
        if (!rt) return;
        auto& sc = ThemeManager::instance().current();
        rt->Clear(sc.background);

        Rect full = {0, 0,
                     static_cast<float>(WindowManager::instance().width()),
                     static_cast<float>(WindowManager::instance().height())};
        if (currentPage_) {
            currentPage_->layout(full);
            currentPage_->needsLayout = false;
            currentPage_->draw(rt);
        } else if (homePage_) {
            homePage_->layout(full);
            homePage_->needsLayout = false;
            homePage_->draw(rt);
        }
    }

    void run() {
        WindowManager::instance().runMessageLoop();
    }

    void shutdown() {
        TrayService::instance().shutdown();
        GlobalPlayer::instance().musicBackend()->dispose();
        CoUninitialize();
    }

private:
    App() = default;

    static bool isDarkMode() {
        auto tm = AppSettings::instance().themeMode();
        if (tm == ThemeMode::Light) return false;
        if (tm == ThemeMode::Dark)  return true;
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD val = 0, sz = sizeof(val);
            RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&val), &sz);
            RegCloseKey(hKey);
            return val == 0;
        }
        return false;
    }

    std::shared_ptr<HomePage> homePage_;
    std::shared_ptr<Page>     currentPage_;
};

}  // namespace meplayer

// ---- WinMain ----
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // 声明进程为 Per-Monitor-V2 DPI 感知。
    // 若不声明，Windows 会对整个窗口做位图缩放：
    // - 界面被整体缩放/挤到左上角一小块区域（视觉错位）
    // - 鼠标消息坐标是"虚拟化"坐标，与实际渲染坐标不一致，导致点击无响应或点错位置
    // 必须在创建任何窗口之前调用。
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI* SetCtxFn)(DPI_AWARENESS_CONTEXT);
    bool dpiSet = false;
    if (hUser32) {
        auto setCtx = reinterpret_cast<SetCtxFn>(
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
        if (setCtx) {
            dpiSet = setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
        }
    }
    if (!dpiSet) {
        // 回退：Windows 8.1+ Per-Monitor DPI 感知
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    }

    auto& app = meplayer::App::instance();
    app.init(hInst);
    app.run();
    app.shutdown();
    return 0;
}

// 控制台模式备用入口（便于调试）
int main() {
    return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOWDEFAULT);
}
