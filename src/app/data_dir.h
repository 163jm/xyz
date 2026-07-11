// 应用数据目录管理：exe 同级 appdata/
//   appdata/meplayer.db        SQLite 数据库
//   appdata/thumbnails/        视频缩略图缓存（落盘）
//   appdata/covers/            音乐封面不落盘，此目录保留以兼容
// 音乐封面策略：纯内存，不落盘
#pragma once
#include <string>

namespace meplayer {

class AppDataDir {
public:
    static AppDataDir& instance();

    // exe 同级 appdata/
    const std::wstring& root() const { return root_; }
    const std::wstring& thumbsDir() const { return thumbs_; }
    const std::wstring& coversDir() const { return covers_; }
    const std::wstring& dbPath() const { return db_path_; }

    void ensureCreated();

    // 相对/绝对路径互转：appdata 内文件存相对路径，保证程序文件夹可移动
    std::wstring toRelative(const std::wstring& absolute) const;
    std::wstring toAbsolute(const std::wstring& stored) const;

private:
    AppDataDir();
    std::wstring root_;
    std::wstring thumbs_;
    std::wstring covers_;
    std::wstring db_path_;
};

}  // namespace meplayer
