#include "util/string_util.h"
#include <algorithm>
#include <sstream>
#include <cstdarg>
#include <cstdio>
#include <windows.h>

namespace meplayer {

std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()), &wide[0], n);
    return wide;
}

std::string wide_to_utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                static_cast<int>(wide.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string utf8(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                        static_cast<int>(wide.size()), &utf8[0], n,
                        nullptr, nullptr);
    return utf8;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) out.push_back(item);
    return out;
}

std::vector<std::wstring> wsplit(const std::wstring& s, wchar_t delim) {
    std::vector<std::wstring> out;
    std::wstringstream ss(s);
    std::wstring item;
    while (std::getline(ss, item, delim)) out.push_back(item);
    return out;
}

std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring wtrim(const std::wstring& s) {
    auto a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    auto b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring base_name(const std::wstring& path) {
    auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring dir_name(const std::wstring& path) {
    auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"" : path.substr(0, pos);
}

std::wstring ext_of(const std::wstring& path) {
    auto pos = path.find_last_of(L'.');
    if (pos == std::wstring::npos) return L"";
    std::wstring e = path.substr(pos);
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    return e;
}

std::wstring no_ext(const std::wstring& path) {
    auto pos = path.find_last_of(L'.');
    auto sl = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos || (sl != std::wstring::npos && pos < sl))
        return path;
    return path.substr(0, pos);
}

bool has_ext(const std::wstring& path, const std::vector<std::wstring>& exts) {
    auto e = ext_of(path);
    for (auto& x : exts) if (x == e) return true;
    return false;
}

std::string format_time(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = static_cast<int>(seconds);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    char buf[16];
    if (h > 0) snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else       snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}

}  // namespace meplayer
