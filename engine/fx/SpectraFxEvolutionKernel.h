// SpectraFX particle evolution kernel, C++ twin of the Metal/GLSL compute
// kernel in SpectraFxComputeShaders.h (kSpectraFxComputeMetal / GLSL).
//
// ONE authored emitter asset (fx/gpu/<name>.json) runs either on the GPU
// compute kernel or, when compute is unavailable or the asset/host asks for
// the CPU, on this function. Both paths receive the same CPU-generated seeds
// (SpectraFxGpuSpawnScheduler) and render through the same billboard
// renderer, so CPU and GPU results agree within float tolerance
// (tests/spectrafx_cpu_gpu_parity.mm checks this on a real Metal device).
//
// Keep this function and the shader strings in lockstep: every arithmetic
// step below mirrors one line of cs_main, in the same order.
#pragma once
#include <algorithm>
#include "../render/Math3D.h"

namespace aiverse::fx {

struct SpectraFxEvolutionParams {
    float dt = 0;
    float damping = 1;
    render::float3 acceleration{0, -9.81f, 0};
    bool collideGround = false;
    float groundHeight = 0;
    float restitution = 0.35f;
    float tangentRetention = 0.8f;
};

// positionAge = xyz position, w age; velocityLife = xyz velocity, w lifetime.
// Returns true when the particle hit the ground plane during this step.
// `remaining` receives the value the GPU writes into the position texture's
// alpha channel: (life-age)/life clamped to 0..1, or 0 for dormant slots.
inline bool spectraFxEvolveParticle(render::float4& a, render::float4& v,
                                    const SpectraFxEvolutionParams& p,
                                    float& remaining) {
    bool hitGround = false;
    if (v.w > 0.0f && a.w < v.w) {
        float nx = (v.x + p.acceleration.x * p.dt) * p.damping;
        float ny = (v.y + p.acceleration.y * p.dt) * p.damping;
        float nz = (v.z + p.acceleration.z * p.dt) * p.damping;
        a.x += nx * p.dt;
        a.y += ny * p.dt;
        a.z += nz * p.dt;
        if (p.collideGround && a.y < p.groundHeight) {
            a.y = p.groundHeight;
            if (ny < 0.0f) {
                hitGround = true;
                ny = -ny * p.restitution;
                nx *= p.tangentRetention;
                nz *= p.tangentRetention;
            }
        }
        a.w = std::min(a.w + p.dt, v.w);
        v.x = nx; v.y = ny; v.z = nz;
    }
    remaining = v.w > 0.0f ? std::clamp((v.w - a.w) / v.w, 0.0f, 1.0f) : 0.0f;
    return hitGround;
}

} // namespace aiverse::fx
