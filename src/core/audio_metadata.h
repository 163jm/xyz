// 音乐元数据读取（TagLib 封装）
// 对应原项目 AudioMetadataReader：Tag（title/artist/album）与封面分离读取
//   扫描全量只读 Tag（快），封面按需读取（内存缓存）
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace meplayer {

struct AudioMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::string lyrics;             // 内嵌或同名 .lrc
    std::vector<uint8_t> coverBytes; // 嵌入封面（APIC / PICTURE）
};

class AudioMetadataReader {
public:
    // 全量读取（Tag + 封面 + 歌词）。filePath 为 UTF-8
    static AudioMetadata readFile(const std::string& filePath);

    // 仅读 Tag（title/artist/album），不读封面。扫描全量用，快
    static AudioMetadata readTagOnly(const std::string& filePath);

    // 仅读封面字节
    static std::vector<uint8_t> readPicture(const std::string& filePath);

    // 读歌词（内嵌 USLT + 同名 .lrc）
    static std::string readLyrics(const std::string& filePath);
};

}  // namespace meplayer
