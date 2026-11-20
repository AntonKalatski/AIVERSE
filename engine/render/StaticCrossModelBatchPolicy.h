#pragma once
// GPU-page compatibility for opt-in static CPU MDI batching.
//
// Each model keeps its own primitive, transform, culling, LOD selection and
// source material. Only *after* view selection can compatible rigid primitives
// from different models sharing a GPU geometry arena join one MDI submission.
// Material/pipeline equivalence is checked separately by Renderer3D.
#include "Model.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

namespace aiverse::render {

// Temporary correctness gate: enable only after Vulkan direct/indirect pixel
// parity has been verified for rigid pooled models with nonzero baseVertex.
// This controls neither arbitrary RHI draws nor Metal GPU ICB.
inline bool vulkanPooledMdiAddressingEnabled() {
    static const bool enabled = [] {
        const char* setting = std::getenv("AIVERSE_VULKAN_POOLED_MDI");
        return setting && std::strcmp(setting, "1") == 0;
    }();
    return enabled;
}

inline bool staticCrossModelPageCompatible(const Model& a, const Model& b) {
    return &a != &b &&
        a.geometryPoolPage && b.geometryPoolPage &&
        a.geometryPoolPage == b.geometryPoolPage &&
        a.vb.valid() && a.ib.valid() &&
        a.vb == b.vb && a.ib == b.ib &&
        !a.geometry && !b.geometry;
}

// Preflight is a *necessary-condition* filter only. The renderer still
// performs full shader/material/buffer checks and enforces the backend's
// indexed-command threshold before every actual MDI submission.
// A group combines one geometry page, exact material-state group and
// draw flags. No camera/view/LOD data is cached across frames.
struct StaticCrossModelPreflightKey {
    const GeometryPoolPage* page = nullptr;
    uint32_t materialGroup = 0;
    uint32_t flags = 0;
    uint32_t vertexBuffer = 0;
    uint32_t indexBuffer = 0;
    constexpr bool operator==(const StaticCrossModelPreflightKey&) const = default;
};

struct StaticCrossModelPreflightKeyHash {
    size_t operator()(const StaticCrossModelPreflightKey& key) const noexcept {
        size_t value = std::hash<const GeometryPoolPage*>{}(key.page);
        const auto combine = [&value](size_t next) {
            value ^= next + size_t(0x9e3779b9) + (value << 6) + (value >> 2);
        };
        combine(std::hash<uint32_t>{}(key.materialGroup));
        combine(std::hash<uint32_t>{}(key.flags));
        combine(std::hash<uint32_t>{}(key.vertexBuffer));
        combine(std::hash<uint32_t>{}(key.indexBuffer));
        return value;
    }
};

struct StaticCrossModelPreflightGroup {
    const Model* firstModel = nullptr;
    bool mixedModels = false;
    bool ready = false; // enough visible mixed-model indexed commands
    bool permitted = true; // optional per-page CPU feedback decision
    size_t items = 0;
    // The supported backend command threshold is at most 32 (Metal env
    // tuning), or 4 on other platforms. Fixed storage avoids one heap
    // allocation per material/page group in dense scenes.
    std::array<const Primitive*, 32> distinctPrimitives{};
    size_t distinctCount = 0;

    void observe(const Model* model, const Primitive* primitive, size_t threshold) {
        if (!model || !primitive || !threshold) return;
        if (!firstModel) firstModel = model;
        else if (model != firstModel) mixedModels = true;
        ++items;
        const size_t limit = std::min(threshold, distinctPrimitives.size());
        if (distinctCount < limit &&
            std::find(distinctPrimitives.begin(),
                      distinctPrimitives.begin() + distinctCount,
                      primitive) == distinctPrimitives.begin() + distinctCount)
            distinctPrimitives[distinctCount++] = primitive;
    }

    bool opportunity(size_t threshold, size_t maxItemsPerSubmission) const {
        return mixedModels && items >= threshold &&
            maxItemsPerSubmission >= threshold &&
            distinctCount >= threshold;
    }
};

// A reusable renderer-owned, *per-pass* workspace. The fixed-size open-
// addressing directory avoids allocating one unordered_map node for every
// page/material group on each shadow/opaque pass. Each index is meaningful
// only until the next beginPass(); no source pointers are dereferenced after
// the pass that inserted them.
class StaticCrossModelPreflightWorkspace {
public:
    static constexpr uint32_t kMaxGroups = 2048;
    static constexpr uint32_t kHashSlots = 4096; // power-of-two, <=50% full
    static constexpr uint16_t kInvalidGroup = std::numeric_limits<uint16_t>::max();

    struct Entry {
        StaticCrossModelPreflightKey key{};
        StaticCrossModelPreflightGroup group{};
        // Ordered once per render pass. Only preflight-ready, permitted
        // groups get a rank; this never persists material/view decisions.
        uint16_t sortRank = kInvalidGroup;
    };

