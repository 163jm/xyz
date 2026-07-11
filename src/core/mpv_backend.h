// libmpv 后端封装
#pragma once
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>

namespace meplayer {

using Microsoft::WRL::ComPtr;

// mpv 属性回调
using MpvEventCb = std::function<void()>;

class MpvBackend {
public:
    MpvBackend();
    ~MpvBackend();

    // 初始化 mpv 实例 + D3D11 渲染上下文
    //   d3dDevice: 共享主窗口的 D3D11 设备
    //   renderTarget: mpv 渲染输出的 D3D11 纹理 RTV（视频画面）
    //   若 renderTarget=nullptr，创建 1x1 隐藏纹理（音乐播放器保活用）
    bool init(ID3D11Device* d3dDevice, ID3D11RenderTargetView* renderTarget);

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

    // 渲染：将 mpv 当前帧绘制到绑定的 RTV
    void render();

    // 释放
    void dispose();

    mpv_handle* handle() const { return mpv_; }

private:
    void eventLoop();

    mpv_handle*          mpv_ = nullptr;
    mpv_render_context*  render_ctx_ = nullptr;
    std::thread          event_thread_;
    std::atomic<bool>    exiting_{false};

    ComPtr<ID3D11RenderTargetView> hidden_rtv_;  // 音乐播放器的 1x1 隐藏 RTV
    ID3D11RenderTargetView* target_rtv_ = nullptr;
    ID3D11Device*         d3d_device_ = nullptr;

    // 状态
    std::atomic<double> position_{0};
    std::atomic<double> duration_{0};
    std::atomic<bool>   paused_{true};
    std::atomic<bool>   buffering_{false};
    std::atomic<int>    volume_{100};

    PropertyChangedCb prop_cb_;
    EndFileCb         end_cb_;
};

}  // namespace meplayer
