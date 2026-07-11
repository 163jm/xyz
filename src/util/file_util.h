// 文件系统工具
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

namespace meplayer {

namespace fs = std::filesystem;

bool file_exists(const std::wstring& path);
bool dir_exists(const std::wstring& path);
bool create_dirs(const std::wstring& path);

// 递归扫描目录下指定扩展名的文件（按修改时间或名称排序）
struct ScannedFile {
    std::wstring path;
    std::wstring name;       // 含扩展名
    uint64_t     size;       // 字节
    int64_t      modifiedMs; // Unix 毫秒
};

std::vector<ScannedFile> scan_dir(const std::wstring& root,
                                  const std::vector<std::wstring>& exts,
                                  int max_depth = 8);

// exe 所在目录
std::wstring exe_dir();

// 拼接路径
std::wstring join(const std::wstring& a, const std::wstring& b);

// 读取整个文件为字节
std::vector<uint8_t> read_all_bytes(const std::wstring& path);

}  // namespace meplayer
