// AIVERSE disk-backed physical virtual texture streaming facade.
// A single worker reads page files; only pump() on the graphics-device
// thread touches gfx::Device or publishes a virtual page-table mapping.
// Presentation state only; never authoritative gameplay/ECS state.
#pragma once

#include "VirtualTextureDiskQueue.h"
#include "VirtualTextureStreamer.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::gfx {

struct VirtualTextureFileStats {
    VirtualDiskPageStats disk;
    VirtualPageStats pages;
    TextureStreamStats gpu;
    size_t bufferedDiskPages = 0, gpuPendingPages = 0, trackedRequests = 0;
};

class VirtualTextureFileStreamer {
public:
    using CompletionFn = std::function<void(VirtualDiskPageTicket, VirtualPageKey,
                                            bool, const std::string&)>;
    explicit VirtualTextureFileStreamer(Device& device,
                                        TextureStreamConfig gpuUploadBudget = {})
        : atlas_(device, gpuUploadBudget) {}
    ~VirtualTextureFileStreamer() { shutdown(); }
    VirtualTextureFileStreamer(const VirtualTextureFileStreamer&) = delete;
    VirtualTextureFileStreamer& operator=(const VirtualTextureFileStreamer&) = delete;

    bool initialize(VirtualTextureStreamConfig atlas,
                    VirtualDiskPageConfig disk,
                    size_t maxBufferedDiskPages = 8) {
        shutdown();
        if (maxBufferedDiskPages < 1 || maxBufferedDiskPages > 128 ||
            atlas.tilesX != disk.tilesX || atlas.tilesY != disk.tilesY ||
            atlas.mipLevels != disk.mipLevels ||
            atlas.virtualLayers != disk.virtualLayers ||
            atlas.physicalPageSize != disk.pageSize ||
            atlas.physicalFormat != PixelFormat::RGBA8) {
            error_ = "virtual atlas and disk-page configuration do not match";
            return false;
        }
        if (!atlas_.initialize(atlas)) {
            error_ = atlas_.lastError();
            return false;
        }
        if (!disk_.start(std::move(disk))) {
            error_ = disk_.lastError();
            atlas_.shutdown();
            return false;
        }
        maxBufferedPages_ = maxBufferedDiskPages;
        initialized_ = true;
        error_.clear();
        return true;
    }

    // Caller requests coarse fallback pages first if it wants them pinned.
    // Existing resident pages and duplicate in-flight keys are not re-read.
    // A request accepted here has exactly one completion or explicit cancel.
    VirtualDiskPageTicket requestPage(VirtualPageKey key, bool pin = false) {
        if (!ready()) {
            error_ = "virtual file streamer is not ready";
            return {};
        }
        const auto existing = active_.find(key);
        if (existing != active_.end()) return {existing->second};
        if (atlas_.resolveExact(key).valid()) {
            if (pin) atlas_.pin(key,true);
            error_ = "virtual page is already resident";
            return {};
        }
        const VirtualDiskPageTicket ticket = disk_.request(key);
        if (!ticket.valid()) {
            error_ = disk_.lastError();
            return {};
        }
        active_.emplace(key,ticket.id);
        requests_.emplace(ticket.id,RequestState{key,pin,{}});
        error_.clear();
        return ticket;
    }

    // Cancel only while the request is pending in disk/CPU or staging GPU
    // queue. No in-flight GPU command is canceled behind the driver's back.
    bool cancel(VirtualDiskPageTicket ticket) {
        auto it = requests_.find(ticket.id);
        if (it == requests_.end()) return false;
        if (it->second.gpuTicket.valid()) {
            if (!atlas_.cancel(it->second.gpuTicket)) return false;
        } else {
            auto buffered = std::find_if(buffered_.begin(),buffered_.end(),
                [ticket](const VirtualDiskPageResult& r) {
                    return r.ticket == ticket;
                });
            if (buffered != buffered_.end()) buffered_.erase(buffered);
            else if (!disk_.cancel(ticket)) return false;
        }
        active_.erase(it->second.key);
        requests_.erase(it);
        return true;
    }

