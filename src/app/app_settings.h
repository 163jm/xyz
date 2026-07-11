// 应用设置（SQLite 持久化）
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace meplayer {

// mpv 画质预设
enum class MpvProfile {
    Performance = 0,  // 性能优先
    Balanced     = 1,  // 均衡（Windows 留空，避免与渲染层冲突）
    Quality      = 2,  // 画质优先
};

// 主题模式
enum class ThemeMode {
    System = 0,
    Light  = 1,
    Dark   = 2,
};

class AppSettings {
public:
    static AppSettings& instance();

    void load();
    void save();

    // ---- 外观 ----
    ThemeMode    themeMode() const  { return themeMode_; }
    void         setThemeMode(ThemeMode m) { themeMode_ = m; save(); }
    uint32_t     seedColor() const  { return seedColor_; }   // ARGB
    void         setSeedColor(uint32_t c) { seedColor_ = c; save(); }

    // ---- 播放 ----
    bool         hardwareDecode() const { return hwDecode_; }
    void         setHardwareDecode(bool v) { hwDecode_ = v; save(); }
    MpvProfile   mpvProfile() const { return mpvProfile_; }
    void         setMpvProfile(MpvProfile p) { mpvProfile_ = p; save(); }
    bool         deinterlace() const { return deinterlace_; }
    void         setDeinterlace(bool v) { deinterlace_ = v; save(); }
    bool         tsResilience() const { return tsResilience_; }
    void         setTsResilience(bool v) { tsResilience_ = v; save(); }
    bool         subAutoLoad() const { return subAutoLoad_; }
    void         setSubAutoLoad(bool v) { subAutoLoad_ = v; save(); }
    int          subFontSize() const { return subFontSize_; }
    void         setSubFontSize(int v) { subFontSize_ = v; save(); }
    int          volume() const { return volume_; }
    void         setVolume(int v) { volume_ = v; save(); }
    double       speed() const { return speed_; }
    void         setSpeed(double v) { speed_ = v; save(); }

    // 设置变更回调（用于主题刷新等）
    using ChangedCb = std::function<void()>;
    void addChangedCallback(ChangedCb cb);

private:
    AppSettings();
    void notifyChanged();

    ThemeMode    themeMode_  = ThemeMode::System;
    uint32_t     seedColor_  = 0xff3498db;  // 默认蓝
    bool         hwDecode_   = true;
    MpvProfile   mpvProfile_ = MpvProfile::Balanced;
    bool         deinterlace_    = true;
    bool         tsResilience_   = true;
    bool         subAutoLoad_    = true;
    int          subFontSize_    = 36;
    int          volume_         = 100;
    double       speed_          = 1.0;
    std::vector<ChangedCb> cbs_;
    std::mutex   mtx_;
};

}  // namespace meplayer
