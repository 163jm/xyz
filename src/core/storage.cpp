#include "core/storage.h"
#include "util/string_util.h"
#include <ctime>

namespace meplayer {

Storage& Storage::instance() {
    static Storage inst;
    return inst;
}

Storage::Storage() {}

static int cb_string(void* data, int argc, char** argv, char** col) {
    auto* out = static_cast<std::string*>(data);
    if (argc > 0 && argv[0]) *out = argv[0];
    return 0;
}

bool Storage::init(const std::wstring& dbPath) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (db_) return true;
    int rc = sqlite3_open16(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) return false;
    createTables();
    return true;
}

void Storage::createTables() {
    const char* sqls[] = {
        "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS video_scan_paths (path TEXT PRIMARY KEY)",
        "CREATE TABLE IF NOT EXISTS music_scan_paths (path TEXT PRIMARY KEY)",
        "CREATE TABLE IF NOT EXISTS iptv_sources ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, url TEXT,"
        "  type TEXT, auto_update INTEGER, file_path TEXT)",
        "CREATE TABLE IF NOT EXISTS recent_files (path TEXT PRIMARY KEY, played_at INTEGER)",
        "CREATE TABLE IF NOT EXISTS video_thumbs (video_path TEXT PRIMARY KEY, thumb_path TEXT)",
    };
    for (auto s : sqls) {
        char* err = nullptr;
        sqlite3_exec(db_, s, nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
    }
}

std::string Storage::getString(const std::string& key, const std::string& def) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!db_) return def;
    const char* sql = "SELECT value FROM settings WHERE key=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return def;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::string out = def;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* v = sqlite3_column_text(stmt, 0);
        if (v) out = reinterpret_cast<const char*>(v);
    }
    sqlite3_finalize(stmt);
    return out;
}

void Storage::setString(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!db_) return;
    const char* sql = "INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

int Storage::getInt(const std::string& key, int def) const {
    std::string v = getString(key);
    if (v.empty()) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}

void Storage::setInt(const std::string& key, int value) {
    setString(key, std::to_string(value));
}

bool Storage::getBool(const std::string& key, bool def) const {
    return getInt(key, def ? 1 : 0) != 0;
}

void Storage::setBool(const std::string& key, bool value) {
    setInt(key, value ? 1 : 0);
}

// ---- 扫描路径 ----
static int cb_paths(void* data, int argc, char** argv, char** col) {
    auto* out = static_cast<std::vector<std::wstring>*>(data);
    if (argc > 0 && argv[0]) {
        out->push_back(util::utf8_to_wide(argv[0]));
    }
    return 0;
}

std::vector<std::wstring> Storage::getVideoScanPaths() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::wstring> out;
    if (!db_) return out;
    char* err = nullptr;
    sqlite3_exec(db_, "SELECT path FROM video_scan_paths ORDER BY rowid",
                 cb_paths, &out, &err);
    if (err) sqlite3_free(err);
    return out;
}

std::vector<std::wstring> Storage::getMusicScanPaths() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::wstring> out;
    if (!db_) return out;
    char* err = nullptr;
    sqlite3_exec(db_, "SELECT path FROM music_scan_paths ORDER BY rowid",
                 cb_paths, &out, &err);
    if (err) sqlite3_free(err);
    return out;
}

static void addPath(sqlite3* db, const char* table, const std::wstring& path) {
    std::string u8 = util::wide_to_utf8(path);
    std::string sql = std::string("INSERT OR IGNORE INTO ") + table + "(path) VALUES(?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, u8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void removePath(sqlite3* db, const char* table, const std::wstring& path) {
    std::string u8 = util::wide_to_utf8(path);
    std::string sql = std::string("DELETE FROM ") + table + " WHERE path=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, u8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Storage::addVideoScanPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (db_) addPath(db_, "video_scan_paths", path);
}

void Storage::addMusicScanPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (db_) addPath(db_, "music_scan_paths", path);
}

void Storage::removeVideoScanPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (db_) removePath(db_, "video_scan_paths", path);
}

void Storage::removeMusicScanPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (db_) removePath(db_, "music_scan_paths", path);
}

