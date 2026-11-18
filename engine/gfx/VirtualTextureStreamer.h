// AIVERSE portable physical virtual-texture page streaming coordinator.
// Composes the existing CPU page cache, RGBA8Array page table, and owned
// frame-budgeted GPU upload queue. No GPU feedback or disk IO worker here.
#pragma once

#include "TextureStreaming.h"
#include "VirtualTexturePageTable.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::gfx {

struct VirtualTextureStreamConfig {
    int tilesX = 0, tilesY = 0;
    int mipLevels = 0, virtualLayers = 1;
    int physicalPages = 0;
    int physicalPageSize = 128; // including caller-provided edge gutters
    PixelFormat physicalFormat = PixelFormat::RGBA8;
    size_t pageTableBytesPerFlush = size_t(4) << 20;
};

// Memory bounded by physicalPages * pageSize^2 * bytesPerPixel, NOT
// by the logical virtual texture's full virtual resolution.
class VirtualTextureStreamer {
public:
    using CompletionFn = std::function<void(VirtualPageKey key, bool ok)>;
    explicit VirtualTextureStreamer(Device& device,
                                    TextureStreamConfig budget = {})
        : device_(&device), uploads_(device,budget) {}
    ~VirtualTextureStreamer() { shutdown(); }
    VirtualTextureStreamer(const VirtualTextureStreamer&) = delete;
    VirtualTextureStreamer& operator=(const VirtualTextureStreamer&) = delete;

    bool initialize(VirtualTextureStreamConfig cfg) {
        shutdown();
        if (!device_ || cfg.physicalPageSize < 4 || cfg.physicalPages < 1 ||
            cfg.physicalPages > 65535 || cfg.pageTableBytesPerFlush < 4 ||
            (cfg.physicalFormat != PixelFormat::RGBA8 &&
             cfg.physicalFormat != PixelFormat::RGBA8_sRGB) ||
            !device_->supports(cfg.physicalFormat) ||
            !device_->supportsTextureDimension(TextureDimension::Texture2DArray)) {
            error_ = "virtual texture needs supported RGBA8 array resources";
            return false;
        }
        if (!pages_.initialize(cfg.tilesX,cfg.tilesY,cfg.mipLevels,
                               cfg.virtualLayers,cfg.physicalPages) ||
            !table_.initialize(cfg.tilesX,cfg.tilesY,cfg.mipLevels,
                               cfg.virtualLayers)) {
            pages_.clear();
            table_.clear();
            error_ = "virtual texture page dimensions or metadata budget invalid";
            return false;
        }
        const TextureDesc atlasDesc =
            pages_.atlasDescriptor(cfg.physicalPageSize,cfg.physicalFormat);
        atlas_ = uploads_.createTexture(atlasDesc);
        if (!atlas_.valid()) {
            error_ = uploads_.lastError();
            pages_.clear(); table_.clear();
            return false;
        }
        tableGpu_ = table_.createGpuTexture(*device_);
        if (!tableGpu_.valid()) {
            error_ = device_->lastError();
            if (error_.empty()) error_ = "virtual texture page table GPU allocation failed";
            uploads_.destroy(atlas_); atlas_ = {};
            pages_.clear(); table_.clear();
            return false;
        }
        config_ = cfg;
        failed_ = false;
        error_.clear();
        return true;
    }

    // Input contains one complete, tightly packed *physical* page image.
    // Page gutters/border texels are supplied by the asset system; no
    // automatic gutter expansion, downsampling or transcoding happens here.
    // A pending or already resident key is not scheduled twice.
    TextureUploadTicket stage(VirtualPageKey key, const void* packed,
                              size_t bytes, bool pin = false) {
        if (!ready() || !packed) {
            error_ = "virtual texture not ready or source pixels absent";
            return {};
        }
        const size_t size = size_t(config_.physicalPageSize);
        const size_t expected = size*size*4u;
        if (bytes != expected || expected > size_t(std::numeric_limits<int>::max())) {
            error_ = "virtual texture page payload size does not match physical atlas";
            return {};
        }
        const VirtualPageAllocation allocation = pages_.reserve(key,pin);
        if (!allocation.valid() || !allocation.needsUpload) {
            error_ = "virtual texture page invalid, pending, resident or all slots pinned";
            return {};
        }
        // A reserve may evict an LRU physical slot. Invalidate the stale
        // shader mapping BEFORE the slot can be overwritten by the new upload.
        if (!synchronizeTable()) {
            failed_ = true; // never expose aliasing/corrupt mapping on the GPU
            return {};
        }
        TextureUploadDesc update;
        update.mipLevel = 0;
        update.arrayLayer = allocation.physicalSlice;
        update.width = config_.physicalPageSize;
        update.height = config_.physicalPageSize;
        update.rowPitchBytes = int(size*4u);
        update.slicePitchBytes = int(expected);
        const TextureUploadTicket ticket =
            uploads_.enqueue(atlas_,update,packed,bytes);
        if (!ticket.valid()) {
            pages_.abandon(key,allocation);
            if (!synchronizeTable()) failed_ = true;
            error_ = uploads_.lastError();
            return {};
        }
        pending_.emplace(ticket.value, Pending{key,allocation});
        error_.clear();
        return ticket;
    }

