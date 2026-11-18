// AIVERSE virtual texture CPU page cache.
// Physical storage is a user-created Texture2DArray atlas: each slice is one
// fixed-size page. The cache owns *mapping only*, never GPU texture storage.
// Upload through TextureStreamQueue (arrayLayer = physicalSlice). Pending
// pages cannot be evicted; a stale async completion cannot publish a new page.
#pragma once

#include "Gfx.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <vector>

namespace aiverse::gfx {

struct VirtualPageKey {
    int mip = -1, x = -1, y = -1, layer = -1;
    bool operator==(const VirtualPageKey&) const = default;
    bool operator<(const VirtualPageKey& other) const {
        return std::tie(mip, layer, y, x) <
               std::tie(other.mip, other.layer, other.y, other.x);
    }
};
struct VirtualPageAllocation {
    int physicalSlice = -1;
    uint64_t generation = 0;
    bool ready = false;
    bool needsUpload = false;
    bool valid() const { return physicalSlice >= 0 && generation != 0; }
};
struct VirtualPageStats {
    size_t capacity = 0, resident = 0, pending = 0, pinned = 0, free = 0;
    uint64_t evictions = 0;
};

class VirtualTexturePageCache {
public:
    // virtualTilesX/Y count pages at mip 0, not pixels. Physical slots are
    // array layers; choose a value accepted by Device::createTexture.
    bool initialize(int virtualTilesX, int virtualTilesY, int mipLevels,
                    int virtualLayers, int physicalSlots) {
        clear();
        if (virtualTilesX < 1 || virtualTilesY < 1 ||
            virtualTilesX > (1 << 20) || virtualTilesY > (1 << 20) ||
            mipLevels < 1 || mipLevels > 30 || virtualLayers < 1 ||
            virtualLayers > (1 << 16) || physicalSlots < 1 ||
            physicalSlots > (1 << 16)) return false;
        tilesX_ = virtualTilesX;
        tilesY_ = virtualTilesY;
        levels_ = mipLevels;
        layers_ = virtualLayers;
        slots_.resize(size_t(physicalSlots));
        return true;
    }

    // Build the compatible physical atlas descriptor. It allocates only the
    // 2DArray storage; shader page-table indirection is a separate consumer.
    TextureDesc atlasDescriptor(int pageSize, PixelFormat format) const {
        TextureDesc d;
        if (!slots_.empty() && pageSize > 0) {
            d.width = pageSize;
            d.height = pageSize;
            d.dimension = TextureDimension::Texture2DArray;
            d.arrayLayers = int(slots_.size());
            d.mipLevels = 1;
            d.format = format;
            d.filter = Filter::Linear;
        }
        return d;
    }

    VirtualPageAllocation reserve(VirtualPageKey key, bool pin = false) {
        if (!validKey(key)) return {};
        auto found = mappings_.find(key);
        if (found != mappings_.end()) {
            Slot& s = slots_[size_t(found->second)];
            s.lastUsed = tick();
            if (pin) s.pinned = true;
            // An in-flight page already has an upload owner. Repeated
            // lookups must never schedule a duplicate write to the slice.
            return {found->second, s.generation, s.ready, false};
        }
        int slot = -1;
        for (size_t i = 0; i < slots_.size(); ++i)
            if (!slots_[i].occupied) { slot = int(i); break; }
        if (slot < 0) {
            // LRU eviction is deterministic; tie-break by physical slot index.
            uint64_t oldest = std::numeric_limits<uint64_t>::max();
            for (size_t i = 0; i < slots_.size(); ++i) {
                const Slot& s = slots_[i];
                if (!s.occupied || !s.ready || s.pinned) continue;
                if (s.lastUsed < oldest) {
                    oldest = s.lastUsed;
                    slot = int(i);
                }
            }
        }
        if (slot < 0 || nextGeneration_ == std::numeric_limits<uint64_t>::max())
            return {};
        Slot& s = slots_[size_t(slot)];
        if (s.occupied) {
            mappings_.erase(s.key);
            ++evictions_;
        }
        s.key = key;
        s.occupied = true;
        s.ready = false;
        s.pinned = pin;
        s.generation = ++nextGeneration_;
        s.lastUsed = tick();
        mappings_.emplace(key, slot);
        return {slot, s.generation, false, true};
    }

    // Publish only once the corresponding GPU upload actually succeeded.
    // A failed upload retires the page and makes its slice reusable.
    bool complete(VirtualPageKey key, VirtualPageAllocation allocation, bool ok) {
        auto it = mappings_.find(key);
        if (it == mappings_.end() || !allocation.valid() ||
            it->second != allocation.physicalSlice) return false;
        Slot& s = slots_[size_t(it->second)];
        if (!s.occupied || s.ready || s.generation != allocation.generation)
            return false;
        if (ok) {
            s.ready = true;
            s.lastUsed = tick();
        } else {
            s = Slot{};
            mappings_.erase(it);
        }
        return true;
    }

