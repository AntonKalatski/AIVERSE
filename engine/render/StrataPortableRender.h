#pragma once
// Strata portable indexed raster path for Metal, Vulkan, GLES3 / WebGL2
// and desktop GL. CPU cluster selection is an independent correctness path;
// GPU compute selection is optional and never required for raster support.
// One original source VB; auxiliary indices live in its GlobalGeometryPool
// when available or ONE dedicated index buffer otherwise. No second VB/pool.
#include "StrataModelAdapter.h"
#include "StrataView.h"
#include "GlobalGeometryPool.h"
#include "StrataIndexResidency.h"

#include <memory>

#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::render::strata {

// The owning Renderer3D draws the same prepared alias Model in main,
// sun-shadow and reflection passes. We therefore resolve ONE conservative
// cluster frontier covering all supplied passes, never a main-only cull.
struct PortablePassView {
    mat4 viewProjection{};
    int width = 0, height = 0;
    bool clipDepthZeroToOne = true;
    float errorThresholdPixels = 1.0f;
};
struct PortableRenderStats {
    uint64_t drawPreparations = 0;
    uint64_t selectedClusters = 0;
    uint64_t fallbackPreparations = 0;
    bool pooledIndices = false;
    bool dedicatedIndices = false;
    bool streamingIndices = false;
    uint64_t residentIndexBytes = 0;
    bool forcedFinest = false;
};

// Immutable per-source GPU index sidecar and CPU hierarchy. The caller owns
// Model/ModelAsset lifetime and must shutdown BEFORE releasing the source.
// An alias returned by prepareModel borrows GPU/texture handles and MUST
// NOT be uploaded or releaseModel()'d. Keep it alive until Renderer3D::render
// has consumed the draw; discard it after the frame completes.
// Explicit fallback: leave output untouched on failure and draw the original.
class PortableSource final {
public:
    PortableSource() = default;
    PortableSource(const PortableSource&) = delete;
    PortableSource& operator=(const PortableSource&) = delete;
    ~PortableSource() = default; // device must be passed to shutdown explicitly

    // indexBudgetBytes==0 preserves the fully resident Stage 14 index path.
    // Positive budgets opt in to real, root-first compact GPU index residency.
    bool stage(gfx::Device& device, const Model& source,
               const ModelAsset& asset, std::string* error = nullptr,
               size_t indexBudgetBytes = 0,
               bool preferIncremental = false,
               size_t uploadBudgetBytes = 0,
               std::shared_ptr<DiskIndexPages> diskPages = {});
    bool streamingEnabled() const { return bool(streaming_); }
    const IndexResidencyStats* residencyStats() const {
        return streaming_ ? &streaming_->stats() : nullptr;
    }
    // Called BEFORE any Renderer3D::drawModel in the new submission epoch,
    // once per source after merging demands from all its instances/passes.
    bool updateResidency(const std::vector<IndexDemand>& demands,
                         std::string* error = nullptr);
    // Predict ideal clusters from immutable CPU hierarchy (NOT current GPU
    // mask). Does not submit, allocate or mutate; appends only valid demands.
    bool collectResidencyDemands(const mat4& instanceWorld,
                     const std::vector<PortablePassView>& passes,
                     bool forceFinestForUnspecifiedPass,
                     std::vector<IndexDemand>& out) const;
    bool prepareModel(const mat4& instanceWorld,
                      const std::vector<PortablePassView>& passes,
                      bool forceFinestForUnspecifiedPass,
                      Model& output, std::string* error = nullptr);
    bool validFor(const Model& source) const;
    // Must be called with the same live device used by stage(). A wrong
    // device is refused without destroying foreign RHI buffer handles.
    void shutdown(gfx::Device& device);
    const PortableRenderStats& stats() const { return stats_; }
    gfx::BufferHandle indexBuffer() const { return indexBuffer_; }
    uint32_t firstIndex() const { return firstIndex_; }
private:
    gfx::Device* device_ = nullptr; // borrowed: index handles belong to this RHI device
    const Model* source_ = nullptr; // borrowed, never dereference after unload
    const ModelAsset* asset_ = nullptr; // borrowed immutable CPU asset
    gfx::BufferHandle indexBuffer_{};
    // Dedicated GLES3/Vulkan sidecars borrow the source VB/IB, but do not
    // own a GeometryPool lease. Snapshot the original binding to reject
    // source unload/reupload even when callers forget a revision bump.
    gfx::BufferHandle sourceVertexBuffer_{}, sourceIndexBuffer_{};
    uint32_t sourceFirstIndex_ = 0;
    GeometryPoolIndexSpan pooledIndices_;
    std::unique_ptr<IndexResidency> streaming_;
    bool dedicated_ = false;
    uint32_t firstIndex_ = 0, indexCount_ = 0;
    uint32_t baseVertex_ = 0;
    uint64_t sourceGeometryRevision_ = 0;
    uint64_t sourceMaterialRevision_ = 0;
    std::vector<uint32_t> primitiveOffsets_;
    PortableRenderStats stats_{};
};

} // namespace aiverse::render::strata
