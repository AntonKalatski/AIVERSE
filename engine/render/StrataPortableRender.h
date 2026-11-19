#pragma once
// Strata portable indexed raster path for Metal, Vulkan, GLES3 / WebGL2
// and desktop GL. CPU cluster selection is an independent correctness path;
// GPU compute selection is optional and never required for raster support.
// One original source VB; auxiliary indices live in its GlobalGeometryPool
// when available or ONE dedicated index buffer otherwise. No second VB/pool.
#include "StrataModelAdapter.h"
#include "StrataView.h"
#include "GlobalGeometryPool.h"

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

    bool stage(gfx::Device& device, const Model& source,
               const ModelAsset& asset, std::string* error = nullptr);
    bool prepareModel(const mat4& instanceWorld,
                      const std::vector<PortablePassView>& passes,
                      bool forceFinestForUnspecifiedPass,
                      Model& output, std::string* error = nullptr);
    bool validFor(const Model& source) const;
    void shutdown(gfx::Device& device);
    const PortableRenderStats& stats() const { return stats_; }
    gfx::BufferHandle indexBuffer() const { return indexBuffer_; }
    uint32_t firstIndex() const { return firstIndex_; }
private:
    const Model* source_ = nullptr; // borrowed, never dereference after unload
    const ModelAsset* asset_ = nullptr; // borrowed immutable CPU asset
    gfx::BufferHandle indexBuffer_{};
    GeometryPoolIndexSpan pooledIndices_;
    bool dedicated_ = false;
    uint32_t firstIndex_ = 0, indexCount_ = 0;
    uint32_t baseVertex_ = 0;
    uint64_t sourceGeometryRevision_ = 0;
    uint64_t sourceMaterialRevision_ = 0;
    std::vector<uint32_t> primitiveOffsets_;
    PortableRenderStats stats_{};
};

} // namespace aiverse::render::strata
