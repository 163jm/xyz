#include "app/data_dir.h"
#include "util/file_util.h"
#include "util/string_util.h"
#include <windows.h>

namespace meplayer {

AppDataDir& AppDataDir::instance() {
    static AppDataDir inst;
    return inst;
}

AppDataDir::AppDataDir() {
    root_ = util::join(util::exe_dir(), L"appdata");
    thumbs_ = util::join(root_, L"thumbnails");
    covers_ = util::join(root_, L"covers");
    db_path_ = util::join(root_, L"meplayer.db");
}

void AppDataDir::ensureCreated() {
    util::create_dirs(root_);
    util::create_dirs(thumbs_);
    util::create_dirs(covers_);
}

std::wstring AppDataDir::toRelative(const std::wstring& absolute) const {
    // 兼容旧绝对路径：若不在 root_ 下，原样返回
    if (absolute.empty()) return absolute;
    std::wstring lower_root = root_;
    std::transform(lower_root.begin(), lower_root.end(), lower_root.begin(), ::towlower);
    std::wstring lower_abs = absolute;
    std::transform(lower_abs.begin(), lower_abs.end(), lower_abs.begin(), ::towlower);
    if (lower_abs.find(lower_root) == 0) {
        // 去掉 root_ 前缀和分隔符
        std::wstring rel = absolute.substr(root_.size());
        if (!rel.empty() && (rel[0] == L'\\' || rel[0] == L'/')) rel = rel.substr(1);
        return rel;
    }
    return absolute;
}

std::wstring AppDataDir::toAbsolute(const std::wstring& stored) const {
    if (stored.empty()) return stored;
    // 绝对路径原样返回（兼容旧数据）
    if (stored.size() >= 2 && stored[1] == L':') return stored;
    if (stored[0] == L'\\' || stored[0] == L'/') return stored;
    return util::join(root_, stored);
}

}  // namespace meplayer