    void beginPass(size_t itemCount) {
        if (slots_.empty()) slots_.resize(kHashSlots);
        if (++epoch_ == 0) {
            // Extremely rare wraparound must not interpret old slots as new.
            for (Slot& slot : slots_) slot.epoch = 0;
            epoch_ = 1;
        }
        // Item generations are intentionally 32-bit to avoid 64-bit stamps
        // for every ModelItem in a large city. At wrap, explicitly reset
        // stamps once so no stale group or mode can become current again.
        if (++itemEpoch_ == 0) {
            std::fill(groupStamp_.begin(), groupStamp_.end(), uint32_t{0});
            std::fill(modeStamp_.begin(), modeStamp_.end(), uint32_t{0});
            itemEpoch_ = 1;
        }
        groups_.clear(); // retain capacity, reset all per-pass group state
        if (groupForItem_.size() < itemCount)
            groupForItem_.resize(itemCount, kInvalidGroup);
        if (groupStamp_.size() < itemCount)
            groupStamp_.resize(itemCount, uint32_t{0});
        modesActive_ = false;
        activeItemCount_ = itemCount;
        // No group-index or mode-array clear is necessary. Their generation
        // stamps make old values inaccessible in this pass.
    }

    // Only grow the mode storage if some group actually qualifies; all
    // unwritten entries logically read zero through their epoch stamps.
    void activateModes(size_t itemCount) {
        if (modes_.size() < itemCount)
            modes_.resize(itemCount, uint8_t{0});
        if (modeStamp_.size() < itemCount)
            modeStamp_.resize(itemCount, uint32_t{0});
        modesActive_ = true;
    }
    // Called even when no preflight is run (reflection/other phases). This
    // is a logical clear, not a linear write over all scene items.
    void clearModes() { modesActive_ = false; }

    uint16_t findOrCreate(const StaticCrossModelPreflightKey& key) {
        if (slots_.empty()) return kInvalidGroup; // beginPass() required
        uint32_t slotIndex = uint32_t(StaticCrossModelPreflightKeyHash{}(key)) &
                             (kHashSlots - 1);
        for (uint32_t probe = 0; probe < kHashSlots; ++probe) {
            Slot& slot = slots_[slotIndex];
            if (slot.epoch != epoch_) {
                if (groups_.size() == kMaxGroups) return kInvalidGroup;
                const uint16_t index = uint16_t(groups_.size());
                groups_.push_back(Entry{key, {}});
                slot.epoch = epoch_;
                slot.group = index;
                return index;
            }
            if (groups_[slot.group].key == key)
                return slot.group;
            slotIndex = (slotIndex + 1) & (kHashSlots - 1);
        }
        return kInvalidGroup;
    }

    void rememberItem(uint32_t index, uint16_t groupIndex) {
        groupForItem_[index] = groupIndex;
        groupStamp_[index] = itemEpoch_;
    }
    uint16_t groupForItem(uint32_t index) const {
        return index < activeItemCount_ && groupStamp_[index] == itemEpoch_
            ? groupForItem_[index] : kInvalidGroup;
    }
    void rememberMode(uint32_t index, uint8_t mode) {
        modes_[index] = mode;
        modeStamp_[index] = itemEpoch_;
    }
    uint8_t modeOf(uint32_t index) const {
        return modesActive_ && index < activeItemCount_ &&
               modeStamp_[index] == itemEpoch_ ? modes_[index] : uint8_t{0};
    }
    std::vector<Entry>& groups() { return groups_; }
    const std::vector<Entry>& groups() const { return groups_; }

    // Existing renderModels sorting compares material state, page pointers,
    // buffers and draw flags for *every* item comparison. Those properties
    // are uniform within each exact preflight group; order the (<=2048)
    // permitted groups once, and compare their numeric rank for each item.
    // Match the prior comparator's cross-candidate key ordering exactly.
    size_t prepareSortRanks() {
        sortedGroups_.clear(); // preserve high-water allocation
        for (size_t i = 0; i < groups_.size(); ++i) {
            Entry& entry = groups_[i];
            entry.sortRank = kInvalidGroup;
            if (entry.group.ready && entry.group.permitted)
                sortedGroups_.push_back(uint16_t(i));
        }
        std::sort(sortedGroups_.begin(), sortedGroups_.end(),
                  [this](uint16_t ai, uint16_t bi) {
            const auto& a = groups_[ai].key;
            const auto& b = groups_[bi].key;
            if (a.materialGroup != b.materialGroup)
                return a.materialGroup < b.materialGroup;
            if (a.page != b.page)
                return std::less<const GeometryPoolPage*>{}(a.page, b.page);
            if (a.vertexBuffer != b.vertexBuffer)
                return a.vertexBuffer < b.vertexBuffer;
            if (a.indexBuffer != b.indexBuffer)
                return a.indexBuffer < b.indexBuffer;
            return a.flags < b.flags;
        });
        for (size_t i = 0; i < sortedGroups_.size(); ++i)
            groups_[sortedGroups_[i]].sortRank = uint16_t(i);
        return sortedGroups_.size();
    }

