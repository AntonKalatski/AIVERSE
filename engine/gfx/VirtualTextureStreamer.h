// AIVERSE portable physical virtual-texture page streaming coordinator.
// Composes the existing CPU page cache, RGBA8Array page table, and owned
// frame-budgeted GPU upload queue. No GPU feedback or disk IO worker here.
#pragma once

#include "TextureStreaming.h"
#include "VirtualTexturePhysicalMips.h"
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
    int physicalMipLevels = 1; // opt-in complete mip chain per physical array slice
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
        if (!device_ || cfg.physicalPageSize < 4 || cfg.physicalPageSize > 4096 ||
            cfg.physicalMipLevels < 1 ||
            cfg.physicalMipLevels > maxTextureMipLevels(cfg.physicalPageSize,cfg.physicalPageSize) ||
            cfg.physicalPages < 1 ||
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
        TextureDesc atlasDesc =
            pages_.atlasDescriptor(cfg.physicalPageSize,cfg.physicalFormat);
        atlasDesc.mipLevels = cfg.physicalMipLevels;
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


    // Atomic *publication* across every authored mip of a physical page.
    // View memory is snapshotted immediately by TextureStreamQueue::enqueue.
    // The first returned ticket identifies the entire mip batch for cancel().
    // No physical slice becomes sampleable until all its subresources succeed.
    TextureUploadTicket stageMipChain(
        VirtualPageKey key,const std::vector<VirtualTextureMipView>& levels,
        bool pin=false) {
        if (!ready() || levels.size()!=size_t(config_.physicalMipLevels)) {
            error_="virtual texture unavailable or incorrect physical mip count";
            return {};
        }
        const int side=config_.physicalPageSize;
        for(size_t mip=0;mip<levels.size();++mip) {
            const size_t s=size_t(std::max(1,side>>int(mip)));
            const size_t expected=s*s*4u;
            if (!levels[mip].data || levels[mip].bytes!=expected ||
                expected>size_t(std::numeric_limits<int>::max())) {
                error_="physical mip chain has missing or incorrect payload";
                return {};
            }
        }
        const VirtualPageAllocation allocation=pages_.reserve(key,pin);
        if(!allocation.valid() || !allocation.needsUpload) {
            error_="physical page invalid, pending, resident or atlas slots pinned";
            return {};
        }
        // Before the first overwrite, remove any evicted previous slot alias.
        if(!synchronizeTable()) { failed_=true; return {}; }
        std::vector<TextureUploadTicket> tickets;
        tickets.reserve(levels.size());
        for(size_t mip=0;mip<levels.size();++mip) {
            const size_t s=size_t(std::max(1,side>>int(mip)));
            TextureUploadDesc update;
            update.mipLevel=int(mip);
            update.arrayLayer=allocation.physicalSlice;
            update.width=int(s);update.height=int(s);
            update.rowPitchBytes=int(s*4u);
            update.slicePitchBytes=int(s*s*4u);
            const TextureUploadTicket ticket=uploads_.enqueue(
                atlas_,update,levels[mip].data,levels[mip].bytes);
            if(!ticket.valid()) {
                const std::string enqueueError=uploads_.lastError();
                for(const auto& t:tickets) uploads_.cancel(t);
                pages_.abandon(key,allocation);
                if(!synchronizeTable()) failed_=true;
                error_=enqueueError.empty() ?
                    "physical page mip staging failed" : enqueueError;
                return {};
            }
            tickets.push_back(ticket);
        }
        const uint64_t root=tickets.front().value;
        PendingBatch batch;
        batch.key=key;batch.allocation=allocation;
        batch.remaining=tickets.size();
        batch.tickets=std::move(tickets);
        batches_.emplace(allocation.generation,std::move(batch));
        roots_.emplace(root,allocation.generation);
        for(const auto& ticket:batches_.at(allocation.generation).tickets)
            pending_.emplace(ticket.value,allocation.generation);
        error_.clear();
        return {root};
    }

    // Compatibility API: existing one-mip pages work without changes.
    // With physicalMipLevels>1 the supplied *guttered* RGBA8 physical page
    // gets a complete alpha-correct CPU mip chain before enqueue. For authored
    // mips use stageMipChain() directly instead.
    TextureUploadTicket stage(VirtualPageKey key, const void* packed,
                              size_t bytes, bool pin=false) {
        const size_t side=config_.physicalPageSize>0 ?
                          size_t(config_.physicalPageSize) : 0u;
        if(!ready() || !packed || side*side*4u!=bytes) {
            error_="virtual texture unavailable or base physical page payload invalid";
            return {};
        }
        if(config_.physicalMipLevels==1)
            return stageMipChain(key,{{packed,bytes}},pin);
        std::vector<VirtualTextureMipLevel> generated;
        if(!buildVirtualTexturePhysicalMips(packed,bytes,config_.physicalPageSize,
               config_.physicalMipLevels,config_.physicalFormat==PixelFormat::RGBA8_sRGB,
               generated)) {
            error_="unable to generate physical mip chain";
            return {};
        }
        std::vector<VirtualTextureMipView> views;
        views.reserve(generated.size());
        for(const auto& level:generated)
            views.push_back({level.rgba.data(),level.rgba.size()});
        return stageMipChain(key,views,pin);
    }


    // Returns submitted subresource GPU transfers, not complete pages.
    // Exactly one page event is published only after *every* mip succeeds.
    size_t drain(const CompletionFn& onCompletion={}) {
        std::vector<std::pair<VirtualPageKey,bool>> events;
        const size_t submitted=uploads_.drain(
            [&](TextureUploadTicket ticket,TextureStreamId resource,bool success) {
                if(resource!=atlas_) return;
                auto it=pending_.find(ticket.value);
                if(it==pending_.end()) return;
                const uint64_t generation=it->second;
                pending_.erase(it);
                auto group=batches_.find(generation);
                if(group==batches_.end()) return;
                PendingBatch& batch=group->second;
                if(!success || --batch.remaining==0) {
                    const bool ok=success;
                    pages_.complete(batch.key,batch.allocation,ok);
                    events.emplace_back(batch.key,ok);
                    roots_.erase(batch.tickets.front().value);
                    batches_.erase(group);
                    if(!success) failed_=true;
                }
            });
        if(!failed_ && !synchronizeTable()) failed_=true;
        if(uploads_.failed(atlas_)) failed_=true;
        if(failed_) {
            pending_.clear(); batches_.clear(); roots_.clear();
        }
        if(onCompletion)
            for(const auto& event:events)
                onCompletion(event.first,event.second && !failed_);
        return submitted;
    }

    // Cancels every as-yet-unsubmitted mip in the batch. Earlier updates
    // completed on the device thread but have not been published in the table.
    bool cancel(TextureUploadTicket rootTicket) {
        auto root=roots_.find(rootTicket.value);
        if(root==roots_.end()) return false;
        auto batch=batches_.find(root->second);
        if(batch==batches_.end()) return false;
        for(const auto& ticket:batch->second.tickets) {
            auto pending=pending_.find(ticket.value);
            if(pending==pending_.end()) continue; // mip already submitted
            if(!uploads_.cancel(ticket)) return false;
            pending_.erase(pending);
        }
        const VirtualPageKey key=batch->second.key;
        const VirtualPageAllocation allocation=batch->second.allocation;
        roots_.erase(root);
        batches_.erase(batch);
        pages_.abandon(key,allocation);
        if(!synchronizeTable()) failed_=true;
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
        pending_.clear(); batches_.clear(); roots_.clear();
        if (atlas_.valid()) uploads_.destroy(atlas_);
        atlas_ = {};
        if (tableGpu_.valid() && device_) device_->destroyTexture(tableGpu_);
        tableGpu_ = {};
        pages_.clear(); table_.clear();
        config_ = {};
        failed_ = false;
    }

private:
    struct PendingBatch {
        VirtualPageKey key;
        VirtualPageAllocation allocation;
        size_t remaining = 0;
        std::vector<TextureUploadTicket> tickets;
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
    std::map<uint64_t,uint64_t> pending_; // queued ticket -> allocation generation
    std::map<uint64_t,PendingBatch> batches_; // generation -> atomic page batch
    std::map<uint64_t,uint64_t> roots_; // first ticket -> allocation generation
    std::string error_;
};

} // namespace aiverse::gfx
