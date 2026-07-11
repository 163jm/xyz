#include "app/app_settings.h"
#include "core/storage.h"

namespace meplayer {

AppSettings& AppSettings::instance() {
    static AppSettings inst;
    return inst;
}

AppSettings::AppSettings() {}

void AppSettings::load() {
    auto& s = Storage::instance();
    themeMode_    = static_cast<ThemeMode>(s.getInt("theme_mode", 0));
    seedColor_    = static_cast<uint32_t>(std::stoul(s.getString("seed_color", "4288066267"))); // 0xff3498db
    hwDecode_     = s.getBool("hw_decode", true);
    mpvProfile_   = static_cast<MpvProfile>(s.getInt("mpv_profile", 1));
    deinterlace_  = s.getBool("deinterlace", true);
    tsResilience_ = s.getBool("ts_resilience", true);
    subAutoLoad_  = s.getBool("sub_auto_load", true);
    subFontSize_  = s.getInt("sub_font_size", 36);
    volume_       = s.getInt("volume", 100);
    speed_        = std::stod(s.getString("speed", "1.0"));
}

void AppSettings::save() {
    auto& s = Storage::instance();
    s.setInt("theme_mode", static_cast<int>(themeMode_));
    s.setString("seed_color", std::to_string(seedColor_));
    s.setBool("hw_decode", hwDecode_);
    s.setInt("mpv_profile", static_cast<int>(mpvProfile_));
    s.setBool("deinterlace", deinterlace_);
    s.setBool("ts_resilience", tsResilience_);
    s.setBool("sub_auto_load", subAutoLoad_);
    s.setInt("sub_font_size", subFontSize_);
    s.setInt("volume", volume_);
    s.setString("speed", std::to_string(speed_));
    notifyChanged();
}

void AppSettings::addChangedCallback(ChangedCb cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    cbs_.push_back(cb);
}

void AppSettings::notifyChanged() {
    std::vector<ChangedCb> cbs;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cbs = cbs_;
    }
    for (auto& cb : cbs) cb();
}

}  // namespace meplayer
