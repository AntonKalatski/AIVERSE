#pragma once
// STRATA Stage 15: budgeted index-only GPU residency, shared by all genres.
// Source Model VB remains owned by Renderer3D / GlobalGeometryPool.
// Residency is one immutable, compact indexed RHI buffer generation; never
// overwrite GPU indices while an earlier frame may still read them.
// Call updates at a renderer submission epoch boundary, before drawModel().
#include "StrataModelAdapter.h"
#include "../gfx/Gfx.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::render::strata {
struct IndexDemand {
    uint32_t primitive = 0; // ordinal in ModelAsset::primitives
    uint32_t node = 0;
    float priority = 0.0f; // larger demand wins when budget is exhausted
};
struct IndexSpan {
    uint32_t firstIndex = 0, indexCount = 0;
    bool valid() const { return indexCount != 0; }
};
struct IndexResidencyStats {
    uint64_t activeBytes = 0, pinnedRootBytes = 0, budgetBytes = 0;
    uint64_t generations = 0, uploads = 0, evictions = 0;
    uint64_t deferredDemands = 0, failedUploads = 0;
    uint32_t residentNodes = 0;
};
class IndexResidency final {
public:
    IndexResidency() = default;
    IndexResidency(const IndexResidency&) = delete;
    IndexResidency& operator=(const IndexResidency&) = delete;
    ~IndexResidency() = default; // call shutdown(device) while RHI is alive

    // Performs a real root-only index upload, not a virtual residency flag.
    // Roots are pinned; if budget cannot contain them, returns false without
    // allocating/publishing any GPU state.
    bool initialize(gfx::Device& device, const ModelAsset& asset,
                    size_t budgetBytes, std::string* error = nullptr);
    // Atomic compact/repack: requests promote complete sibling groups along
    // each path (parent->children). Rejected requests retain a drawable
    // resident ancestor. Existing generation is unchanged on GPU failure.
    // Can evict unrequested descendants; never evicts pinned roots.
    bool update(const std::vector<IndexDemand>& demands,
                std::string* error = nullptr);

    IndexSpan span(uint32_t primitive, uint32_t node) const;
    bool resident(uint32_t primitive, uint32_t node) const {
        return span(primitive,node).valid();
    }
    gfx::BufferHandle buffer() const { return buffer_; }
    bool valid() const { return device_ && device_->gpuHealthy() && buffer_.valid(); }
    const IndexResidencyStats& stats() const { return stats_; }
    void shutdown(gfx::Device& device);

private:
    const ModelAsset* asset_ = nullptr; // immutable borrowed CPU backing data
    gfx::Device* device_ = nullptr; // borrowed RHI, never cross-device destroy
    gfx::BufferHandle buffer_{};
    std::vector<std::vector<IndexSpan>> spans_;
    std::vector<std::vector<uint32_t>> parent_;
    IndexResidencyStats stats_{};
};
} // namespace aiverse::render::strata
