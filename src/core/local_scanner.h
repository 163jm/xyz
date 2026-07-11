// 媒体文件扫描（Win32 递归）
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace meplayer {

// 扩展名常量（与原项目 media_file.dart 一致）
extern const std::vector<std::wstring> kVideoExts;  // .mp4 .mkv .avi .mov .flv .ts .wmv .m4v .webm .mpg .mpeg .m2ts .3gp
extern const std::vector<std::wstring> kMusicExts;  // .mp3 .flac .ogg .m4a .aac .wma .wav .ape .mka .opus

struct MediaItem {
    std::wstring path;
    std::wstring name;       // 含扩展名
    std::wstring folder;     // 所在目录名
    uint64_t     size = 0;
    int64_t      modifiedMs = 0;
    // 音乐元数据（扫描时读 Tag 填充，封面不填）
    std::string  title;      // UTF-8
    std::string  artist;
    std::string  album;
};

class LocalScanner {
public:
    // 扫描视频文件
    static std::vector<MediaItem> scanVideos(const std::wstring& root);

    // 扫描音乐文件（同时读 TagLib Tag，不读封面）
    static std::vector<MediaItem> scanMusics(const std::wstring& root);
};

}  // namespace meplayer
