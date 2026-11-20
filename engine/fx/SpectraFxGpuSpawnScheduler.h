// SpectraFX GPU emitter scheduler. Presentation-only, independent of genre,
// ECS authority and graphics backends. The caller provides a safe-point spawn
// callback; failed GPU allocations are dropped rather than replayed later.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include "../render/Math3D.h"
#include "ParticleSystem.h"

namespace aiverse::fx {

struct SpectraFxGpuSpawnSettings {
    float particlesPerSecond=0;
    float particlesPerMeter=0;
    uint32_t startupBurst=0;
    float lifetime=1;
    float speed=1;
    float spread=0;
    uint32_t seed=1;
    // Each frame's burst/rate emission is capped before GPU allocation.
    uint32_t maxSpawnsPerFrame=256;
};

struct SpectraFxGpuSpawnStats {
    uint64_t requested=0,accepted=0,dropped=0;
};

class SpectraFxGpuSpawnScheduler {
public:
    // Constructor does not perform actual GPU uploads.
    bool configure(const SpectraFxGpuSpawnSettings& settings) {
        if(!std::isfinite(settings.particlesPerSecond) ||
           !std::isfinite(settings.particlesPerMeter) ||
           !std::isfinite(settings.lifetime) || !std::isfinite(settings.speed) ||
           !std::isfinite(settings.spread) ||
           settings.particlesPerSecond<0 || settings.particlesPerSecond>100000 ||
           settings.particlesPerMeter<0 || settings.particlesPerMeter>100000 ||
           settings.lifetime<=0 || settings.lifetime>3600 ||
           settings.speed<0 || settings.speed>10000 ||
           settings.spread<0 || settings.spread>1 ||
           settings.maxSpawnsPerFrame==0 || settings.maxSpawnsPerFrame>65536)
            return false;
        settings_=settings;
        reset();
        return true;
    }
    void reset() {
        rateCarry_=distanceCarry_=0;
        rng_=settings_.seed ? settings_.seed : 1u;
        started_=false;
        previous_={};
        stats_={};
    }
    void pauseDistanceTracking(render::float3 pos) {
        previous_=pos;
        started_=true;
    }

    template<class Emit>
    bool tick(float dt,render::float3 position,render::float3 direction,
              float spawnScale,Emit&& emit) {
        if(!std::isfinite(dt) || dt<0 || dt>0.1f ||
           !finite(position) || !finite(direction) ||
           !std::isfinite(spawnScale) || spawnScale<0 || spawnScale>1)
            return false;
        const render::float3 start=started_ ? previous_ : position;
        const bool first=!started_;
        previous_=position;
        started_=true;
        float distance=0;
        if(!first) {
            const float dx=position.x-start.x,dy=position.y-start.y,dz=position.z-start.z;
            distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        }
        if(!std::isfinite(distance)) return false;

        const double rate=rateCarry_+double(dt)*settings_.particlesPerSecond*spawnScale;
        const double length=distanceCarry_+double(distance)*settings_.particlesPerMeter*spawnScale;
        const uint32_t rateCount=uint32_t(std::min(65536.0,std::floor(rate)));
        const uint32_t distanceCount=uint32_t(std::min(65536.0,std::floor(length)));
        rateCarry_=rate-std::floor(rate);
        distanceCarry_=length-std::floor(length);
        // Initial burst fires only on the first valid tick; no startup
        // teleport can accidentally generate an enormous distance burst.
        const uint64_t burst=first ? uint64_t(std::floor(double(settings_.startupBurst)*spawnScale)) : 0;
        const uint64_t desired=uint64_t(rateCount)+distanceCount+burst;
        const uint32_t capped=uint32_t(std::min<uint64_t>(desired,settings_.maxSpawnsPerFrame));
        stats_.requested+=desired;
        stats_.dropped+=desired-capped;
        const float lengthSquared=direction.x*direction.x+
                                  direction.y*direction.y+direction.z*direction.z;
        render::float3 dir{0,1,0};
        if(lengthSquared>1e-12f) {
            const float inv=1/std::sqrt(lengthSquared);
            dir={direction.x*inv,direction.y*inv,direction.z*inv};
        }
        for(uint32_t i=0;i<capped;++i) {
            // Rate and burst spawn at the current location; distance-born
            // particles interpolate along the last movement segment.
            render::float3 spawnPosition=position;
            if(i>=burst+rateCount && distanceCount) {
                const float t=(float(i-burst-rateCount)+0.5f)/float(distanceCount);
                spawnPosition={start.x+(position.x-start.x)*t,
                               start.y+(position.y-start.y)*t,
                               start.z+(position.z-start.z)*t};
            }
            const float jitter=settings_.spread;
            const render::float3 velocity={
                settings_.speed*(dir.x+jitter*(random01()*2-1)),
                settings_.speed*(dir.y+jitter*(random01()*2-1)),
                settings_.speed*(dir.z+jitter*(random01()*2-1))
            };
            if(emit(spawnPosition,velocity,settings_.lifetime)) ++stats_.accepted;
            else ++stats_.dropped;
        }
        return true;
    }

    const SpectraFxGpuSpawnStats& stats() const {return stats_;}
    const SpectraFxGpuSpawnSettings& settings() const {return settings_;}

private:
    static bool finite(render::float3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    }
    float random01() {
        rng_^=rng_<<13; rng_^=rng_>>17; rng_^=rng_<<5;
        return float(rng_&0x00ffffffu)/16777216.0f;
    }
    SpectraFxGpuSpawnSettings settings_{};
    SpectraFxGpuSpawnStats stats_{};
    render::float3 previous_{};
    double rateCarry_=0,distanceCarry_=0;
    uint32_t rng_=1;
    bool started_=false;
};

inline float spectraFxSpawnQualityScale(FxQuality quality) {
    switch(quality) {
    case FxQuality::Off: return 0;
    case FxQuality::Low: return 0.4f;
    case FxQuality::Medium: return 0.7f;
    case FxQuality::High: return 1.0f;
    }
    return 0;
}

} // namespace aiverse::fx
