// 全局音乐播放器（App 生命周期常驻）
// 对应原项目 GlobalPlayerController：音乐 mpv 实例常驻，
// 视频/IPTV 播放器为页面级（进页创建、退页销毁）。
#include "app/global_player.h"
#include "app/app_settings.h"
#include "core/audio_metadata.h"
#include "util/string_util.h"
#include <mpv/client.h>
#include <regex>
#include <algorithm>
#include <random>
#include <thread>
#include <sstream>
#include <cmath>

namespace meplayer {

// 解析 [mm:ss.xx] 格式歌词，转成 LrcLine{timeSec, text(wide)}
// 支持每行多个时间戳（如 [01:12.34][02:34.56]歌词），分别生成多行
static std::vector<LrcLine> parseLyrics(const std::string& lyrics) {
    std::vector<LrcLine> lines;
    if (lyrics.empty()) return lines;

    std::wstring wlyrics = utf8_to_wide(lyrics);
    std::wregex ts_re(L"\\[(\\d+):(\\d+)\\.(\\d{1,3})\\]");

    // 按行处理
    std::wistringstream ss(wlyrics);
    std::wstring line;
    while (std::getline(ss, line)) {
        // 提取行首所有时间戳
        std::vector<double> times;
        std::wstring rest = line;
        std::wsmatch m;
        while (std::regex_search(rest, m, ts_re)) {
            if (m.position() != 0) break;  // 时间戳必须出现在行首
            int mm = std::stoi(m[1].str());
            int sec = std::stoi(m[2].str());
            std::wstring frac_str = m[3].str();
            double frac = std::stoi(frac_str) /
                          std::pow(10.0, static_cast<int>(frac_str.length()));
            times.push_back(mm * 60 + sec + frac);
            rest = m.suffix().str();
        }
        // 无时间戳的行跳过
        if (times.empty()) continue;
        std::wstring text = wtrim(rest);
        for (double t : times) {
            LrcLine lrc;
            lrc.timeSec = t;
            lrc.text    = text;
            lines.push_back(lrc);
        }
    }

    // 按时间升序排序
    std::sort(lines.begin(), lines.end(),
              [](const LrcLine& a, const LrcLine& b) { return a.timeSec < b.timeSec; });
    return lines;
}

GlobalPlayer& GlobalPlayer::instance() {
    static GlobalPlayer inst;
    return inst;
}

GlobalPlayer::GlobalPlayer() = default;

// 预热：创建常驻 mpv（纯音频，1x1 隐藏保活窗口），绑定结束回调与音量
bool GlobalPlayer::warmup(HWND parentHwnd) {
    music_mpv_ = std::make_unique<MpvBackend>();
    if (!music_mpv_->init(nullptr, parentHwnd)) {
        music_mpv_.reset();
        return false;
    }
    music_mpv_->setEndFileCb([this]() { onEndFile(); });
    // 应用持久化音量
    int vol = AppSettings::instance().volume();
    music_mpv_->setVolume(vol);
    return true;
}

// 播放列表：停止当前 -> 记录列表 -> 依据循环模式构造 shuffle 顺序 -> 播放指定项
void GlobalPlayer::playPlaylist(const std::vector<PlayItem>& list, int index) {
    if (music_mpv_) music_mpv_->stop();
    playlist_ = list;
    cur_idx_  = index;

    // 依据 repeat_ 构造 shuffle_order_：Shuffle 模式生成随机排列
    shuffle_order_.clear();
    if (!list.empty()) {
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            shuffle_order_.push_back(i);
        }
        if (repeat_ == RepeatMode::Shuffle) {
            std::shuffle(shuffle_order_.begin(), shuffle_order_.end(),
                         std::mt19937(std::random_device()()));
        }
    }

    if (index >= 0 && index < static_cast<int>(list.size())) {
        playAt(index);
    }
}

// 播放指定索引：open + 异步加载元数据 + 通知 UI
void GlobalPlayer::playAt(int index) {
    if (index < 0 || index >= static_cast<int>(playlist_.size())) return;
    std::string path = playlist_[index].path;
    cur_idx_  = index;
    mini_kind_ = MiniBarKind::Music;

    if (music_mpv_) music_mpv_->open(path);

    // 异步加载元数据（TagLib 读取较重，避免阻塞 UI 线程）
    std::thread([this, path]() {
        loadMeta(path);
    }).detach();

    notifyChanged();
}

// 读取元数据并解析歌词，更新 meta_
void GlobalPlayer::loadMeta(const std::string& path) {
    AudioMetadata md = AudioMetadataReader::readFile(path);
    TrackMeta m;
    m.title      = md.title;
    m.artist     = md.artist;
    m.coverBytes = md.coverBytes;
    m.lrcLines   = parseLyrics(md.lyrics);

    {
        std::lock_guard<std::mutex> lk(mtx_);
        meta_    = std::move(m);
        lrc_idx_ = -1;
    }
    notifyChanged();
}

