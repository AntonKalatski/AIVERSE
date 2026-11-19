#pragma once
// PRISMA Stage 45. CPU-only hysteresis for presentation render scale.
// Only samples explicitly provided verified GPU frame durations; never uses
// CPU frame time, FPS or a simulated GPU completion token.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::render {
struct PrismaAdaptiveResolution {
    float scale = 1.0f;
    uint64_t lastCompletedFence = 0;
    unsigned slowSamples = 0, fastSamples = 0, cooldown = 0;
    bool initialized = false;

    void reset(float authoredScale) {
        scale = quantize(authoredScale);
        slowSamples = fastSamples = cooldown = 0;
        lastCompletedFence = 0;
        initialized = true;
    }
    static float quantize(float s) {
        return std::round(std::clamp(s,0.5f,1.0f)*16.0f)/16.0f;
    }
    // 'ceiling' is an author-chosen renderScale. The controller never raises
    // fidelity beyond it and never drops below the configured floor.
    // A true completed fence advancing is required for every new sample:
    // duplicate last-completed GPU timings must NOT be counted repeatedly.
    bool observe(double gpuMs, uint64_t completedFence, float targetGpuMs,
                 float floor, float ceiling) {
        if (!initialized) reset(ceiling);
        const float hi=quantize(ceiling);
        const float lo=std::min(hi,quantize(floor));
        scale=std::clamp(scale,lo,hi);
        if (!completedFence || completedFence<=lastCompletedFence ||
            !std::isfinite(gpuMs) || gpuMs<=0.0 || gpuMs>250.0 ||
            !std::isfinite(targetGpuMs) || targetGpuMs<4.0f ||
            targetGpuMs>50.0f) return false;
        lastCompletedFence=completedFence;
        if (cooldown) { --cooldown; slowSamples=fastSamples=0; return false; }
        // 15% slowdown is required for a downward step, but recovery
        // requires 20% GPU headroom for considerably longer.
        if (gpuMs>double(targetGpuMs)*1.15) {
            ++slowSamples; fastSamples=0;
        } else if (gpuMs<double(targetGpuMs)*0.80) {
            ++fastSamples;slowSamples=0;
        } else { slowSamples=fastSamples=0; }
        float next=scale;
        if (slowSamples>=12) next=std::max(lo,scale-1.0f/16.0f);
        if (fastSamples>=36) next=std::min(hi,scale+1.0f/16.0f);
        if (next==scale) return false;
        scale=next;
        cooldown=48;
        slowSamples=fastSamples=0;
        return true;
    }
};
} // namespace aiverse::render
