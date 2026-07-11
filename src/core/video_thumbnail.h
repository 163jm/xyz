// 视频缩略图生成（Media Foundation IMFSourceReader）
// 对应原项目 fc_native_video_thumbnail：启用 AVDecVideoThumbnailGenerationMode 加速
// 缩略图落盘到 appdata/thumbnails/<FNV-1a>.jpg（与原项目策略一致）
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>

namespace meplayer {

class VideoThumbnail {
public:
    static VideoThumbnail& instance();

    // 生成单个视频缩略图，返回相对 appdata 的路径（失败返回空）
    //   videoPath: UTF-8
    //   size: 缩略图边长（正方形，默认 160）
    std::string generate(const std::string& videoPath, int size = 160);

    // 队列生成（最多 2 并发），用于扫描后批量补缩略图
    using DoneCb = std::function<void(const std::string& videoPath, const std::string& thumbPath)>;
    void enqueue(const std::string& videoPath, DoneCb cb);
    void waitAll();

    void shutdown();

private:
    VideoThumbnail();
    void worker();

    struct Task { std::string path; DoneCb cb; };
    std::queue<Task>     queue_;
    std::mutex           mtx_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::atomic<bool>    running_{false};
    std::atomic<int>     active_{0};
};

}  // namespace meplayer