// ---- IPTV 源 ----
static int cb_iptv(void* data, int argc, char** argv, char** col) {
    auto* out = static_cast<std::vector<Storage::IptvSource>*>(data);
    Storage::IptvSource s;
    for (int i = 0; i < argc; i++) {
        std::string k = col[i];
        const char* v = argv[i] ? argv[i] : "";
        if      (k == "id")          s.id = std::atoi(v);
        else if (k == "name")        s.name = v;
        else if (k == "url")         s.url = v;
        else if (k == "type")        s.type = v;
        else if (k == "auto_update") s.autoUpdate = std::atoi(v) != 0;
        else if (k == "file_path")   s.filePath = v;
    }
    out->push_back(s);
    return 0;
}

std::vector<Storage::IptvSource> Storage::getIptvSources() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<IptvSource> out;
    if (!db_) return out;
    char* err = nullptr;
    sqlite3_exec(db_, "SELECT id,name,url,type,auto_update,file_path FROM iptv_sources ORDER BY id",
                 cb_iptv, &out, &err);
    if (err) sqlite3_free(err);
    return out;
}

int Storage::addIptvSource(const IptvSource& src) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!db_) return 0;
    const char* sql = "INSERT INTO iptv_sources(name,url,type,auto_update,file_path) VALUES(?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, src.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, src.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, src.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 4, src.autoUpdate ? 1 : 0);
    sqlite3_bind_text(stmt, 5, src.filePath.c_str(), -1, SQLITE_TRANSIENT);
    int id = 0;
    if (sqlite3_step(stmt) == SQLITE_DONE) id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    sqlite3_finalize(stmt);
    return id;
}

void Storage::updateIptvSource(const IptvSource& src) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!db_) return;
    const char* sql = "UPDATE iptv_sources SET name=?,url=?,type=?,auto_update=?,file_path=? WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, src.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, src.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, src.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 4, src.autoUpdate ? 1 : 0);
    sqlite3_bind_text(stmt, 5, src.filePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 6, src.id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Storage::removeIptvSource(int id) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!db_) return;
    const char* sql = "DELETE FROM iptv_sources WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ---- 最近播放 ----
static int cb_strings(void* data, int argc, char** argv, char** col) {
    auto* out = static_cast<std::vector<std::string>*>(data);
    if (argc > 0 && argv[0]) out->push_back(argv[0]);
    return 0;
}

std::vector<std::string> Storage::getRecentFiles() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::string> out;
    if (!db_) return out;
    char* err = nullptr;
    sqlite3_exec(db_, "SELECT path FROM recent_files ORDER BY played_at DESC LIMIT 50",
                 cb_strings, &out, &err);
    if (err) sqlite3_free(err);
    return out;
}

void Storage::addRecentFile(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!db_) return;
    const char* sql = "INSERT OR REPLACE INTO recent_files(path,played_at) VALUES(?,?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(std::time(nullptr)));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    // 保留最近 50
    sqlite3_exec(db_, "DELETE FROM recent_files WHERE path NOT IN "
                 "(SELECT path FROM recent_files ORDER BY played_at DESC LIMIT 50)",
                 nullptr, nullptr, nullptr);
}

// ---- 视频缩略图缓存 ----
static int cb_thumb(void* data, int argc, char** argv, char** col) {
    auto* out = static_cast<std::map<std::string,std::string>*>(data);
    std::string k, v;
    for (int i = 0; i < argc; i++) {
        std::string c = col[i];
        const char* val = argv[i] ? argv[i] : "";
        if (c == "video_path") k = val;
        else if (c == "thumb_path") v = val;
    }
    (*out)[k] = v;
    return 0;
}

std::map<std::string, std::string> Storage::getVideoThumbCache() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::map<std::string, std::string> out;
    if (!db_) return out;
    char* err = nullptr;
    sqlite3_exec(db_, "SELECT video_path,thumb_path FROM video_thumbs",
                 cb_thumb, &out, &err);
    if (err) sqlite3_free(err);
    return out;
}

void Storage::setVideoThumb(const std::string& videoPath, const std::string& thumbPath) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!db_) return;
    const char* sql = "INSERT OR REPLACE INTO video_thumbs(video_path,thumb_path) VALUES(?,?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, videoPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, thumbPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

}  // namespace meplayer
