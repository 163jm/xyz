// 全局音乐播放器（App 生命周期常驻）
// 对应原项目 GlobalPlayerController：音乐 mpv 实例常驻，
// 视频/IPTV 播放器为页面级（进页创建、退页销毁）。
#pragma once
#include "core/mpv_backend.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <memory>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace meplayer {

// 歌词行
struct LrcLine {
    double timeSec = 0;
    std::wstring text;
};

// 当前曲目元数据
struct TrackMeta {
    std::string title;
    std::string artist;
    std::vector<uint8_t> coverBytes;  // 内存封面
    std::vector<LrcLine> lrcLines;
};

// 循环模式
enum class RepeatMode { List, Shuffle, One };

// 底部迷你栏类型
enum class MiniBarKind { None, Video, Iptv, Music };

// 底部栏快照（视频/IPTV 退出播放页后保留，供续播）
struct VideoSnapshot {
    std::string url;
    std::string title;
    bool        isLocal = false;
    double      position = 0;
    double      duration = 0;
    // 播放列表（可选）
    std::vector<std::string> playlist;
    int          startIndex = 0;
};
struct IptvSnapshot {
    std::string url;
    std::string channelName;
    std::string groupName;
    int         sourceIndex = 0;
};

class GlobalPlayer {
public:
    static GlobalPlayer& instance();

    // 在 App 启动时创建常驻 mpv（预热，纯音频保活）
    bool warmup(HWND parentHwnd);

    // 音乐播放列表
    struct PlayItem {
        std::string path;
        std::string name;
    };
    void playPlaylist(const std::vector<PlayItem>& list, int index);
    void playAt(int index);
    void next();
    void prev();
    void togglePlay();
    void seekTo(double sec);
    void setVolume(int v);
    void setSpeed(double v);
    void cycleRepeat();

    // 视频/IPTV 开始播放时，暂停音乐
    void stopMusicForOtherPlayback();

    // 底部栏快照
    void saveVideoSnapshot(const VideoSnapshot& s);
    void saveIptvSnapshot(const IptvSnapshot& s);
    const VideoSnapshot& videoSnapshot() const { return video_snap_; }
    const IptvSnapshot&  iptvSnapshot()  const { return iptv_snap_; }
    MiniBarKind miniBarKind() const { return mini_kind_; }
    void setMiniBarKind(MiniBarKind k) { mini_kind_ = k; }

    // 音乐状态（供 UI 读取）
    int     musicIndex() const { return cur_idx_; }
    bool    musicIsPlaying() const;
    double  musicPosition() const;
    double  musicDuration() const;
    int     musicVolume() const;
    double  musicSpeed() const;
    RepeatMode repeatMode() const { return repeat_; }
    const std::vector<PlayItem>& playlist() const { return playlist_; }
    const TrackMeta& currentMeta() const { return meta_; }
    int currentLrcIdx() const { return lrc_idx_; }
    const std::vector<std::wstring>& channelGroups() const { return channel_groups_; }

    // 状态变更回调（UI 刷新）
    using ChangedCb = std::function<void()>;
    void addChangedCb(ChangedCb cb);
    void notifyChanged();

    MpvBackend* musicBackend() { return music_mpv_.get(); }

private:
    GlobalPlayer();
    void loadMeta(const std::string& path);
    void onEndFile();

    std::unique_ptr<MpvBackend> music_mpv_;
    std::vector<PlayItem> playlist_;
    int          cur_idx_ = -1;
    RepeatMode   repeat_  = RepeatMode::List;
    TrackMeta    meta_;
    int          lrc_idx_ = -1;
    std::vector<int> shuffle_order_;

    MiniBarKind  mini_kind_ = MiniBarKind::None;
    VideoSnapshot video_snap_;
    IptvSnapshot  iptv_snap_;
    std::vector<std::wstring> channel_groups_;

    std::vector<ChangedCb> cbs_;
    std::mutex mtx_;
};

}  // namespace meplayer
