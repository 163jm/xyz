#include "core/m3u_parser.h"
#include <sstream>
#include <algorithm>
#include <regex>

namespace meplayer {

// 从 #EXTINF 行提取属性
static void parseExtInf(const std::string& line, M3uChannel& ch) {
    // 格式: #EXTINF:-1 tvg-name="x" tvg-id="y" group-title="g" tvg-logo="l",显示名
    ch.name = "";
    ch.group = "";
    ch.logo = "";
    ch.id = "";

    auto comma = line.find(',');
    std::string attrPart = (comma != std::string::npos) ? line.substr(0, comma) : line;
    std::string namePart = (comma != std::string::npos) ? line.substr(comma + 1) : "";

    // tvg-name
    std::smatch m;
    if (std::regex_search(attrPart, m, std::regex("tvg-name=\"([^\"]*)\""))) {
        ch.name = m[1].str();
    }
    if (std::regex_search(attrPart, m, std::regex("tvg-id=\"([^\"]*)\""))) {
        ch.id = m[1].str();
    }
    if (std::regex_search(attrPart, m, std::regex("group-title=\"([^\"]*)\""))) {
        ch.group = m[1].str();
    }
    if (std::regex_search(attrPart, m, std::regex("tvg-logo=\"([^\"]*)\""))) {
        ch.logo = m[1].str();
    }
    // 名称优先用逗号后的显示名，其次 tvg-name
    if (!namePart.empty()) {
        // 去前后空白
        auto a = namePart.find_first_not_of(" \t\r\n");
        auto b = namePart.find_last_not_of(" \t\r\n");
        if (a != std::string::npos) ch.name = namePart.substr(a, b - a + 1);
    }
}

std::vector<M3uChannel> M3uParser::parse(const std::string& content) {
    std::vector<M3uChannel> out;
    std::istringstream ss(content);
    std::string line;
    M3uChannel pending;
    bool hasPending = false;

    while (std::getline(ss, line)) {
        // 去行尾 \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // 去前导空白
        auto a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        std::string trimmed = line.substr(a);

        if (trimmed[0] == '#') {
            if (trimmed.rfind("#EXTINF", 0) == 0) {
                pending = M3uChannel();
                parseExtInf(trimmed.substr(7), pending);  // 跳过 "#EXTINF"
                hasPending = true;
            }
            // 其它 #EXT 行忽略
            continue;
        }

        // URL 行
        if (hasPending) {
            pending.url = trimmed;
            if (pending.name.empty()) pending.name = trimmed;
            out.push_back(pending);
            pending = M3uChannel();
            hasPending = false;
        } else {
            // 纯 URL 行（无 #EXTINF）
            M3uChannel ch;
            ch.url = trimmed;
            ch.name = trimmed;
            ch.group = "";
            out.push_back(ch);
        }
    }
    return out;
}

std::map<std::string, std::vector<M3uChannel>> M3uParser::groupBy(
    const std::vector<M3uChannel>& channels) {
    std::map<std::string, std::vector<M3uChannel>> out;
    for (auto& ch : channels) {
        std::string g = ch.group.empty() ? "未分组" : ch.group;
        out[g].push_back(ch);
    }
    return out;
}

std::vector<std::string> M3uParser::groupNames(
    const std::vector<M3uChannel>& channels) {
    std::vector<std::string> out;
    std::vector<std::string> seen;
    for (auto& ch : channels) {
        std::string g = ch.group.empty() ? "未分组" : ch.group;
        if (std::find(seen.begin(), seen.end(), g) == seen.end()) {
            seen.push_back(g);
            out.push_back(g);
        }
    }
    return out;
}

}  // namespace meplayer
