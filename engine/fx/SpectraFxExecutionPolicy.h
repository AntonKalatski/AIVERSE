// SpectraFX presentation backend selection policy.
// This is an explicit, conservative opt-in gate, not automatic migration
// of existing CPU particle effects to a partial GPU implementation.
#pragma once

#include <cstdint>
#include "../gfx/Gfx.h"
#include "FxEffect.h"

namespace aiverse::fx {

enum class SpectraFxExecutionBackend : uint8_t { Cpu, Gpu };
enum class SpectraFxExecutionPreference : uint8_t { Cpu, Auto, Gpu };

enum class SpectraFxFeature : uint32_t {
    None = 0,
    Billboard = 1u << 0,
    Additive = 1u << 1,
    Alpha = 1u << 2,
    Collision = 1u << 3,
    Trails = 1u << 4,
    Mesh = 1u << 5,
    SubEmitters = 1u << 6,
    Lights = 1u << 7,
    Sorting = 1u << 8,
    LocalSpace = 1u << 9,
    Flipbook = 1u << 10,
    Ribbons = 1u << 11,
    PerParticleEvents = 1u << 12,
    GroundPlaneCollision = 1u << 13, // GPU-supported world Y plane only
    GpuImpactFeedback = 1u << 14, // optional Metal async ground-hit events
    GpuDepthSort = 1u << 15 // Metal bitonic back-to-front sorted index map
};
inline constexpr SpectraFxFeature operator|(SpectraFxFeature a,SpectraFxFeature b) {
    return SpectraFxFeature(uint32_t(a)|uint32_t(b));
}
inline constexpr bool hasFeature(SpectraFxFeature mask,SpectraFxFeature feature) {
    return (uint32_t(mask)&uint32_t(feature))!=0;
}

struct SpectraFxBackendRequest {
    SpectraFxExecutionPreference preference = SpectraFxExecutionPreference::Auto;
    SpectraFxFeature required = SpectraFxFeature::Billboard;
    uint32_t particles = 0;
    bool allowCpuFallback = true;
};
struct SpectraFxBackendDecision {
    SpectraFxExecutionBackend backend = SpectraFxExecutionBackend::Cpu;
    bool accepted = false;
    const char* reason = "not evaluated";
};

// The implemented GPU backend currently handles only simple world-space,
// unsorted alpha/additive billboards and simple world-Y ground plane bounce.
// Native GPU execution is currently Metal-only. Vulkan has no implemented
// compute entry points in this AIVERSE RHI; all other RHI backends stay CPU.
// Mesh/scene/terrain/depth collisions still stay CPU.
inline SpectraFxBackendDecision chooseSpectraFxBackend(
        const gfx::Device& device,const SpectraFxBackendRequest& request) {
    const auto cpu = [&](const char* reason) -> SpectraFxBackendDecision {
        // Explicit CPU mode is not a fallback. Auto/Gpu must honour the
        // authored allowCpuFallback flag even if no GPU backend is available.
        // Previously Auto accidentally accepted CPU with fallback disabled.
        return {SpectraFxExecutionBackend::Cpu,
                request.preference==SpectraFxExecutionPreference::Cpu ||
                request.allowCpuFallback,
                reason};
    };
    if(request.preference==SpectraFxExecutionPreference::Cpu)
        return cpu("CPU selected explicitly");
    if(request.particles==0 || request.particles>65536)
        return cpu("GPU particle capacity must be 1..65536");
    const uint32_t mask=uint32_t(request.required);
    const uint32_t allowed=uint32_t(SpectraFxFeature::Billboard) |
                           uint32_t(SpectraFxFeature::Alpha) |
                           uint32_t(SpectraFxFeature::Additive) |
                           uint32_t(SpectraFxFeature::GroundPlaneCollision) |
                           uint32_t(SpectraFxFeature::GpuImpactFeedback) |
                           uint32_t(SpectraFxFeature::Sorting) |
                           uint32_t(SpectraFxFeature::GpuDepthSort);
    if((mask&~allowed)!=0 || !hasFeature(request.required,SpectraFxFeature::Billboard))
        return cpu("requested particle modules require CPU fallback");
    if(!device.gpuHealthy())
        return cpu("RHI GPU reported unhealthy; use authored CPU effect");
    // As of this branch's RHI inspection, VulkanDevice does not implement
    // the Device compute entry points. A future Vulkan compute backend
    // must separately validate persistent GPU-writable storage, barriers,
    // frame-in-flight uploads and sampled RGBA32F output before enabling
    // this path. Do not silently turn GPU particles on when that changes.
    const auto backend=device.info().backend;
    if(backend==gfx::Backend::Vulkan)
        return cpu("SpectraFX Vulkan GPU evolution requires native RHI validation");
    if(!device.supportsCompute() || !device.supports(gfx::PixelFormat::RGBA32F))
        return cpu("RHI compute or RGBA32F storage unavailable");
    const bool depthSort=hasFeature(request.required,SpectraFxFeature::GpuDepthSort);
    const bool needsSort=hasFeature(request.required,SpectraFxFeature::Sorting);
    if(depthSort && (!needsSort ||
                     !hasFeature(request.required,SpectraFxFeature::Alpha)))
        return cpu("GPU depth sort only supports alpha billboard effects");
    if(needsSort && !depthSort)
        return cpu("GPU alpha requires explicit back-to-front depth sorting");
    if(depthSort && (backend!=gfx::Backend::Metal ||
                     request.particles>4096 || !device.frameSerial()))
        return cpu("GPU alpha depth sorting requires Metal and <=4096 slots");
    if(hasFeature(request.required,SpectraFxFeature::GpuImpactFeedback) &&
       (backend!=gfx::Backend::Metal || request.particles>4096))
        return cpu("GPU impact feedback requires Metal and <=4096 particles");
    if(backend!=gfx::Backend::Metal)
        return cpu("SpectraFX GPU evolution is currently Metal-only");
    return {SpectraFxExecutionBackend::Gpu,true,
            depthSort?"Metal GPU-sorted alpha eligible":"GPU billboard backend eligible"};
}

} // namespace aiverse::fx
