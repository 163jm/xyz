// mpv 选项：三档画质预设 + 去交错 + TS 容错
// 移植自原项目 player_core.dart 的 mpvProfiles / _deinterlaceOptions / _tsResilienceOptions
#include "core/mpv_options.h"

namespace meplayer {

// 画质预设（与原项目一致）
//   performance(idx=0): profile=fast, hwdec=auto-safe, vo=gpu, bilinear 缩放, display-resample
//   balanced(idx=1):    Windows 留空（避免与渲染层冲突导致拖影）
//   quality(idx=2):     profile=gpu-hq, vo=gpu-next, ewa_lanczossharp, sigmoid-upscaling, deband
MpvOptionSet MpvOptions::profile(int profileIdx) {
    MpvOptionSet set;
    switch (profileIdx) {
        case 0: {  // performance：性能优先
            set.options["profile"]    = "fast";
            set.options["hwdec"]      = "auto-safe";
            set.options["vo"]         = "gpu";
            set.options["scale"]      = "bilinear";
            set.options["cscale"]     = "bilinear";
            set.options["dscale"]     = "bilinear";
            set.options["video-sync"] = "display-resample";
            break;
        }
        case 1: {  // balanced：Windows 专用，刻意不下发 vo/hwdec，避免与渲染层冲突导致拖影
            break;
        }
        case 2: {  // quality：画质优先
            set.options["profile"]          = "gpu-hq";
            set.options["vo"]               = "gpu-next";
            set.options["scale"]            = "ewa_lanczossharp";
            set.options["cscale"]           = "ewa_lanczossharp";
            set.options["sigmoid-upscaling"] = "yes";
            set.options["deband"]           = "yes";
            break;
        }
        default:
            break;
    }
    return set;
}

// 去交错选项（IPTV 用）：deinterlace=yes
MpvOptionSet MpvOptions::deinterlace() {
    MpvOptionSet set;
    set.options["deinterlace"] = "yes";
    return set;
}

// TS 容错选项：demuxer-max-bytes / cache / cache-secs
MpvOptionSet MpvOptions::tsResilience() {
    MpvOptionSet set;
    set.options["demuxer-max-bytes"] = "157286400";  // 150MiB
    set.options["cache"]             = "yes";
    set.options["cache-secs"]        = "10";
    return set;
}

// 字幕选项：autoLoad 时下发 sub-auto=fuzzy，始终下发 sub-font-size
MpvOptionSet MpvOptions::subtitle(bool autoLoad, int fontSize) {
    MpvOptionSet set;
    if (autoLoad) {
        set.options["sub-auto"] = "fuzzy";
    }
    set.options["sub-font-size"] = std::to_string(fontSize);
    return set;
}

// 硬解开关：enabled -> "auto-safe"，否则 "no"
std::string MpvOptions::hwdecValue(bool enabled) {
    return enabled ? "auto-safe" : "no";
}

}  // namespace meplayer
