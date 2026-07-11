// mpv 选项：三档画质预设 + 去交错 + TS 容错
// 移植自原项目 player_core.dart 的 mpvProfiles / _deinterlaceOptions / _tsResilienceOptions
#pragma once
#include <map>
#include <string>
#include <vector>

namespace meplayer {

struct MpvOptionSet {
    std::map<std::string, std::string> options;       // set_property 键值
    std::vector<std::string>           vfCommands;    // vf-add 命令参数
};

class MpvOptions {
public:
    // 画质预设（与原项目一致）
    //   performance: profile=fast, hwdec=auto-safe, vo=gpu, bilinear 缩放, display-resample
    //   balanced: Windows 留空（避免与渲染层冲突导致拖影）
    //   quality: profile=gpu-hq, vo=gpu-next, ewa_lanczossharp, sigmoid-upscaling, deband
    static MpvOptionSet profile(int profileIdx);

    // 去交错选项（IPTV 用）：deinterlace=yes
    static MpvOptionSet deinterlace();

    // TS 容错选项：demuxer-max-bytes / cache / cache-secs
    static MpvOptionSet tsResilience();

    // 字幕选项
    static MpvOptionSet subtitle(bool autoLoad, int fontSize);

    // 硬解开关（hwdec=auto-safe 或 no）
    static std::string hwdecValue(bool enabled);
};

}  // namespace meplayer
