// SpectraFX opt-in homogeneous shared GPU particle arena.
//
// Several independent presentation emitters share ONE GPU particle storage
// pool, ONE evolution dispatch and ONE billboard draw. Each emitter still
// owns a spawn scheduler, pose and generation-safe CPU slot handles.
// Only effects with identical appearance and physical environment can
// inhabit an arena; heterogeneous effects remain in HybridPlayback.
// No authoritative ECS or network/save state is read or modified.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "SpectraFxGpuParticleSlots.h"
#include "SpectraFxGpuSpawnScheduler.h"
#include "SpectraFxGpuLod.h"
#include "SpectraFxGpuBillboardRenderer.h"
#include "SpectraFxGpuCompaction.h"

namespace aiverse::fx {

class SpectraFxGpuSharedArena {
public:
    static constexpr size_t kMaxEmitterSources = 64;
    struct Config {
        uint32_t particles = 8192;
        SpectraFxGpuEnvironment environment{};
        SpectraFxGpuBillboardRenderer::Appearance appearance{};
        bool gpuIndirectCompaction = false;
    };
    struct EmitterConfig {
        SpectraFxGpuSpawnSettings spawn{};
        SpectraFxGpuLodSettings lod{};
        FxQuality quality = FxQuality::High;
    };
    struct Stats {
        uint64_t created = 0, removed = 0;
        uint64_t submitted = 0, rejected = 0;
        uint64_t gpuSteps = 0, gpuCompacted = 0, indirectUnavailable = 0;
    };

    SpectraFxGpuSharedArena() = default;
    SpectraFxGpuSharedArena(const SpectraFxGpuSharedArena&) = delete;
    SpectraFxGpuSharedArena& operator=(const SpectraFxGpuSharedArena&) = delete;
    ~SpectraFxGpuSharedArena() = default; // call shutdown before device destruction

    bool init(gfx::Device& device,Config config,
              gfx::ComputeHandle sharedEvolution={}) {
        shutdown();
        error_.clear();
        const auto& a=config.appearance;
        const auto validColor=[](render::float3 v) {
            return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z) &&
                   v.x>=0 && v.y>=0 && v.z>=0 &&
                   v.x<=8 && v.y<=8 && v.z<=8;
        };
        if(!config.particles || config.particles>SpectraFxGpuEvolution::kMaxParticles ||
           !std::isfinite(a.radius) || a.radius<=0 || a.radius>1000 ||
           !validColor(a.color)) {
            error_="SpectraFX shared arena capacity/appearance invalid";
            return false;
        }
        if(!slots_.init(device,config.particles,
                        config.environment.reportImpacts,
                        sharedEvolution)) {
            error_="SpectraFX shared arena GPU storage initialization failed";
            return false;
        }
        if(!renderer_.init(device)) {
            error_=renderer_.lastError();
            slots_.shutdown();
            return false;
        }
        if(config.gpuIndirectCompaction && !compact_.init(device,config.particles)) {
            // Keep the shared GPU arena usable even on Vulkan or when
            // Metal indirect compute resources cannot be created.
            ++stats_.indirectUnavailable;
        }
        device_=&device;
        config_=std::move(config);
        return true;
    }

    bool addEmitter(uint64_t generationSafeKey,EmitterConfig config,
                    render::float3 position={},render::float3 direction={0,1,0}) {
        if(!ready() || !generationSafeKey ||
           emitters_.size()>=kMaxEmitterSources ||
           emitters_.find(generationSafeKey)!=emitters_.end() ||
           !config.lod.validate() || !finite(position) || !finite(direction))
            return false;
        Emitter e;
        if(!e.scheduler.configure(config.spawn)) return false;
        e.config=std::move(config);
        e.position=position;
        e.direction=direction;
        emitters_.emplace(generationSafeKey,std::move(e));
        ++stats_.created;
        return true;
    }

    bool moveEmitter(uint64_t key,render::float3 position,
                     render::float3 direction={0,1,0}) {
        auto it=emitters_.find(key);
        if(it==emitters_.end() || !finite(position) || !finite(direction))
            return false;
        it->second.position=position;
        it->second.direction=direction;
        return true;
    }

    // If killExisting=false, old particles finish naturally after the source
    // is detached (their GPU slots retire automatically). Kill requires a
    // shared-buffer-safe point, outside compute and render encoders.
    bool removeEmitter(uint64_t key,bool killExisting=false) {
        auto it=emitters_.find(key);
        if(it==emitters_.end()) return false;
        if(killExisting) {
            if(!device_->canUpdateSharedBuffers()) return false;
            for(const auto& handle:it->second.particles) {
                if(slots_.alive(handle) && !slots_.kill(handle)) return false;
            }
        }
        emitters_.erase(it);
        ++stats_.removed;
        return true;
    }