    // The caller has already established modeOf(index) == 1. Do not
    // repeat the stamped mode lookup in every sort comparison: the group
    // index carries its own epoch guard and unranked groups return invalid.
    uint16_t sortRankForPermittedItem(uint32_t itemIndex) const {
        const uint16_t groupIndex = groupForItem(itemIndex);
        return groupIndex == kInvalidGroup
            ? kInvalidGroup : groups_[groupIndex].sortRank;
    }

    // Bucketize the *partitioned* accepted static prefix by the previously
    // prepared exact page/material/flags ranks. The caller has already checked
    // modeOf(item)==1. Buckets preserve the candidate-first ordering of the
    // previous comparator, but only sort within each exact group instead of
    // running a global O(C log C) comparison sort on all C candidates.
    //
    // Defensive failure leaves the source order untouched: validation and
    // counts run before any writes, so the renderer can use its old comparison
    // sort when an invalid/stale rank is encountered.
    template<class WithinGroupLess>
    bool bucketizePermittedPrefix(std::vector<uint32_t>& order, size_t count,
                                  WithinGroupLess&& withinGroupLess) {
        const size_t rankCount = sortedGroups_.size();
        if (!rankCount || count > order.size()) return false;
        bucketOffsets_.assign(rankCount + 1, size_t{0});
        for (size_t i = 0; i < count; ++i) {
            const uint16_t rank = sortRankForPermittedItem(order[i]);
            if (rank == kInvalidGroup || rank >= rankCount) return false;
            ++bucketOffsets_[size_t(rank) + 1];
        }
        for (size_t i = 1; i <= rankCount; ++i)
            bucketOffsets_[i] += bucketOffsets_[i - 1];
        bucketWrites_.assign(bucketOffsets_.begin(), bucketOffsets_.end() - 1);
        if (bucketItems_.size() < count) bucketItems_.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const uint16_t rank = sortRankForPermittedItem(order[i]);
            bucketItems_[bucketWrites_[rank]++] = order[i];
        }
        std::copy_n(bucketItems_.begin(), count, order.begin());
        for (size_t rank = 0; rank < rankCount; ++rank) {
            const size_t first = bucketOffsets_[rank];
            const size_t last = bucketOffsets_[rank + 1];
            if (last - first > 1)
                std::sort(order.begin() + first, order.begin() + last,
                          withinGroupLess);
        }
        return true;
    }

    size_t rankedGroupCount() const { return sortedGroups_.size(); }

    size_t retainedBytes() const {
        return slots_.capacity() * sizeof(Slot) +
               groups_.capacity() * sizeof(Entry) +
               sortedGroups_.capacity() * sizeof(uint16_t) +
               bucketOffsets_.capacity() * sizeof(size_t) +
               bucketWrites_.capacity() * sizeof(size_t) +
               bucketItems_.capacity() * sizeof(uint32_t) +
               groupForItem_.capacity() * sizeof(uint16_t) +
               groupStamp_.capacity() * sizeof(uint32_t) +
               modes_.capacity() * sizeof(uint8_t) +
               modeStamp_.capacity() * sizeof(uint32_t);
    }

    // Explicit full-scene reload / renderer shutdown: release the high-water
    // allocations rather than retaining references to old streamed assets.
    void release() {
        std::vector<Slot>().swap(slots_);
        std::vector<Entry>().swap(groups_);
        std::vector<uint16_t>().swap(sortedGroups_);
        std::vector<size_t>().swap(bucketOffsets_);
        std::vector<size_t>().swap(bucketWrites_);
        std::vector<uint32_t>().swap(bucketItems_);
        std::vector<uint16_t>().swap(groupForItem_);
        std::vector<uint32_t>().swap(groupStamp_);
        std::vector<uint8_t>().swap(modes_);
        std::vector<uint32_t>().swap(modeStamp_);
        epoch_ = 0;
        itemEpoch_ = 0;
        activeItemCount_ = 0;
        modesActive_ = false;
    }

private:
    struct Slot {
        uint64_t epoch = 0;
        uint16_t group = 0;
    };
    std::vector<Slot> slots_; // allocated once when the feature is first used
    std::vector<Entry> groups_;
    std::vector<uint16_t> sortedGroups_; // bounded current-pass group ranks
    std::vector<size_t> bucketOffsets_; // exclusive prefix offsets, rankCount + 1
    std::vector<size_t> bucketWrites_; // temporary per-rank write positions
    std::vector<uint32_t> bucketItems_; // high-water candidate scratch
    std::vector<uint16_t> groupForItem_; // contents guarded by groupStamp_
    std::vector<uint32_t> groupStamp_;
    std::vector<uint8_t> modes_; // 0=ordinary, 1=MDI, 2=instancing
    std::vector<uint32_t> modeStamp_;
    uint64_t epoch_ = 0;        // hash-directory generation
    uint32_t itemEpoch_ = 0;    // stamped item-index generation
    size_t activeItemCount_ = 0;
    bool modesActive_ = false;
};

} // namespace aiverse::render
