// libmpv 后端封装（wid 嵌入模式）：mpv 实例 + 事件线程
#include "core/mpv_backend.h"
#include <cstring>
#include <cstdio>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace meplayer {

MpvBackend::MpvBackend() = default;

MpvBackend::~MpvBackend() {
    dispose();
}

// 纯音频模式的隐藏保活子窗口过程（仅处理 WM_DESTROY）
static LRESULT CALLBACK HiddenMpvWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, msg, w, l);
}

// 初始化 mpv 实例
//   videoHwnd: 视频输出嵌入的 Win32 子窗口；nullptr 则创建 1x1 隐藏窗口（音乐保活）
bool MpvBackend::init(HWND videoHwnd, HWND parentHwnd) {
    // 1. 创建 mpv 实例
    mpv_ = mpv_create();
    if (!mpv_) return false;

    // 2. 基础选项（须在 initialize 前设置）
    mpv_set_option_string(mpv_, "config", "no");
    mpv_set_option_string(mpv_, "terminal", "no");
    mpv_set_option_string(mpv_, "msg-level", "all=warn");

    // 3. 视频输出嵌入窗口
    if (videoHwnd == nullptr) {
        // 纯音频模式：创建 1x1 隐藏子窗口保活
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(parentHwnd, GWLP_HINSTANCE);
        static const wchar_t* kClass = L"MEPlayerHiddenMpv";
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc   = HiddenMpvWndProc;
            wc.hInstance     = hInst;
            wc.lpszClassName = kClass;
            RegisterClassW(&wc);
            registered = true;
        }
        video_hwnd_ = CreateWindowExW(0, kClass, L"", 0,
                                      0, 0, 1, 1,
                                      parentHwnd, nullptr, hInst, nullptr);
        owns_hwnd_ = true;
    } else {
        video_hwnd_ = videoHwnd;
        owns_hwnd_ = false;
    }
    parent_hwnd_ = parentHwnd;

    // 设置 wid：把 mpv 视频输出嵌入到该窗口
    // wid 在 Windows 上接受 HWND（以 int64_t 传递）
    int64_t wid = (int64_t)(intptr_t)video_hwnd_;
    mpv_set_option(mpv_, "wid", MPV_FORMAT_INT64, &wid);

    // Windows 上推荐 vo=gpu（ANGLE/D3D11），由 mpv 自行创建渲染上下文
    mpv_set_option_string(mpv_, "vo", "gpu");
    // 不让 mpv 拦截鼠标/键盘事件（由宿主处理）
    mpv_set_option_string(mpv_, "input-default-bindings", "no");
    mpv_set_option_string(mpv_, "input-vo-keyboard", "no");
    mpv_set_option_string(mpv_, "input-cursor", "no");
    // 不显示 OSC（屏幕控制器），UI 由宿主绘制
    mpv_set_option_string(mpv_, "osc", "no");
    // 保持窗口尺寸由宿主控制
    mpv_set_option_string(mpv_, "force-window", "no");

    // 4. 初始化 mpv
    if (mpv_initialize(mpv_) < 0) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        if (owns_hwnd_ && video_hwnd_) {
            DestroyWindow(video_hwnd_);
            video_hwnd_ = nullptr;
        }
        return false;
    }

    // 5. 启动事件线程
    event_thread_ = std::thread(&MpvBackend::eventLoop, this);

    // 6. 监听属性变化
    mpv_observe_property(mpv_, 0, "time-pos",         MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration",         MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "pause",            MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "volume",           MPV_FORMAT_INT64);

    return true;
}