    // Call ONLY after canceling the corresponding pending GPU upload; never
    // reuse an in-flight physical page slice that can still receive writes.
    bool abandon(VirtualPageKey key, VirtualPageAllocation allocation) {
        return complete(key, allocation, false);
    }

    bool touch(VirtualPageKey key) {
        auto it = mappings_.find(key);
        if (it == mappings_.end()) return false;
        Slot& s = slots_[size_t(it->second)];
        if (!s.ready) return false;
        s.lastUsed = tick();
        return true;
    }

    bool pin(VirtualPageKey key, bool pinned = true) {
        auto it = mappings_.find(key);
        if (it == mappings_.end()) return false;
        slots_[size_t(it->second)].pinned = pinned;
        return true;
    }

    bool evict(VirtualPageKey key) {
        auto it = mappings_.find(key);
        if (it == mappings_.end()) return false;
        Slot& s = slots_[size_t(it->second)];
        if (!s.ready || s.pinned) return false;
        s = Slot{};
        mappings_.erase(it);
        ++evictions_;
        return true;
    }

    VirtualPageAllocation resolve(VirtualPageKey key) const {
        auto it = mappings_.find(key);
        if (it == mappings_.end()) return {};
        const Slot& s = slots_[size_t(it->second)];
        if (!s.ready) return {};
        return {it->second, s.generation, true, false};
    }

    // Sample an available coarser ancestor from the SAME virtual layer.
    // Return invalid when no resident parent or pinned fallback exists;
    // the application must use an explicit fallback material in that case.
    VirtualPageAllocation resolveWithCoarseFallback(VirtualPageKey key) const {
        if (!validKey(key)) return {};
        for (int mip = key.mip; mip < levels_; ++mip) {
            const int shift = mip - key.mip;
            VirtualPageKey parent{mip, key.x >> shift, key.y >> shift, key.layer};
            auto resolved = resolve(parent);
            if (resolved.valid()) return resolved;
        }
        return {};
    }

    VirtualPageStats stats() const {
        VirtualPageStats s;
        s.capacity = slots_.size();
        s.evictions = evictions_;
        for (const Slot& slot : slots_) {
            if (!slot.occupied) { ++s.free; continue; }
            if (slot.ready) ++s.resident;
            else ++s.pending;
            if (slot.pinned) ++s.pinned;
        }
        return s;
    }

    // Clear page maps but NEVER recycle logical generation IDs. A completion
    // from an earlier atlas cannot publish a page in the new atlas.
    void clear() {
        slots_.clear();
        mappings_.clear();
        tilesX_ = tilesY_ = levels_ = layers_ = 0;
        clock_ = 0;
        evictions_ = 0;
    }

private:
    struct Slot {
        VirtualPageKey key;
        uint64_t generation = 0;
        uint64_t lastUsed = 0;
        bool occupied = false;
        bool ready = false;
        bool pinned = false;
    };

    bool validKey(const VirtualPageKey& key) const {
        if (key.mip < 0 || key.mip >= levels_ ||
            key.layer < 0 || key.layer >= layers_ ||
            key.x < 0 || key.y < 0) return false;
        const int scale = 1 << key.mip;
        const int w = (tilesX_ + scale - 1) / scale;
        const int h = (tilesY_ + scale - 1) / scale;
        return key.x < w && key.y < h;
    }

    uint64_t tick() {
        if (clock_ == std::numeric_limits<uint64_t>::max()) {
            // Preserve relative LRU order through an extremely rare counter
            // wrap, maintaining deterministic tie-breaking.
            std::vector<size_t> indices;
            for (size_t i = 0; i < slots_.size(); ++i)
                if (slots_[i].occupied) indices.push_back(i);
            std::stable_sort(indices.begin(), indices.end(),
                [this](size_t a, size_t b) {
                    return slots_[a].lastUsed < slots_[b].lastUsed;
                });
            uint64_t t = 0;
            for (size_t i : indices) slots_[i].lastUsed = ++t;
            clock_ = t;
        }
        return ++clock_;
    }

    int tilesX_ = 0, tilesY_ = 0, levels_ = 0, layers_ = 0;
    uint64_t clock_ = 0, nextGeneration_ = 0, evictions_ = 0;
    std::vector<Slot> slots_;
    std::map<VirtualPageKey, int> mappings_;
};

} // namespace aiverse::gfx
