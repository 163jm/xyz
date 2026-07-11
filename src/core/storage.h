// SQLite 持久化封装
#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <sqlite3.h>

namespace meplayer {

class Storage {
public:
    static Storage& instance();

    bool init(const std::wstring& dbPath);

    // ---- 键值设置 ----
    std::string getString(const std::string& key, const std::string& def = "") const;
    void        setString(const std::string& key, const std::string& value);
    int         getInt(const std::string& key, int def = 0) const;
    void        setInt(const std::string& key, int value);
    bool        getBool(const std::string& key, bool def = false) const;
    void        setBool(const std::string& key, bool value);

    // ---- 扫描路径 ----
    std::vector<std::wstring> getVideoScanPaths();
    std::vector<std::wstring> getMusicScanPaths();
    void addVideoScanPath(const std::wstring& path);
    void addMusicScanPath(const std::wstring& path);
    void removeVideoScanPath(const std::wstring& path);
    void removeMusicScanPath(const std::wstring& path);

    // ---- IPTV 源 ----
    struct IptvSource {
        int         id = 0;
        std::string name;
        std::string url;
        std::string type;        // "remote" | "file"
        bool        autoUpdate = false;
        std::string filePath;
    };
    std::vector<IptvSource> getIptvSources();
    int  addIptvSource(const IptvSource& src);
    void updateIptvSource(const IptvSource& src);
    void removeIptvSource(int id);

    // ---- 最近播放（≤50）----
    std::vector<std::string> getRecentFiles();
    void addRecentFile(const std::string& path);

    // ---- 视频缩略图缓存 ----
    std::map<std::string, std::string> getVideoThumbCache();
    void setVideoThumb(const std::string& videoPath, const std::string& thumbPath);

private:
    Storage();
    void createTables();
    sqlite3* db_ = nullptr;
    mutable std::mutex mtx_;
};

}  // namespace meplayer
