#include "util/fnv.h"

namespace meplayer {

std::string fnv1a_hex32(const std::string& s) {
    // 与原项目 _stableHash 一致：FNV-1a 64 位，取低 32 位，输出 8 位 hex
    uint64_t hash = 0xcbf29ce484222325ULL;
    const uint64_t kPrime = 0x100000001b3ULL;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= kPrime;
    }
    uint32_t low = static_cast<uint32_t>(hash);
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", low);
    return buf;
}

}  // namespace meplayer
