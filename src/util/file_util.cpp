#include "util/file_util.h"
#include "util/string_util.h"
#include <windows.h>
#include <chrono>
#include <fstream>

namespace meplayer {

bool file_exists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dir_exists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool create_dirs(const std::wstring& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

std::vector<ScannedFile> scan_dir(const std::wstring& root,
                                  const std::vector<std::wstring>& exts,
                                  int max_depth) {
    std::vector<ScannedFile> out;
    std::error_code ec;
    for (auto& p : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (p.depth() > max_depth) continue;
        if (!p.is_regular_file()) continue;
        auto& path = p.path();
        // 跳过隐藏目录
        bool hidden = false;
        for (auto& seg : path) {
            std::wstring s = seg.wstring();
            if (!s.empty() && s[0] == L'.') { hidden = true; break; }
        }
        if (hidden) continue;
        if (!has_ext(path.wstring(), exts)) continue;
        ScannedFile f;
        f.path = path.wstring();
        f.name = path.filename().wstring();
        std::error_code ec2;
        f.size = static_cast<uint64_t>(fs::file_size(path, ec2));
        auto t = fs::last_write_time(path, ec2);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        f.modifiedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            sctp.time_since_epoch()).count();
        out.push_back(std::move(f));
    }
    return out;
}

std::wstring exe_dir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring exe = buf;
    return dir_name(exe);
}

std::wstring join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    wchar_t last = a.back();
    if (last == L'\\' || last == L'/') return a + b;
    return a + L"\\" + b;
}

std::vector<uint8_t> read_all_bytes(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> buf(sz);
    if (sz) f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

}  // namespace meplayer
