#pragma once
#include "Model.h"
#include <map>
#include <atomic>
#include <limits>
#include <mutex>
namespace aiverse::render {
// Per-content session. CPU access is synchronized; GPU methods belong to the render thread.
// The owning view shuts this down after releasing all models/native consumers.
class ModelTextureCache : public std::enable_shared_from_this<ModelTextureCache> {
public:
    bool read(const std::string& path, const FileReader& reader, Image& out);
    void upload(gfx::Device& dev, Image& image);
    void shutdown(gfx::Device& dev);
    void trim(gfx::Device& dev);
    // Render-thread source revision. Discards only its CPU decode; a new
    // upload receives a versioned GPU cache key while existing image leases
    // keep old textures alive until every borrower releases them.
    void invalidateSource(const std::string& path);
    size_t decodedCount() const { return decodedCount_.load(); }
    size_t gpuCount() const { return gpu_.size(); } // render thread only
    struct GpuOwnerEstimate {
        size_t uniqueTextures=0,compressedTextures=0;
        uint64_t rgba8BaseMipEquivalentBytes=0;
    };
    // Each shared GPU texture cache owner is counted ONCE by its unique
    // sampler+revision key, never once per Model/Image borrower. This is an
    // RGBA8 base-mip equivalent, not driver bytes (compressed formats,
    // mipmaps, padding and backend allocation can differ).
    GpuOwnerEstimate gpuOwnerEstimate() const {
        GpuOwnerEstimate result;
        for(const auto& [key,image]:gpu_) {
            (void)key;
            if(!image.tex.valid() || image.width<=0 || image.height<=0)
                continue;
            ++result.uniqueTextures;
            if(image.gpuCompressed)++result.compressedTextures;
            const auto width=uint64_t(image.width);
            const auto height=uint64_t(image.height);
            const auto max=std::numeric_limits<uint64_t>::max();
            const auto bytes=height>max/width/4u?max:width*height*4u;
            result.rgba8BaseMipEquivalentBytes=
                bytes>max-result.rgba8BaseMipEquivalentBytes?max:
                result.rgba8BaseMipEquivalentBytes+bytes;
        }
        return result;
    }
    // Actual decoded pixel cache allocation (not source file size / GPU textures).
    size_t decodedBytes() const { std::lock_guard lock(mutex_); return bytes_; }
private:
    struct Decoded { Image image; size_t bytes = 0; uint64_t used = 0; };
    mutable std::mutex mutex_;
    std::map<std::string, Decoded> decoded_;
    std::map<std::string, Image> gpu_;
    // Render thread only: upload() and invalidateSource() use this map.
    std::map<std::string, uint64_t> sourceRevisions_;
    size_t bytes_ = 0;
    std::atomic<size_t> decodedCount_{0};
    uint64_t clock_ = 0;
    static constexpr size_t kCpuBudget = 64u * 1024u * 1024u;
};
}
