// 字符串/编码工具
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace meplayer {

// UTF-16 ↔ UTF-8 转换（Win32 API 多用 wchar，业务层用 UTF-8）
std::wstring utf8_to_wide(const std::string& utf8);
std::string  wide_to_utf8(const std::wstring& wide);

// 分隔
std::vector<std::string> split(const std::string& s, char delim);
std::vector<std::wstring> wsplit(const std::wstring& s, wchar_t delim);

// 去空白
std::string trim(const std::string& s);
std::wstring wtrim(const std::wstring& s);

// 路径工具
std::wstring base_name(const std::wstring& path);
std::wstring dir_name(const std::wstring& path);
std::wstring ext_of(const std::wstring& path);          // 含点，小写，如 ".mp3"
std::wstring no_ext(const std::wstring& path);          // 不含扩展名的路径
bool        has_ext(const std::wstring& path, const std::vector<std::wstring>& exts);

// 格式化
std::string format_time(double seconds);                 // "mm:ss" 或 "h:mm:ss"

// 调用方普遍以 util:: 前缀引用本文件中的自由函数（历史遗留），
// 这里提供命名空间别名以保持源码兼容，避免逐处修改调用点。
namespace util {
using meplayer::utf8_to_wide;
using meplayer::wide_to_utf8;
using meplayer::split;
using meplayer::wsplit;
using meplayer::trim;
using meplayer::wtrim;
using meplayer::base_name;
using meplayer::dir_name;
using meplayer::ext_of;
using meplayer::no_ext;
using meplayer::has_ext;
using meplayer::format_time;
}  // namespace util

}  // namespace meplayer
