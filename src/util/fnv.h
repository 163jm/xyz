// FNV-1a 哈希，用于生成视频缩略图文件名（与原 Flutter 项目一致的算法）
#pragma once
#include <cstdint>
#include <string>

namespace meplayer {

// 64 位 FNV-1a，返回 16 位十六进制字符串（与原项目 _stableHash 一致：取低 32 位 → 8 位 hex）
std::string fnv1a_hex32(const std::string& s);

// 调用方普遍以 util:: 前缀引用本文件中的自由函数（历史遗留），
// 这里提供命名空间别名以保持源码兼容，避免逐处修改调用点。
namespace util {
using meplayer::fnv1a_hex32;
}  // namespace util

}  // namespace meplayer
