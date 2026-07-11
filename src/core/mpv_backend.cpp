// libmpv 后端封装：mpv 实例 + D3D11 渲染上下文 + 事件线程
#include "core/mpv_backend.h"
#include <cstring>
#include <cstdio>
#include <utility>

namespace meplayer {

MpvBackend::MpvBackend() = default;

MpvBackend::~MpvBackend() {
    dispose();
}

// 初始化 mpv 实例 + D3D11 渲染上下文
//   d3dDevice:    共享主窗口的 D3D11 设备
//   renderTarget: mpv 渲染输出的 RTV（视频画面）；为 nullptr 时创建 1x1 隐藏 RTV（音乐保活用）
bool MpvBackend::init(ID3D11Device* d3dDevice, ID3D11RenderTargetView* renderTarget) {
    if (!d3dDevice) return false;
    d3d_device_ = d3dDevice;

    // 1. 创建 mpv 实例
    mpv_ = mpv_create();
    if (!mpv_) return false;

    // 2. 基础选项（须在 initialize 前设置）
    mpv_set_option_string(mpv_, "config", "no");
    mpv_set_option_string(mpv_, "terminal", "no");
    mpv_set_option_string(mpv_, "msg-level", "all=warn");

    // 3. 初始化 mpv
    if (mpv_initialize(mpv_) < 0) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    // 4. 创建 D3D11 渲染上下文
    ComPtr<ID3D11DeviceContext> ctx;
    d3dDevice->GetImmediateContext(&ctx);

    mpv_render_d3d11_device_params d3d_params = {};
    d3d_params.device  = d3dDevice;
    d3d_params.context = ctx.Get();

    mpv_render_param create_params[] = {
        {MPV_RENDER_PARAM_API_TYPE,                               const_cast<char*>(MPV_RENDER_API_TYPE_D3D11)},
        {static_cast<mpv_render_param_type>(MPV_RENDER_PARAM_D3D11_DEVICE), &d3d_params},
        {MPV_RENDER_PARAM_INVALID,                                nullptr},
    };
    if (mpv_render_context_create(&render_ctx_, mpv_, create_params) < 0) {
        render_ctx_ = nullptr;
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    // 5. 设置渲染目标
    if (renderTarget == nullptr) {
        // 音乐播放器保活用：创建 1x1 隐藏纹理 + RTV
        D3D11_TEXTURE2D_DESC tex_desc = {};
        tex_desc.Width            = 1;
        tex_desc.Height           = 1;
        tex_desc.MipLevels        = 1;
        tex_desc.ArraySize        = 1;
        tex_desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Usage            = D3D11_USAGE_DEFAULT;
        tex_desc.BindFlags        = D3D11_BIND_RENDER_TARGET;

        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(d3dDevice->CreateTexture2D(&tex_desc, nullptr, &tex))) {
            mpv_render_context_free(render_ctx_);
            render_ctx_ = nullptr;
            mpv_terminate_destroy(mpv_);
            mpv_ = nullptr;
            return false;
        }

        D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
        rtv_desc.Format         = tex_desc.Format;
        rtv_desc.ViewDimension  = D3D11_RTV_DIMENSION_TEXTURE2D;

        if (FAILED(d3dDevice->CreateRenderTargetView(tex.Get(), &rtv_desc, &hidden_rtv_))) {
            mpv_render_context_free(render_ctx_);
            render_ctx_ = nullptr;
            mpv_terminate_destroy(mpv_);
            mpv_ = nullptr;
            return false;
        }
        target_rtv_ = hidden_rtv_.Get();
    } else {
        target_rtv_ = renderTarget;
    }

    // 6. 启动事件线程
    event_thread_ = std::thread(&MpvBackend::eventLoop, this);

    // 7. 监听属性变化
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

                if (prop_cb_) prop_cb_(name);
                break;
            }
            case MPV_EVENT_END_FILE: {
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

// 渲染：将 mpv 当前帧绘制到绑定的 RTV
void MpvBackend::render() {
    if (!render_ctx_ || !target_rtv_) return;
    ID3D11RenderTargetView* rtv = target_rtv_;
    mpv_render_param params[] = {
        {static_cast<mpv_render_param_type>(MPV_RENDER_PARAM_D3D11_RTV), &rtv},
        {MPV_RENDER_PARAM_INVALID,                                        nullptr},
    };
    mpv_render_context_render(render_ctx_, params);
}

// 释放：置退出标志 -> 释放渲染上下文 -> 销毁 mpv -> join 线程
void MpvBackend::dispose() {
    if (exiting_.exchange(true)) return;  // 防止重复释放

    if (render_ctx_) {
        mpv_render_context_free(render_ctx_);
        render_ctx_ = nullptr;
    }
    if (mpv_) {
        mpv_terminate_destroy(mpv_);  // 会使 mpv_wait_event 返回 SHUTDOWN
        mpv_ = nullptr;
    }
    if (event_thread_.joinable()) {
        event_thread_.join();
    }

    hidden_rtv_.Reset();
    target_rtv_ = nullptr;
    d3d_device_ = nullptr;
}

}  // namespace meplayer
