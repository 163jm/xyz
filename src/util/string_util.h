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

}  // namespace meplayer
