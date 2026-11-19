#include "StrataGpuCulling.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace aiverse::render::strata {
namespace {
// GPU selection is diagnostic until it is proven equivalent to CPU planView.
// Metal-only first path: other RHI implementations must return false safely.
constexpr const char* kMetalCulling = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Header {
    uint version, primitiveCount, nodeCount, rootCount;
    uint primitivesOffset, nodesOffset, rootsOffset, reserved;
};
struct Primitive {
    uint mesh, primitive, materialGroup, nodeBase;
    uint nodeCount, rootBase, rootCount, gpuFirstIndex;
};
struct Node {
    float4 lo, hi;
    uint firstChild, childCount, sourceFirstIndex, sourceIndexCount;
    uint materialGroup, resident;
    float geometricError;
    uint reserved;
};
struct Instance {
    float4 c0, c1, c2, c3;
    uint primitiveIndex, enabled, instanceToken, reserved;
};
struct Selected {
    uint instanceToken, gpuFirstIndex, indexCount, materialGroup;
    uint nodeIndex, primitiveIndex, localFirstIndex, reserved;
};
struct Params {
    float4x4 viewProjection;
    uint instanceCount, outputCapacity, width, height;
    float thresholdPixels;
    uint clipDepthZeroToOne, frustumCull, reserved;
};

inline float projectError(float4x4 clip, Node node, Params params) {
    float nearW = 1.0e30f, maxX = 0.0f, maxY = 0.0f;
    for (uint c = 0; c < 8; ++c) {
        float3 p = float3((c & 1) ? node.hi.x : node.lo.x,
                          (c & 2) ? node.hi.y : node.lo.y,
                          (c & 4) ? node.hi.z : node.lo.z);
        float4 q = clip * float4(p, 1.0f);
        if (!all(isfinite(q)) || q.w <= 0.0f ||
            q.z < (params.clipDepthZeroToOne ? 0.0f : -q.w))
            return 1.0e30f; // conservative near/behind camera
        nearW = min(nearW, q.w);
        maxX = max(maxX, abs(q.x));
        maxY = max(maxY, abs(q.y));
    }
    float error = node.geometricError;
    float nx = length(float3(clip[0][0], clip[1][0], clip[2][0]));
    float ny = length(float3(clip[0][1], clip[1][1], clip[2][1]));
    float nw = length(float3(clip[0][3], clip[1][3], clip[2][3]));
    float delta = nw * error;
    if (nearW <= delta) return 1.0e30f;
    float pixels = 0.5f * max(
        float(params.width) * error * (nx * nearW + maxX * nw),
        float(params.height) * error * (ny * nearW + maxY * nw)) /
        (nearW * (nearW - delta));
    return isfinite(pixels) ? pixels : 1.0e30f;
}
inline bool outsideFrustum(float4x4 clip, Node n) {
    uint allOutside = 63u;
    for (uint c = 0; c < 8; ++c) {
        float3 p = float3((c & 1) ? n.hi.x : n.lo.x,
                          (c & 2) ? n.hi.y : n.lo.y,
                          (c & 4) ? n.hi.z : n.lo.z);
        float4 q = clip * float4(p, 1.0f);
        if (!all(isfinite(q))) return false;
        uint outside = 0;
        if (q.x < -q.w) outside |= 1u;
        if (q.x >  q.w) outside |= 2u;
        if (q.y < -q.w) outside |= 4u;
        if (q.y >  q.w) outside |= 8u;
        if (q.z < -q.w) outside |= 16u; // match current geometryBoxVisible oracle
        if (q.z >  q.w) outside |= 32u;
        allOutside &= outside;
    }
    return allOutside != 0;
}
inline void markInvalid(device atomic_uint* counters) {
    atomic_fetch_or_explicit(counters + 3, 1u, memory_order_relaxed);
}
kernel void cs_main(device const uchar* metadata [[buffer(0)]],
                    device const Instance* instances [[buffer(1)]],
                    device Selected* output [[buffer(2)]],
                    device atomic_uint* counters [[buffer(3)]],
                    constant Params& params [[buffer(8)]],
                    uint tid [[thread_position_in_grid]]) {
    if (tid >= params.instanceCount) return;
    device const Header* header = (device const Header*)metadata;
    if (header->version != 2u || header->primitiveCount == 0 ||
        header->nodeCount == 0 || header->rootCount == 0) {
        markInvalid(counters);
        return;
    }
    const Instance inst = instances[tid];
    if (!inst.enabled) return;
    if (inst.primitiveIndex >= header->primitiveCount) {
        markInvalid(counters);
        return;
    }
    device const Primitive* primitives =
        (device const Primitive*)(metadata + header->primitivesOffset);
    device const Node* nodes =
        (device const Node*)(metadata + header->nodesOffset);
    device const uint* roots =
        (device const uint*)(metadata + header->rootsOffset);
    const Primitive primitive = primitives[inst.primitiveIndex];
    if (primitive.gpuFirstIndex == 0xffffffffu ||
        primitive.rootBase > header->rootCount ||
        primitive.rootCount > header->rootCount - primitive.rootBase ||
        primitive.nodeBase > header->nodeCount ||
        primitive.nodeCount > header->nodeCount - primitive.nodeBase) {
        markInvalid(counters);
        return;
    }
    const float4x4 objectToClip = params.viewProjection *
        float4x4(inst.c0, inst.c1, inst.c2, inst.c3);

    // One thread traverses one material primitive instance. No shared queue
    // or unbounded recursion. Parent fallback occurs when ANY child is absent.
    for (uint ri = 0; ri < primitive.rootCount; ++ri) {
        uint stack[128];
        uint top = 0;
        const uint root = roots[primitive.rootBase + ri];
        if (root < primitive.nodeBase ||
            root >= primitive.nodeBase + primitive.nodeCount) {
            markInvalid(counters);
            return;
        }
        stack[top++] = root;
        uint visited = 0;
        while (top) {
            if (++visited > primitive.nodeCount) {
                markInvalid(counters);
                return; // corrupt graph / unexpected cycle
            }
            const uint id = stack[--top];
            if (id >= header->nodeCount) {
                markInvalid(counters);
                return;
            }
            const Node node = nodes[id];
            atomic_fetch_add_explicit(counters + 2, 1u, memory_order_relaxed);
            if (!node.resident) {
                markInvalid(counters); // parent/root residency promise broken
                return;
            }
            bool refine = node.childCount != 0 &&
                projectError(objectToClip, node, params) > params.thresholdPixels;
            if (refine) {
                if (node.firstChild < primitive.nodeBase ||
                    node.firstChild >= primitive.nodeBase + primitive.nodeCount ||
                    node.childCount > primitive.nodeBase +
                        primitive.nodeCount - node.firstChild) {
                    markInvalid(counters);
                    return;
                }
                for (uint c = 0; c < node.childCount; ++c)
                    if (!nodes[node.firstChild + c].resident) {
                        refine = false;
                        break;
                    }
                if (refine && top + node.childCount > 128u) {
                    markInvalid(counters); // NO truncated output may be rendered
                    return;
                }
            }
            if (refine) {
                for (uint c = node.childCount; c > 0; --c)
                    stack[top++] = node.firstChild + c - 1u;
                continue;
            }
            if (params.frustumCull &&
                outsideFrustum(objectToClip, node))
                continue;
            if (!node.sourceIndexCount ||
                primitive.gpuFirstIndex >
                    0xffffffffu - node.sourceFirstIndex) {
                markInvalid(counters);
                return;
            }
            uint at = atomic_fetch_add_explicit(
                counters, 1u, memory_order_relaxed);
            if (at >= params.outputCapacity) {
                atomic_fetch_or_explicit(counters + 1, 1u,
                                         memory_order_relaxed);
                continue;
            }
            Selected selected;
            selected.instanceToken = inst.instanceToken;
            selected.gpuFirstIndex =
                primitive.gpuFirstIndex + node.sourceFirstIndex;
            selected.indexCount = node.sourceIndexCount;
            selected.materialGroup = node.materialGroup;
            selected.nodeIndex = id;
            selected.primitiveIndex = inst.primitiveIndex;
            selected.localFirstIndex = node.sourceFirstIndex;
            selected.reserved = 0;
            output[at] = selected;
        }
    }
}
)MSL";
} // namespace

