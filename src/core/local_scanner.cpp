#include "core/local_scanner.h"
#include "core/audio_metadata.h"
#include "util/string_util.h"
#include "util/file_util.h"

namespace meplayer {

const std::vector<std::wstring> kVideoExts = {
    L".mp4", L".mkv", L".avi", L".mov", L".flv", L".ts", L".wmv",
    L".m4v", L".webm", L".mpg", L".mpeg", L".m2ts", L".3gp"
};

const std::vector<std::wstring> kMusicExts = {
    L".mp3", L".flac", L".ogg", L".m4a", L".aac", L".wma",
    L".wav", L".ape", L".mka", L".opus"
};

static MediaItem toMediaItem(const util::ScannedFile& sf) {
    MediaItem m;
    m.path = sf.path;
    m.name = sf.name;
    auto d = util::dir_name(sf.path);
    // folder 取最后一级
    auto pos = d.find_last_of(L"\\/");
    m.folder = (pos == std::wstring::npos) ? d : d.substr(pos + 1);
    m.size = sf.size;
    m.modifiedMs = sf.modifiedMs;
    return m;
}

std::vector<MediaItem> LocalScanner::scanVideos(const std::wstring& root) {
    std::vector<MediaItem> out;
    auto files = util::scan_dir(root, kVideoExts);
    out.reserve(files.size());
    for (auto& sf : files) out.push_back(toMediaItem(sf));
    return out;
}

std::vector<MediaItem> LocalScanner::scanMusics(const std::wstring& root) {
    std::vector<MediaItem> out;
    auto files = util::scan_dir(root, kMusicExts);
    out.reserve(files.size());
    for (auto& sf : files) {
        MediaItem m = toMediaItem(sf);
        // 读 Tag（不读封面，快）
        std::string u8path = util::wide_to_utf8(sf.path);
        auto meta = AudioMetadataReader::readTagOnly(u8path);
        m.title  = meta.title;
        m.artist = meta.artist;
        m.album  = meta.album;
        out.push_back(std::move(m));
    }
    return out;
}

}  // namespace meplayer
