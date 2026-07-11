#include "core/audio_metadata.h"
#include "util/string_util.h"
#include <taglib/tag.h>
#include <taglib/fileref.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/id3v2frame.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/vorbisfile.h>
#include <taglib/xiphcomment.h>
#include <taglib/oggflacfile.h>
#include <taglib/mp4file.h>
#include <taglib/opusfile.h>
#include <fstream>
#include <filesystem>

namespace meplayer {

// 文件路径 UTF-8 → TagLib 宽字符
static TagLib::FileName toFileName(const std::string& utf8) {
    return TagLib::FileName(util::utf8_to_wide(utf8).c_str());
}

// 从文件名提取标题（去扩展名）
static std::string titleFromFilename(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    std::string name = (pos == std::string::npos) ? path : path.substr(pos + 1);
    auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

// 查找同名 .lrc 文件
static std::string readExternalLrc(const std::string& audioPath) {
    auto dot = audioPath.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string base = audioPath.substr(0, dot);

    // UTF-8 → wide
    auto tryRead = [](const std::wstring& path) -> std::string {
        std::ifstream f(path, std::ios::binary);
        if (!f) return "";
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        // 简单 BOM 检测
        if (content.size() >= 3 &&
            (unsigned char)content[0] == 0xEF &&
            (unsigned char)content[1] == 0xBB &&
            (unsigned char)content[2] == 0xBF) {
            return content.substr(3);
        }
        return content;
    };

    std::wstring wbase = util::utf8_to_wide(base);
    std::string lrc = tryRead(wbase + L".lrc");
    if (!lrc.empty()) return lrc;
    lrc = tryRead(wbase + L".LRC");
    return lrc;
}

// ---- ID3v2 封面 ----
static std::vector<uint8_t> readId3v2Picture(TagLib::ID3v2::Tag* tag) {
    if (!tag) return {};
    auto frames = tag->frameList("APIC");
    if (frames.isEmpty()) return {};
    auto* frame = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
    if (!frame) return {};
    auto& data = frame->picture();
    return std::vector<uint8_t>(data.data(), data.data() + data.size());
}

// ---- ID3v2 歌词（USLT）----
static std::string readId3v2Lyrics(TagLib::ID3v2::Tag* tag) {
    if (!tag) return "";
    auto frames = tag->frameList("USLT");
    if (frames.isEmpty()) return "";
    auto* frame = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame*>(frames.front());
    if (!frame) return "";
    return frame->text().to8Bit(true);
}

// ---- FLAC 封面 ----
static std::vector<uint8_t> readFlacPicture(TagLib::FLAC::File* file) {
    if (!file) return {};
    auto& list = file->pictureList();
    if (list.isEmpty()) return {};
    auto* pic = list.front();
    if (!pic) return {};
    auto& data = pic->data();
    return std::vector<uint8_t>(data.data(), data.data() + data.size());
}

// ---- Vorbis Comment 歌词 ----
static std::string readXiphLyrics(TagLib::Ogg::XiphComment* c) {
    if (!c) return "";
    auto f = c->fieldListMap().find("LYRICS");
    if (f == c->fieldListMap().end()) {
        f = c->fieldListMap().find("UNSYNCEDLYRICS");
    }
    if (f == c->fieldListMap().end() || f->second.isEmpty()) return "";
    return f->second.toString().to8Bit(true);
}

// ===================== 公共 API =====================

AudioMetadata AudioMetadataReader::readFile(const std::string& filePath) {
    AudioMetadata m;
    auto fn = toFileName(filePath);

    // 先用 FileRef 读基础 Tag
    TagLib::FileRef f(fn);
    if (!f.isNull() && f.tag()) {
        auto* tag = f.tag();
        m.title  = tag->title().to8Bit(true);
        m.artist = tag->artist().to8Bit(true);
        m.album  = tag->album().to8Bit(true);
    }
    if (m.title.empty()) m.title = titleFromFilename(filePath);

    // 按格式读封面和歌词
    // MP3
    {
        TagLib::MPEG::File mp3(fn);
        if (mp3.isValid()) {
            if (mp3.ID3v2Tag()) {
                m.coverBytes = readId3v2Picture(mp3.ID3v2Tag());
                m.lyrics = readId3v2Lyrics(mp3.ID3v2Tag());
            }
            if (m.lyrics.empty()) m.lyrics = readExternalLrc(filePath);
            return m;
        }
    }
    // FLAC
    {
        TagLib::FLAC::File flac(fn);
        if (flac.isValid()) {
            m.coverBytes = readFlacPicture(&flac);
            if (flac.xiphComment()) m.lyrics = readXiphLyrics(flac.xiphComment());
            if (m.lyrics.empty()) m.lyrics = readExternalLrc(filePath);
            return m;
        }
    }
    // Ogg Vorbis
    {
        TagLib::Ogg::Vorbis::File ogg(fn);
        if (ogg.isValid()) {
            if (ogg.tag()) {
                m.lyrics = readXiphLyrics(ogg.tag());
                // OGG 内嵌封面暂不支持（与原项目一致）
            }
            if (m.lyrics.empty()) m.lyrics = readExternalLrc(filePath);
            return m;
        }
    }
    // MP4/AAC
    {
        TagLib::MP4::File mp4(fn);
        if (mp4.isValid() && mp4.tag()) {
            auto cov = mp4.tag()->item("covr");
            if (cov.isValid()) {
                auto data = cov.toCoverArtList();
                if (!data.isEmpty()) {
                    auto& pic = data.front();
                    auto& bytes = pic.data();
                    m.coverBytes.assign(bytes.data(), bytes.data() + bytes.size());
                }
            }
            if (m.lyrics.empty()) m.lyrics = readExternalLrc(filePath);
            return m;
        }
    }
    // Opus
    {
        TagLib::Ogg::Opus::File opus(fn);
        if (opus.isValid()) {
            if (opus.tag()) m.lyrics = readXiphLyrics(opus.tag());
            if (m.lyrics.empty()) m.lyrics = readExternalLrc(filePath);
            return m;
        }
    }

    m.lyrics = readExternalLrc(filePath);
    return m;
}

AudioMetadata AudioMetadataReader::readTagOnly(const std::string& filePath) {
    AudioMetadata m;
    TagLib::FileRef f(toFileName(filePath));
    if (!f.isNull() && f.tag()) {
        auto* tag = f.tag();
        m.title  = tag->title().to8Bit(true);
        m.artist = tag->artist().to8Bit(true);
        m.album  = tag->album().to8Bit(true);
    }
    if (m.title.empty()) m.title = titleFromFilename(filePath);
    return m;
}

std::vector<uint8_t> AudioMetadataReader::readPicture(const std::string& filePath) {
    auto fn = toFileName(filePath);
    // MP3
    {
        TagLib::MPEG::File mp3(fn);
        if (mp3.isValid() && mp3.ID3v2Tag()) {
            return readId3v2Picture(mp3.ID3v2Tag());
        }
    }
    // FLAC
    {
        TagLib::FLAC::File flac(fn);
        if (flac.isValid()) return readFlacPicture(&flac);
    }
    // MP4
    {
        TagLib::MP4::File mp4(fn);
        if (mp4.isValid() && mp4.tag()) {
            auto cov = mp4.tag()->item("covr");
            if (cov.isValid()) {
                auto data = cov.toCoverArtList();
                if (!data.isEmpty()) {
                    auto& pic = data.front();
                    auto& bytes = pic.data();
                    return std::vector<uint8_t>(bytes.data(), bytes.data() + bytes.size());
                }
            }
        }
    }
    return {};
}

std::string AudioMetadataReader::readLyrics(const std::string& filePath) {
    auto fn = toFileName(filePath);
    // MP3
    {
        TagLib::MPEG::File mp3(fn);
        if (mp3.isValid() && mp3.ID3v2Tag()) {
            std::string l = readId3v2Lyrics(mp3.ID3v2Tag());
            if (!l.empty()) return l;
        }
    }
    // FLAC
    {
        TagLib::FLAC::File flac(fn);
        if (flac.isValid() && flac.xiphComment()) {
            std::string l = readXiphLyrics(flac.xiphComment());
            if (!l.empty()) return l;
        }
    }
    // Ogg
    {
        TagLib::Ogg::Vorbis::File ogg(fn);
        if (ogg.isValid() && ogg.tag()) {
            std::string l = readXiphLyrics(ogg.tag());
            if (!l.empty()) return l;
        }
    }
    return readExternalLrc(filePath);
}

}  // namespace meplayer