bool GpuClusterCulling::allocate(gfx::Device& dev, gfx::BufferHandle& h,
                                 size_t& bytes, size_t needed,
                                 const char* label) {
    if (h.valid() && bytes >= needed) return true;
    gfx::BufferDesc desc{};
    desc.type = gfx::BufferType::Storage;
    desc.usage = gfx::Usage::Immutable;
    desc.size = needed;
    desc.label = label;
    const gfx::BufferHandle created = dev.createBuffer(desc);
    if (!created.valid()) return false;
    if (h.valid()) dev.destroyBuffer(h);
    h = created;
    bytes = needed;
    return true;
}

bool GpuClusterCulling::dispatch(gfx::Device& dev,
                                gfx::BufferHandle gpuMetadataV2,
                                const std::vector<GpuSceneInstance>& instances,
                                const ViewConfig& config, uint64_t frameSerial,
                                GpuSelectionSubmission& out) {
    if (dev.info().backend != gfx::Backend::Metal ||
        !dev.supportsCompute() || !gpuMetadataV2.valid() ||
        !frameSerial || instances.empty() || instances.size() > 4096 ||
        config.maxSelectedNodes == 0 || config.maxSelectedNodes > 65536 ||
        config.width <= 0 || config.height <= 0 ||
        config.width > 32768 || config.height > 32768 ||
        !std::isfinite(config.errorThresholdPixels) ||
        config.errorThresholdPixels <= 0.f ||
        config.errorThresholdPixels > 10000.f)
        return false;
    for (float v : config.objectToClip.m)
        if (!std::isfinite(v)) return false;
    for (const auto& inst : instances) {
        for (float v : inst.objectToWorld)
            if (!std::isfinite(v)) return false;
    }
    if (!pipeline_.valid()) {
        pipeline_ = dev.createCompute({kMetalCulling, nullptr,
                                       "Strata diagnostic GPU cluster culling"});
        if (!pipeline_.valid()) return false;
    }
    const size_t index = frameSerial % kFramesInFlight;
    if (epochs_[index] != frameSerial) {
        // Backend must guarantee a completed frame slot before reuse, like
        // the existing GpuSceneDrawCommands 3-frame-slot implementation.
        if (epochs_[index] != 0 && dev.completedFrameSerial() != 0 &&
            dev.completedFrameSerial() < epochs_[index]) return false;
        epochs_[index] = frameSerial;
        cursors_[index] = 0;
    }
    const size_t submission = cursors_[index];
    if (submission >= kMaxSubmissionsPerFrame) return false;
    auto& pool = slots_[index];
    if (submission == pool.size()) pool.emplace_back();
    auto& slot = pool[submission];
    const size_t inputBytes = instances.size() * sizeof(GpuSceneInstance);
    const size_t outputBytes =
        size_t(config.maxSelectedNodes) * sizeof(GpuSelectedCluster);
    if (!allocate(dev, slot.instances, slot.instancesBytes, inputBytes,
                  "Strata diagnostic GPU instances") ||
        !allocate(dev, slot.ranges, slot.rangesBytes, outputBytes,
                  "Strata diagnostic visible cluster ranges")) return false;
    if (!slot.counters.valid()) {
        gfx::BufferDesc desc{};
        desc.type = gfx::BufferType::Storage;
        desc.usage = gfx::Usage::Immutable;
        desc.size = sizeof(GpuSelectionCounters);
        desc.label = "Strata diagnostic GPU counters";
        slot.counters = dev.createBuffer(desc);
        if (!slot.counters.valid()) return false;
    }
    const GpuSelectionCounters zero{};
    dev.updateBuffer(slot.instances, instances.data(), inputBytes);
    dev.updateBuffer(slot.counters, &zero, sizeof(zero));
    if (!dev.beginCompute(pipeline_)) return false;
    dev.setComputeBuffer(0, gpuMetadataV2);
    dev.setComputeBuffer(1, slot.instances);
    dev.setComputeBuffer(2, slot.ranges);
    dev.setComputeBuffer(3, slot.counters);
    GpuSelectionUniforms uniforms{};
    std::memcpy(uniforms.viewProjection, config.objectToClip.m,
                sizeof(uniforms.viewProjection));
    uniforms.instanceCount = uint32_t(instances.size());
    uniforms.outputCapacity = config.maxSelectedNodes;
    uniforms.width = uint32_t(config.width);
    uniforms.height = uint32_t(config.height);
    uniforms.thresholdPixels = config.errorThresholdPixels;
    uniforms.clipDepthZeroToOne = config.clipDepthZeroToOne ? 1u : 0u;
    uniforms.frustumCull = config.frustumCull ? 1u : 0u;
    dev.setComputeUniforms(&uniforms, sizeof(uniforms));
    dev.dispatch(uint32_t(instances.size()));
    dev.endCompute();
    GpuSelectionSubmission ready{};
    ready.ranges = slot.ranges;
    ready.counters = slot.counters;
    ready.capacity = config.maxSelectedNodes;
    ready.submittedInstances = uint32_t(instances.size());
    ready.frameSerial = frameSerial;
    ready.submission = submission;
    ++cursors_[index];
    out = ready;
    return true;
}

void GpuClusterCulling::shutdown(gfx::Device& dev) {
    for (auto& frame : slots_) {
        for (auto& slot : frame) {
            if (slot.instances.valid()) dev.destroyBuffer(slot.instances);
            if (slot.ranges.valid()) dev.destroyBuffer(slot.ranges);
            if (slot.counters.valid()) dev.destroyBuffer(slot.counters);
        }
        frame.clear();
    }
    epochs_.fill(0);
    cursors_.fill(0);
    if (pipeline_.valid()) dev.destroyCompute(pipeline_);
    pipeline_ = {};
}
} // namespace aiverse::render::strata
