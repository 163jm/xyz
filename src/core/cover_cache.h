// 内存封面缓存（音乐封面不落盘，按需加载到内存）
// 策略：全局 path→bytes 缓存，列表显示前 200 首 + 滚动增量 200
//   切换分类/排序 → 重新计算前 200，命中缓存直接用
//   播放页独立 readFile，结果回填缓存
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>

namespace meplayer {

class CoverCache {
public:
    static CoverCache& instance();

    // 查询封面（命中返回，未命中返回空）
    std::vector<uint8_t> get(const std::string& path);

    // 是否已加载（在缓存中）
    bool has(const std::string& path);

    // 批量加载封面到缓存（后台线程）
    //   paths: 要加载的文件路径列表
    //   onDone: 每个加载完成回调（在加载线程调用）
    void loadBatch(const std::vector<std::string>& paths,
                   std::function<void(const std::string&)> onDone = nullptr);

    // 为当前可视列表的前 N 首预加载封面
    //   sortedPaths: 当前排序后的完整列表
    //   upTo: 已加载到第几个（输出）
    //   batchSize: 一批 200
    void ensureLoaded(const std::vector<std::string>& sortedPaths,
                      std::atomic<int>& upTo,
                      int batchSize = 200,
                      std::function<void()> onBatchDone = nullptr);

    // 滚动增量：加载下一批 200
    void loadNextBatch(const std::vector<std::string>& sortedPaths,
                       std::atomic<int>& upTo,
                       int batchSize = 200,
                       std::function<void()> onBatchDone = nullptr);

    // 清空缓存（切换库或退出时）
    void clear();

    // 单个读取并回填缓存（播放页用）
    std::vector<uint8_t> getOrLoad(const std::string& path);

private:
    CoverCache();
    std::unordered_map<std::string, std::vector<uint8_t>> cache_;
    std::mutex mtx_;
};

}  // namespace meplayer