// 下一首：List 循环，Shuffle 按 shuffle_order_，One 重复当前
void GlobalPlayer::next() {
    if (playlist_.empty()) return;
    int n = static_cast<int>(playlist_.size());

    if (repeat_ == RepeatMode::One) {
        playAt(cur_idx_);
        return;
    }

    if (repeat_ == RepeatMode::Shuffle) {
        if (shuffle_order_.empty()) { playAt(0); return; }
        int cur_pos = -1;
        for (int i = 0; i < static_cast<int>(shuffle_order_.size()); ++i) {
            if (shuffle_order_[i] == cur_idx_) { cur_pos = i; break; }
        }
        if (cur_pos < 0) { playAt(shuffle_order_[0]); return; }
        int next_pos = (cur_pos + 1 < static_cast<int>(shuffle_order_.size()))
                       ? cur_pos + 1 : 0;  // 末尾回到开头
        playAt(shuffle_order_[next_pos]);
    } else {  // List
        int next_idx = (cur_idx_ + 1 < n) ? cur_idx_ + 1 : 0;
        playAt(next_idx);
    }
}

// 上一首
void GlobalPlayer::prev() {
    if (playlist_.empty()) return;
    int n = static_cast<int>(playlist_.size());

    if (repeat_ == RepeatMode::One) {
        playAt(cur_idx_);
        return;
    }

    if (repeat_ == RepeatMode::Shuffle) {
        if (shuffle_order_.empty()) { playAt(0); return; }
        int cur_pos = -1;
        for (int i = 0; i < static_cast<int>(shuffle_order_.size()); ++i) {
            if (shuffle_order_[i] == cur_idx_) { cur_pos = i; break; }
        }
        if (cur_pos < 0) { playAt(shuffle_order_[0]); return; }
        int prev_pos = (cur_pos - 1 >= 0)
                       ? cur_pos - 1
                       : static_cast<int>(shuffle_order_.size()) - 1;
        playAt(shuffle_order_[prev_pos]);
    } else {  // List
        int prev_idx = (cur_idx_ - 1 >= 0) ? cur_idx_ - 1 : n - 1;
        playAt(prev_idx);
    }
}

void GlobalPlayer::togglePlay() {
    if (music_mpv_) music_mpv_->playPause();
    notifyChanged();
}

void GlobalPlayer::seekTo(double sec) {
    if (music_mpv_) music_mpv_->seek(sec);
    notifyChanged();
}

void GlobalPlayer::setVolume(int v) {
    if (music_mpv_) music_mpv_->setVolume(v);
    notifyChanged();
}

void GlobalPlayer::setSpeed(double v) {
    if (music_mpv_) music_mpv_->setSpeed(v);
    notifyChanged();
}

// 切换循环模式：List -> Shuffle -> One -> List
void GlobalPlayer::cycleRepeat() {
    switch (repeat_) {
        case RepeatMode::List:    repeat_ = RepeatMode::Shuffle; break;
        case RepeatMode::Shuffle: repeat_ = RepeatMode::One;     break;
        case RepeatMode::One:     repeat_ = RepeatMode::List;    break;
    }
    // 切换到 Shuffle 时重新生成随机顺序
    if (repeat_ == RepeatMode::Shuffle && !playlist_.empty()) {
        shuffle_order_.clear();
        for (int i = 0; i < static_cast<int>(playlist_.size()); ++i) {
            shuffle_order_.push_back(i);
        }
        std::shuffle(shuffle_order_.begin(), shuffle_order_.end(),
                     std::mt19937(std::random_device()()));
    }
    notifyChanged();
}

// 视频/IPTV 开始播放时暂停音乐
void GlobalPlayer::stopMusicForOtherPlayback() {
    if (music_mpv_) music_mpv_->pause();
}

// 底部栏快照
void GlobalPlayer::saveVideoSnapshot(const VideoSnapshot& s) {
    video_snap_ = s;
    mini_kind_  = MiniBarKind::Video;
    notifyChanged();
}

void GlobalPlayer::saveIptvSnapshot(const IptvSnapshot& s) {
    iptv_snap_ = s;
    mini_kind_ = MiniBarKind::Iptv;
    notifyChanged();
}

// 状态回调注册 / 通知
void GlobalPlayer::addChangedCb(ChangedCb cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    cbs_.push_back(std::move(cb));
}

void GlobalPlayer::notifyChanged() {
    std::vector<ChangedCb> cbs;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cbs = cbs_;
    }
    for (auto& cb : cbs) cb();
}

// 音乐状态：转发到 music_mpv_
bool   GlobalPlayer::musicIsPlaying() const {
    return music_mpv_ ? music_mpv_->isPlaying() : false;
}
double GlobalPlayer::musicPosition() const {
    return music_mpv_ ? music_mpv_->position() : 0.0;
}
double GlobalPlayer::musicDuration() const {
    return music_mpv_ ? music_mpv_->duration() : 0.0;
}
int    GlobalPlayer::musicVolume() const {
    return music_mpv_ ? music_mpv_->volume() : 0;
}
// MpvBackend 未暴露 speed 访问器，这里直接从 mpv 句柄读取
double GlobalPlayer::musicSpeed() const {
    if (!music_mpv_ || !music_mpv_->handle()) return 1.0;
    double speed = 1.0;
    mpv_get_property(music_mpv_->handle(), "speed", MPV_FORMAT_DOUBLE, &speed);
    return speed;
}

// 当前曲目结束：One 重复，List/Shuffle 下一首
void GlobalPlayer::onEndFile() {
    if (repeat_ == RepeatMode::One) {
        playAt(cur_idx_);
    } else {
        next();
    }
}

}  // namespace meplayer