// 事件线程：循环 mpv_wait_event，分发属性变化与结束事件
void MpvBackend::eventLoop() {
    while (!exiting_.load()) {
        mpv_event* event = mpv_wait_event(mpv_, 0.1);
        if (!event) continue;
        if (event->event_id == MPV_EVENT_NONE) continue;

        switch (event->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* prop = static_cast<mpv_event_property*>(event->data);
                if (!prop || !prop->name) break;
                std::string name = prop->name;

                // 按格式读取并更新原子变量
                if (name == "time-pos" && prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                    position_.store(*static_cast<double*>(prop->data));
                } else if (name == "duration" && prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                    duration_.store(*static_cast<double*>(prop->data));
                } else if (name == "pause" && prop->format == MPV_FORMAT_FLAG && prop->data) {
                    paused_.store(*static_cast<int*>(prop->data) != 0);
                } else if (name == "paused-for-cache" && prop->format == MPV_FORMAT_FLAG && prop->data) {
                    buffering_.store(*static_cast<int*>(prop->data) != 0);
                } else if (name == "volume" && prop->format == MPV_FORMAT_INT64 && prop->data) {
                    volume_.store(static_cast<int>(*static_cast<int64_t*>(prop->data)));
                }

                std::lock_guard<std::mutex> lk(cb_mutex_);
                if (prop_cb_) prop_cb_(name);
                break;
            }
            case MPV_EVENT_END_FILE: {
                std::lock_guard<std::mutex> lk(cb_mutex_);
                if (end_cb_) end_cb_();
                break;
            }
            case MPV_EVENT_SHUTDOWN: {
                return;
            }
            default:
                break;
        }
    }
}

// 应用 mpv 选项（set_property_string）
void MpvBackend::applyOptions(const std::map<std::string, std::string>& opts) {
    if (!mpv_) return;
    for (const auto& kv : opts) {
        mpv_set_property_string(mpv_, kv.first.c_str(), kv.second.c_str());
    }
}

// 应用 vf 命令：对每个参数执行 ["vf-add", param, nullptr]
void MpvBackend::applyVfCommands(const std::vector<std::string>& cmds) {
    if (!mpv_) return;
    for (const auto& cmd : cmds) {
        const char* args[] = {"vf-add", cmd.c_str(), nullptr};
        mpv_command(mpv_, args);
    }
}

// 打开文件 / URL
void MpvBackend::open(const std::string& pathOrUrl) {
    if (!mpv_) return;
    const char* args[] = {"loadfile", pathOrUrl.c_str(), nullptr};
    mpv_command(mpv_, args);
    paused_.store(false);
}

void MpvBackend::play() {
    if (!mpv_) return;
    int f = 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &f);
}

void MpvBackend::pause() {
    if (!mpv_) return;
    int f = 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &f);
}

void MpvBackend::playPause() {
    if (!mpv_) return;
    int f = paused_.load() ? 0 : 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &f);
}

// 跳转：absolute + exact
void MpvBackend::seek(double seconds) {
    if (!mpv_) return;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", seconds);
    const char* args[] = {"seek", buf, "absolute", "exact", nullptr};
    mpv_command(mpv_, args);
}

void MpvBackend::setVolume(int v) {
    if (!mpv_) return;
    int64_t vol = v;
    mpv_set_property(mpv_, "volume", MPV_FORMAT_INT64, &vol);
    volume_.store(v);
}

void MpvBackend::setSpeed(double rate) {
    if (!mpv_) return;
    mpv_set_property(mpv_, "speed", MPV_FORMAT_DOUBLE, &rate);
}

// 宽高比："-1" 表示 no（按原始比例）
void MpvBackend::setAspectRatio(const std::string& mode) {
    if (!mpv_) return;
    std::string v = (mode == "-1") ? "no" : mode;
    mpv_set_property_string(mpv_, "video-aspect-override", v.c_str());
}

void MpvBackend::stop() {
    if (!mpv_) return;
    const char* args[] = {"stop", nullptr};
    mpv_command(mpv_, args);
}

// 调整嵌入子窗口尺寸
void MpvBackend::resize(int width, int height) {
    if (!video_hwnd_ || !owns_hwnd_) return;  // 自建隐藏窗口无需 resize
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    SetWindowPos(video_hwnd_, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
}

// 释放：置退出标志 -> 销毁 mpv -> join 线程 -> 销毁隐藏窗口
void MpvBackend::dispose() {
    if (exiting_.exchange(true)) return;  // 防止重复释放

    if (mpv_) {
        mpv_terminate_destroy(mpv_);  // 会使 mpv_wait_event 返回 SHUTDOWN
        mpv_ = nullptr;
    }
    if (event_thread_.joinable()) {
        event_thread_.join();
    }

    if (owns_hwnd_ && video_hwnd_) {
        DestroyWindow(video_hwnd_);
        video_hwnd_ = nullptr;
    }
    owns_hwnd_ = false;
    parent_hwnd_ = nullptr;
}

}  // namespace meplayer
