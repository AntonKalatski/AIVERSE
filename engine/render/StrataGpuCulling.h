#pragma once
// Experimental Metal Strata selection: GPU-visible indexed ranges only.
// Deliberately NO draw submission, GPU readback or ECS/world ownership.
// The shared Renderer3D remains the only authority for rendering.
#include "StrataGpuScene.h"
#include "StrataView.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aiverse::render::strata {

// Object-to-world must include the Model node's bind transform. The caller
// supplies the same per-pass view/projection as used by CPU planView.
struct alignas(16) GpuSceneInstance {
    float objectToWorld[16] = {};
    uint32_t primitiveIndex = 0; // index within GpuMetadataHeader::primitiveCount
    uint32_t enabled = 1;
    uint32_t instanceToken = 0; // caller-owned stable diagnostic ID
    uint32_t reserved = 0;
};
static_assert(sizeof(GpuSceneInstance) == 80);
static_assert(alignof(GpuSceneInstance) == 16);

// The CPU oracle uses (pass viewProjection * this instance transform).
// Do not pass a precombined objectToClip matrix to the GPU dispatch.
GpuSceneInstance makeGpuSceneInstance(const mat4& objectToWorld,
                                     uint32_t primitiveIndex,
                                     uint32_t instanceToken,
                                     bool enabled = true);

struct GpuPassConfig {
    mat4 viewProjection{};
    int width = 0, height = 0;
    bool clipDepthZeroToOne = true;
    bool frustumCull = true;
    float errorThresholdPixels = 1.f;
    uint32_t maxOutputClusters = 65536;
};
ViewConfig cpuReferenceView(const GpuPassConfig& pass,
                            const GpuSceneInstance& instance);

// Output is in arbitrary GPU atomic order. Each range is an absolute pooled
// index span (metadata v2 primitive base + node local firstIndex) and never
// carries material/transform state sufficient to call drawIndexed by itself.
struct alignas(16) GpuSelectedCluster {
    uint32_t instanceToken = 0;
    uint32_t gpuFirstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialGroup = 0;
    uint32_t nodeIndex = 0;
    uint32_t primitiveIndex = 0;
    uint32_t localFirstIndex = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(GpuSelectedCluster) == 32);

struct alignas(16) GpuSelectionCounters {
    uint32_t produced = 0; // may exceed output capacity when overflow occurs
    uint32_t overflow = 0; // output incomplete: NEVER submit these results
    uint32_t examined = 0; // diagnostic GPU node visits
    uint32_t invalid = 0;  // unexpected metadata / bounded stack / pass input
};
static_assert(sizeof(GpuSelectionCounters) == 16);

struct alignas(16) GpuSelectionUniforms {
    float viewProjection[16] = {};
    uint32_t instanceCount = 0;
    uint32_t outputCapacity = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    float thresholdPixels = 1.f;
    uint32_t clipDepthZeroToOne = 1;
    uint32_t frustumCull = 1;
    uint32_t reserved = 0;
};
static_assert(sizeof(GpuSelectionUniforms) == 96);

struct GpuSelectionSubmission {
    gfx::BufferHandle ranges;
    gfx::BufferHandle counters;
    uint32_t capacity = 0;
    uint32_t submittedInstances = 0;
    uint64_t frameSerial = 0;
    size_t submission = 0;
    bool valid() const {
        return ranges.valid() && counters.valid() && capacity != 0 &&
               frameSerial != 0;
    }
};

// One diagnostic selection dispatch per source and view; caller must retain
// the source's staged metadata+pooled index leases until GPU completion.
// No indirect command buffer is produced or consumed here.
class GpuClusterCulling {
public:
    GpuClusterCulling() = default;
    GpuClusterCulling(const GpuClusterCulling&) = delete;
    GpuClusterCulling& operator=(const GpuClusterCulling&) = delete;

    bool dispatch(gfx::Device& dev, gfx::BufferHandle gpuMetadataV2,
                  const std::vector<GpuSceneInstance>& instances,
                  const GpuPassConfig& config, uint64_t frameSerial,
                  GpuSelectionSubmission& out);
    void shutdown(gfx::Device& dev);
private:
    struct Buffers {
        gfx::BufferHandle instances, ranges, counters;
        size_t instancesBytes = 0, rangesBytes = 0;
    };
    static constexpr size_t kFramesInFlight = 3;
    static constexpr size_t kMaxSubmissionsPerFrame = 64;
    std::array<std::vector<Buffers>, kFramesInFlight> slots_{};
    std::array<uint64_t, kFramesInFlight> epochs_{};
    std::array<size_t, kFramesInFlight> cursors_{};
    gfx::ComputeHandle pipeline_{};
    static bool allocate(gfx::Device& dev, gfx::BufferHandle& handle,
                         size_t& capacity, size_t requested, const char* label);
};
} // namespace aiverse::render::strata
