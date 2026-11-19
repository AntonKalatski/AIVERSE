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
    GroundPlaneCollision = 1u << 13 // GPU-supported world Y plane only
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
// Mesh/scene/terrain/depth collisions still stay CPU.
// No D3D12 graphics backend exists in the current AIVERSE RHI.
inline SpectraFxBackendDecision chooseSpectraFxBackend(
        const gfx::Device& device,const SpectraFxBackendRequest& request) {
    const auto cpu = [&](const char* reason) -> SpectraFxBackendDecision {
        return {SpectraFxExecutionBackend::Cpu,
                request.preference!=SpectraFxExecutionPreference::Gpu ||
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
                           uint32_t(SpectraFxFeature::GroundPlaneCollision);
    if((mask&~allowed)!=0 || !hasFeature(request.required,SpectraFxFeature::Billboard))
        return cpu("requested particle modules require CPU fallback");
    if(!device.supportsCompute() || !device.supports(gfx::PixelFormat::RGBA32F))
        return cpu("RHI compute or RGBA32F storage unavailable");
    const auto backend=device.info().backend;
    if(backend!=gfx::Backend::Metal && backend!=gfx::Backend::Vulkan)
        return cpu("GPU particle shaders are not supported on this RHI backend");
    return {SpectraFxExecutionBackend::Gpu,true,"GPU billboard backend eligible"};
}

} // namespace aiverse::fx
