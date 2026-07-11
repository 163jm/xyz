#include "core/cover_cache.h"
#include "core/audio_metadata.h"
#include <thread>

namespace meplayer {

CoverCache& CoverCache::instance() {
    static CoverCache inst;
    return inst;
}

CoverCache::CoverCache() {}

std::vector<uint8_t> CoverCache::get(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = cache_.find(path);
    if (it == cache_.end()) return {};
    return it->second;
}

bool CoverCache::has(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    return cache_.find(path) != cache_.end();
}

std::vector<uint8_t> CoverCache::getOrLoad(const std::string& path) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = cache_.find(path);
        if (it != cache_.end()) return it->second;
    }
    auto bytes = AudioMetadataReader::readPicture(path);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cache_[path] = bytes;
    }
    return bytes;
}

void CoverCache::loadBatch(const std::vector<std::string>& paths,
                            std::function<void(const std::string&)> onDone) {
    std::thread([this, paths, onDone]() {
        for (auto& p : paths) {
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (cache_.find(p) != cache_.end()) {
                    if (onDone) onDone(p);
                    continue;
                }
            }
            auto bytes = AudioMetadataReader::readPicture(p);
            {
                std::lock_guard<std::mutex> lk(mtx_);
                cache_[p] = bytes;
            }
            if (onDone) onDone(p);
        }
    }).detach();
}

void CoverCache::ensureLoaded(const std::vector<std::string>& sortedPaths,
                                std::atomic<int>& upTo,
                                int batchSize,
                                std::function<void()> onBatchDone) {
    int start = upTo.load();
    int end = std::min(start + batchSize, static_cast<int>(sortedPaths.size()));
    if (start >= end) {
        if (onBatchDone) onBatchDone();
        return;
    }
    std::vector<std::string> batch(sortedPaths.begin() + start,
                                   sortedPaths.begin() + end);
    upTo.store(end);
    loadBatch(batch, nullptr);
    if (onBatchDone) {
        std::thread([onBatchDone]() { onBatchDone(); }).detach();
    }
}

void CoverCache::loadNextBatch(const std::vector<std::string>& sortedPaths,
                                 std::atomic<int>& upTo,
                                 int batchSize,
                                 std::function<void()> onBatchDone) {
    ensureLoaded(sortedPaths, upTo, batchSize, onBatchDone);
}

void CoverCache::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    cache_.clear();
}

}  // namespace meplayer
