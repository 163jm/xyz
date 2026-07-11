// M3U 解析器（移植自原项目 m3u_parser.dart）
#pragma once
#include <string>
#include <vector>
#include <map>

namespace meplayer {

struct M3uChannel {
    std::string name;    // 频道名
    std::string url;
    std::string group;   // 分组
    std::string logo;
    std::string id;
};

class M3uParser {
public:
    // 解析 M3U 内容（支持 #EXTM3U/#EXTINF 扩展格式和纯 URL 格式）
    static std::vector<M3uChannel> parse(const std::string& content);

    // 按分组组织（保持插入顺序）
    static std::map<std::string, std::vector<M3uChannel>> groupBy(
        const std::vector<M3uChannel>& channels);

    // 分组列表（保持插入顺序）
    static std::vector<std::string> groupNames(
        const std::vector<M3uChannel>& channels);
};

}  // namespace meplayer
