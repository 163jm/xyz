// libmpv 后端封装（wid 嵌入模式）
//
// shinchiro 的 mpv-dev 包只带 render.h / render_gl.h，没有 render_d3d11.h，
// 且 mpv 官方源码本身也不提供 render_d3d11.h（D3D11 render API 仅第三方分支有）。
// 因此这里不使用 mpv render API，改用 wid 嵌入模式：
//   - 把 mpv 的视频输出直接嵌入到一个 Win32 子窗口（HWND）
//   - mpv 内部自行创建 D3D11/ANGLE 渲染上下文，无需宿主提供
//   - 宿主 UI（Direct2D）在子窗口之上/之外绘制
//   - 音乐播放器用 1x1 隐藏子窗口保活
#pragma once
#include <mpv/client.h>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace meplayer {

// mpv 属性回调
using MpvEventCb = std::function<void()>;

class MpvBackend {
public:
    MpvBackend();
    ~MpvBackend();

    // 初始化 mpv 实例
    //   videoHwnd: 视频输出嵌入的 Win32 子窗口句柄。
    //              传 nullptr 表示纯音频模式（音乐播放器），内部创建 1x1 隐藏窗口保活。
    //   parentHwnd: 父窗口句柄（用于创建隐藏保活子窗口），videoHwnd 为空时使用。
    bool init(HWND videoHwnd, HWND parentHwnd = nullptr);

    // 应用 mpv 选项（set_property）
    void applyOptions(const std::map<std::string, std::string>& opts);
    void applyVfCommands(const std::vector<std::string>& cmds);

    // 播放控制
    void open(const std::string& pathOrUrl);
    void play();
    void pause();
    void playPause();
    void seek(double seconds);  // absolute
    void setVolume(int v);
    void setSpeed(double rate);
    void setAspectRatio(const std::string& mode);  // "16:9" / "4:3" / "-1"
    void stop();

    // 状态读取
    double position() const { return position_.load(); }
    double duration() const { return duration_.load(); }
    bool   isPlaying() const { return !paused_.load(); }
    bool   isPaused() const { return paused_.load(); }
    bool   isBuffering() const { return buffering_.load(); }
    int    volume() const { return volume_.load(); }

    // 事件回调注册（属性变化时触发）
    using PropertyChangedCb = std::function<void(const std::string& prop)>;
    void setPropertyChangedCb(PropertyChangedCb cb) { prop_cb_ = std::move(cb); }
    using EndFileCb = std::function<void()>;
    void setEndFileCb(EndFileCb cb) { end_cb_ = std::move(cb); }

    // wid 模式下渲染由 mpv 内部处理，此函数为空操作（保留接口兼容）
    void render() {}

    // 调整嵌入子窗口尺寸（视频区大小变化时调用）
    void resize(int width, int height);

    // 释放
    void dispose();

    mpv_handle* handle() const { return mpv_; }
    HWND videoHwnd() const { return video_hwnd_; }

private:
    void eventLoop();

    mpv_handle*          mpv_ = nullptr;
    std::thread          event_thread_;
    std::atomic<bool>    exiting_{false};

    HWND                 video_hwnd_ = nullptr;   // 视频嵌入窗口
    bool                 owns_hwnd_ = false;      // 是否自建隐藏窗口（音乐播放器）
    HWND                 parent_hwnd_ = nullptr;

    // 状态
    std::atomic<double> position_{0};
    std::atomic<double> duration_{0};
    std::atomic<bool>   paused_{true};
    std::atomic<bool>   buffering_{false};
    std::atomic<int>    volume_{100};

    PropertyChangedCb prop_cb_;
    EndFileCb         end_cb_;
    std::mutex        cb_mutex_;
};

}  // namespace meplayer