    // Source-global presentation budget only; valid for all compatible
    // emitters in this arena. The physical GPU buffer capacity is unchanged.
    void setExternalSpawnScale(float scale) {
        if(std::isfinite(scale))
            externalSpawnScale_=std::clamp(scale,0.0f,1.0f);
    }

    // Run ONCE for this arena each frame after beginFrame, before the scene
    // pass. All compatible sources share a single GPU compute dispatch.
    bool update(float dt,render::float3 cameraEye) {
        if(!ready() || !finite(cameraEye) || !std::isfinite(dt) ||
           dt<0 || dt>0.1f || !device_->canUpdateSharedBuffers()) return false;
        for(auto& pair:emitters_) {
            auto& emitter=pair.second;
            // Bound retained handles by actively pruning dead/reused slots.
            auto& tracked=emitter.particles;
            tracked.erase(std::remove_if(tracked.begin(),tracked.end(),
                [this](SpectraFxGpuParticleHandle h) {return !slots_.alive(h);}),
                tracked.end());
            if(dt<=0) continue; // paused: no burst or new emissions
            const float distance=spectraFxDistance(cameraEye,emitter.position);
            const float spawnScale=spectraFxSpawnQualityScale(emitter.config.quality) *
                                   emitter.config.lod.spawnScale(distance) *
                                   externalSpawnScale_;
            const bool ok=emitter.scheduler.tick(
                dt,emitter.position,emitter.direction,spawnScale,
                [this,&emitter](render::float3 pos,render::float3 vel,float life) {
                    auto h=slots_.emit({pos,vel,0,life});
                    if(!h.valid()) {++stats_.rejected;return false;}
                    emitter.particles.push_back(h);
                    ++stats_.submitted;
                    return true;
                });
            if(!ok) {error_="SpectraFX shared emitter spawn frame invalid";return false;}
        }
        if(!slots_.step(dt,config_.environment)) {
            error_="SpectraFX shared arena GPU evolution failed";
            return false;
        }
        ++stats_.gpuSteps;
        if(compact_.ready()) {
            if(compact_.prepare(slots_.evolution())) ++stats_.gpuCompacted;
            else ++stats_.indirectUnavailable;
        }
        return true;
    }

    // Call inside a compatible open scene render pass after update().
    // The renderer sees one homogeneous batch (one material/appearance);
    // no per-emitter draw submissions or CPU GPU-position readback.
    bool render(const render::mat4& viewProj,
                render::float3 cameraRight,render::float3 cameraUp) {
        return ready() && renderer_.draw(
            slots_.evolution(),viewProj,cameraRight,cameraUp,
            config_.appearance,compact_.ready()?&compact_:nullptr);
    }

    bool ready() const {
        return device_ && slots_.evolution().ready() && renderer_.ready();
    }
    size_t emitterCount() const {return emitters_.size();}
    uint32_t aliveParticles() const {return slots_.activeCount();}
    uint32_t capacity() const {return slots_.capacity();}
    uint64_t nominalGpuBytes() const {
        if(!ready()) return 0;
        return slots_.evolution().nominalGpuBytes() +
               compact_.nominalGpuBytes();
    }
    const Stats& stats() const {return stats_;}
    const std::string& lastError() const {return error_;}
    const SpectraFxGpuParticleSlots& slots() const {return slots_;}

    void shutdown() {
        emitters_.clear();
        compact_.release();
        renderer_.release();
        slots_.shutdown();
        device_=nullptr;
    }

private:
    struct Emitter {
        EmitterConfig config{};
        SpectraFxGpuSpawnScheduler scheduler{};
        render::float3 position{},direction{0,1,0};
        std::vector<SpectraFxGpuParticleHandle> particles;
    };
    static bool finite(render::float3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    }
    gfx::Device* device_=nullptr;
    Config config_{};
    SpectraFxGpuParticleSlots slots_{};
    SpectraFxGpuBillboardRenderer renderer_{};
    SpectraFxGpuCompaction compact_{};
    std::map<uint64_t,Emitter> emitters_;
    Stats stats_{};
    float externalSpawnScale_=1.0f;
    std::string error_;
};

} // namespace aiverse::fx