    // Poll the disk worker (no GPU calls there), stage a bounded set of
    // completed disk reads, then drain GPU uploads and publish the atlas
    // page-table after successful updates. Events are emitted only after
    // both upload and page-table publication have finished on this thread.
    size_t pump(size_t maxNewGpuPages = 8, const CompletionFn& callback = {}) {
        if (!initialized_) return 0;
        std::vector<Event> events;
        if (buffered_.size() < maxBufferedPages_) {
            std::vector<VirtualDiskPageResult> ready;
            const size_t slots = maxBufferedPages_ - buffered_.size();
            disk_.takeReady(ready, slots*disk_.pageBytes(), slots);
            for (auto& page : ready) buffered_.push_back(std::move(page));
        }

        size_t staged = 0;
        while (!buffered_.empty() && staged < maxNewGpuPages) {
            const auto& next = buffered_.front();
            auto record = requests_.find(next.ticket.id);
            if (record == requests_.end()) {
                buffered_.pop_front(); // canceled before publication
                continue;
            }
            if (!next.success()) {
                events.push_back({next.ticket,next.key,false,next.error});
                active_.erase(next.key);
                requests_.erase(record);
                buffered_.pop_front();
                continue;
            }
            if (!atlas_.ready()) break;
            const auto gpuTicket = atlas_.stage(next.key,next.rgba.data(),
                                                next.rgba.size(),record->second.pin);
            if (!gpuTicket.valid()) {
                // Physical pages may all be pinned or pending, or the GPU
                // staging budget may be full. Keep CPU bytes for next pump.
                if (!atlas_.ready()) break;
                error_ = atlas_.lastError();
                break;
            }
            record->second.gpuTicket = gpuTicket;
            buffered_.pop_front(); // GPU staging made its own CPU copy
            ++staged;
        }

        atlas_.drain([&](VirtualPageKey key,bool ok) {
            const auto active = active_.find(key);
            if (active == active_.end()) return;
            const VirtualDiskPageTicket ticket{active->second};
            const auto req = requests_.find(ticket.id);
            if (req == requests_.end() || !req->second.gpuTicket.valid()) return;
            events.push_back({ticket,key,ok,ok ? std::string{} : atlas_.lastError()});
            requests_.erase(req);
            active_.erase(active);
        });

        if (!atlas_.ready()) {
            error_ = atlas_.lastError();
            if (error_.empty()) error_ = "GPU virtual texture atlas quarantined";
            for (auto& [id,request] : requests_) {
                disk_.cancel({id});
                events.push_back({{id},request.key,false,error_});
            }
            buffered_.clear();
            active_.clear();
            requests_.clear();
            initialized_ = false; // requires explicit shutdown + reinitialize
            for (auto& e : events) {
                e.ok = false;
                if (e.error.empty()) e.error = error_;
            }
        }

        if (callback)
            for (const auto& e : events)
                callback(e.ticket,e.key,e.ok,e.error);
        return staged;
    }

    void shutdown() {
        // Join I/O worker before freeing its result state and the GPU atlas.
        disk_.shutdown();
        buffered_.clear();
        active_.clear();
        requests_.clear();
        atlas_.shutdown();
        initialized_ = false;
        maxBufferedPages_ = 0;
    }

    bool ready() const { return initialized_ && atlas_.ready(); }
    TextureHandle physicalAtlas() const { return ready()?atlas_.physicalAtlas():TextureHandle{}; }
    TextureHandle pageTableTexture() const { return ready()?atlas_.pageTableTexture():TextureHandle{}; }
    VirtualPageAllocation resolve(VirtualPageKey key) const {
        return ready()?atlas_.resolve(key):VirtualPageAllocation{};
    }
    std::array<uint8_t,4> encodedPageTable(int x,int y,int layer) const {
        return ready()?atlas_.encodedPageTable(x,y,layer):std::array<uint8_t,4>{};
    }
    VirtualTextureFileStats stats() const {
        VirtualTextureFileStats stats;
        stats.disk = disk_.stats();
        stats.pages = atlas_.pageStats();
        stats.gpu = atlas_.streamStats();
        stats.bufferedDiskPages = buffered_.size();
        stats.trackedRequests = requests_.size();
        for (const auto& [_,r] : requests_)
            if (r.gpuTicket.valid()) ++stats.gpuPendingPages;
        return stats;
    }
    std::string lastError() const { return error_; }

private:
    struct RequestState {
        VirtualPageKey key;
        bool pin = false;
        TextureUploadTicket gpuTicket;
    };
    struct Event {
        VirtualDiskPageTicket ticket;
        VirtualPageKey key;
        bool ok = false;
        std::string error;
    };
    VirtualTextureStreamer atlas_;
    VirtualTextureDiskQueue disk_;
    std::map<VirtualPageKey,uint64_t> active_;
    std::map<uint64_t,RequestState> requests_;
    std::deque<VirtualDiskPageResult> buffered_;
    size_t maxBufferedPages_ = 0;
    bool initialized_ = false;
    std::string error_;
};

} // namespace aiverse::gfx
