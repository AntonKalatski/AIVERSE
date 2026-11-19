// SpectraFX opt-in presentation GPU frame-time governor.
// The RHI's gpuFrameMs() is a WHOLE-FRAME GPU sample, not an isolated VFX
// timer. Never infer SpectraFX GPU cost or alter gameplay simulation from it.
// User/project opt-in only: unsupported timing (<=0) preserves scale 1.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::fx {

class SpectraFxGpuBudgetGovernor {
public:
    struct Config {
        float targetFrameMs=0;       // 0 = disabled (default)
        float minSpawnScale=0.25f;
        float throttlePerSecond=1.0f;
        float recoverPerSecond=0.12f;
        float smoothSeconds=0.5f;
    };
    struct Stats {
        uint64_t samples=0, throttles=0, recoveries=0, invalidSamples=0;
        float filteredFrameMs=0;
        float spawnScale=1;
    };
    bool configure(const Config& cfg) {
        if(!finite(cfg.targetFrameMs) || cfg.targetFrameMs<0 ||
           cfg.targetFrameMs>1000 || !finite(cfg.minSpawnScale) ||
           cfg.minSpawnScale<0 || cfg.minSpawnScale>1 ||
           !finite(cfg.throttlePerSecond) || cfg.throttlePerSecond<0 ||
           cfg.throttlePerSecond>100 ||
           !finite(cfg.recoverPerSecond) || cfg.recoverPerSecond<0 ||
           cfg.recoverPerSecond>100 ||
           !finite(cfg.smoothSeconds) || cfg.smoothSeconds<=0 ||
           cfg.smoothSeconds>60)
            return false;
        config_=cfg;
        reset();
        return true;
    }
    void reset() {
        stats_={};
        lastSampleValid_=false;
    }
    bool enabled() const {return config_.targetFrameMs>0;}
    float spawnScale() const {return stats_.spawnScale;}
    const Stats& stats() const {return stats_;}
    const Config& config() const {return config_;}

    void observe(float gpuFrameMs,float dt) {
        if(!enabled()) return;
        if(!finite(dt)||dt<0||dt>0.25f) {
            ++stats_.invalidSamples;return;
        }
        if(dt==0) return; // paused or editor single-step
        if(!finite(gpuFrameMs)||gpuFrameMs<=0||gpuFrameMs>1000) {
            // Device does not expose timing / returned no completed GPU
            // sample. Do not throttle blindly when timing is unavailable.
            ++stats_.invalidSamples;
            return;
        }
        const float alpha=1.0f-std::exp(-dt/config_.smoothSeconds);
        if(!lastSampleValid_) {
            stats_.filteredFrameMs=gpuFrameMs;
            lastSampleValid_=true;
        } else {
            stats_.filteredFrameMs+=
                alpha*(gpuFrameMs-stats_.filteredFrameMs);
        }
        ++stats_.samples;
        // Wide hysteresis prevents oscillations near the target and allows
        // transient GPU spikes to decay before raising visual spawn density.
        if(stats_.filteredFrameMs>config_.targetFrameMs*1.1f) {
            stats_.spawnScale=std::max(config_.minSpawnScale,
                stats_.spawnScale-config_.throttlePerSecond*dt);
            ++stats_.throttles;
        } else if(stats_.filteredFrameMs<config_.targetFrameMs*0.8f) {
            stats_.spawnScale=std::min(1.0f,
                stats_.spawnScale+config_.recoverPerSecond*dt);
            ++stats_.recoveries;
        }
    }
private:
    static bool finite(float value) {return std::isfinite(value);}
    Config config_{};
    Stats stats_{};
    bool lastSampleValid_=false;
};

} // namespace aiverse::fx