    // Returns submitted GPU transfers, not completed disk operations.
    // Publish pages in the shader table only after the GPU upload succeeded.
    size_t drain(const CompletionFn& onCompletion = {}) {
        std::vector<std::pair<VirtualPageKey,bool>> events;
        const size_t submitted = uploads_.drain(
            [&](TextureUploadTicket ticket,TextureStreamId id,bool success) {
                auto it = pending_.find(ticket.value);
                if (it==pending_.end() || id!=atlas_) return;
                Pending context=it->second;
                pending_.erase(it);
                if (!pages_.complete(context.key,context.allocation,success)) return;
                events.emplace_back(context.key,success);
                if (!success) failed_=true; // entire atlas quarantined by queue
            });
        if (!failed_ && !synchronizeTable()) failed_=true;
        if (uploads_.failed(atlas_)) failed_=true;
        if (failed_) pending_.clear(); // underlying queue cancelled further work
        if (onCompletion)
            for (const auto& item:events)
                onCompletion(item.first,item.second && !failed_);
        return submitted;
    }

    bool cancel(TextureUploadTicket ticket) {
        auto it=pending_.find(ticket.value);
        if (it==pending_.end() || !uploads_.cancel(ticket)) return false;
        const Pending context=it->second;
        pending_.erase(it);
        pages_.abandon(context.key,context.allocation);
        if (!synchronizeTable()) failed_=true;
        return true;
    }

    // Does not reclaim GPU memory per page; whole physical atlas has fixed
    // storage. LRU reuses slices. Explicit eviction makes a key invalid in
    // the GPU page table before that slice becomes reusable.
    bool evict(VirtualPageKey key) {
        if (!ready() || !pages_.evict(key)) return false;
        if (!synchronizeTable()) {
            failed_=true;
            return false;
        }
        return true;
    }

    bool pin(VirtualPageKey key,bool pinned=true) {
        return pages_.pin(key,pinned);
    }
    bool touch(VirtualPageKey key) { return pages_.touch(key); }
    VirtualPageAllocation resolve(VirtualPageKey key) const {
        return ready()?pages_.resolveWithCoarseFallback(key):VirtualPageAllocation{};
    }
    // Exact resident page lookup without the coarser fallback. The
    // disk-backed coordinator must still request missing detail pages,
    // even when an ancestor makes the virtual sample temporarily valid.
    VirtualPageAllocation resolveExact(VirtualPageKey key) const {
        return ready()?pages_.resolve(key):VirtualPageAllocation{};
    }
    std::array<uint8_t,4> encodedPageTable(int x,int y,int layer) const {
        return table_.entry(x,y,layer);
    }
    TextureHandle physicalAtlas() const {
        return ready()?uploads_.texture(atlas_):TextureHandle{};
    }
    TextureHandle pageTableTexture() const {
        return ready()?tableGpu_:TextureHandle{};
    }
    VirtualPageStats pageStats() const { return pages_.stats(); }
    TextureStreamStats streamStats() const { return uploads_.stats(); }
    bool ready() const {
        return atlas_.valid() && tableGpu_.valid() && !failed_ &&
               !uploads_.failed(atlas_);
    }
    const std::string& lastError() const {return error_;}

    void shutdown() {
        pending_.clear();
        if (atlas_.valid()) uploads_.destroy(atlas_);
        atlas_ = {};
        if (tableGpu_.valid() && device_) device_->destroyTexture(tableGpu_);
        tableGpu_ = {};
        pages_.clear(); table_.clear();
        config_ = {};
        failed_ = false;
    }

private:
    struct Pending {
        VirtualPageKey key;
        VirtualPageAllocation allocation;
    };
    bool synchronizeTable() {
        table_.rebuild(pages_);
        // A stale table entry must never point to a physical slot about to
        // be overwritten. Flush dirty rows fully before allowing a reuse.
        const size_t maxIterations = table_.pendingRows();
        for (size_t iteration=0;iteration<maxIterations &&
                                table_.pendingRows();++iteration) {
            const size_t previous=table_.pendingRows();
            const size_t done=table_.flush(*device_,tableGpu_,
                                          config_.pageTableBytesPerFlush);
            if (!done || table_.pendingRows()>=previous) {
                error_=table_.lastError();
                if (error_.empty()) error_="virtual texture GPU page-table write failed";
                return false;
            }
        }
        if (table_.pendingRows()) {
            error_="virtual texture page-table synchronization incomplete";
            return false;
        }
        return true;
    }
    Device* device_ = nullptr;
    TextureStreamQueue uploads_;
    VirtualTexturePageCache pages_;
    VirtualTexturePageTable table_;
    VirtualTextureStreamConfig config_;
    TextureStreamId atlas_;
    TextureHandle tableGpu_;
    bool failed_ = false;
    std::map<uint64_t,Pending> pending_;
    std::string error_;
};

} // namespace aiverse::gfx
