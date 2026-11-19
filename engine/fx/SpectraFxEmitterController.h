// SpectraFX tracked emitter controller. Emits presentation callbacks as an
// effect starts and when its emission ends. Does not claim particle extinction:
// ParticleSystem may keep fading particles after emission completion.
#pragma once
#include <cstdint>
#include <unordered_map>
#include <utility>
#include "SpectraFxEmitterRuntime.h"
#include "SpectraFxEmitterEvents.h"

namespace aiverse::fx {

class SpectraFxEmitterController {
public:
    SpectraFxEmitterController(SpectraFxCpuEmitterRuntime& runtime,
                              SpectraFxEmitterEventRouter& events)
        : runtime_(runtime), events_(events) {}

    bool start(uint64_t key, const SpectraFxEmitterProgram& program,
               ParticleSystem& particles, float3 pos,
               float3 dir={0,1,0}, float scale=1,
               float4 tint={1,1,1,1}) {
        if (!key || !runtime_.start(key,program,particles,pos,dir,scale,tint))
            return false;
        records_[key]={pos,dir,scale,tint};
        emit(key,SpectraFxEmitterEvent::Spawned);
        return true;
    }

    bool move(uint64_t key, ParticleSystem& particles,
              float3 pos,float3 dir={0,1,0}) {
        if (!runtime_.move(key,particles,pos,dir)) return false;
        auto it=records_.find(key);
        if (it!=records_.end()) {
            it->second.pos=pos;
            it->second.dir=dir;
        }
        return true;
    }

    // Explicit cancellation is not natural completion; it emits no Completed.
    bool stop(uint64_t key,ParticleSystem& particles,bool kill=false) {
        if (!runtime_.stop(key,particles,kill)) return false;
        records_.erase(key);
        return true;
    }

    // Call after ParticleSystem::update. Stops tracking instances that are no
    // longer emitting, and publishes Completed once per instance generation.
    // If event queue is saturated, completion may be dropped (visual only).
    void poll(ParticleSystem& particles) {
        for (auto it=records_.begin();it!=records_.end();) {
            if (runtime_.emitting(it->first,particles)) { ++it; continue; }
            const uint64_t key=it->first;
            const Data data=it->second;
            runtime_.stop(key,particles,false);
            SpectraFxEmitterEventRecord event;
            event.emitterKey=key;
            event.type=SpectraFxEmitterEvent::Completed;
            event.position=data.pos;
            event.direction=data.dir;
            event.scale=data.scale;
            event.tint=data.tint;
            events_.publish(event);
            it=records_.erase(it);
        }
    }

    // Discontinuity: do not publish Completed for invalidated instances.
    void reset(ParticleSystem& particles) {
        runtime_.reset(particles);
        records_.clear();
        events_.discardPending();
    }

    size_t trackedCount() const { return records_.size(); }
private:
    struct Data {
        float3 pos{},dir{0,1,0};
        float scale=1;
        float4 tint{1,1,1,1};
    };
    void emit(uint64_t key,SpectraFxEmitterEvent type) {
        const auto it=records_.find(key);
        if(it==records_.end()) return;
        SpectraFxEmitterEventRecord event;
        event.emitterKey=key;
        event.type=type;
        event.position=it->second.pos;
        event.direction=it->second.dir;
        event.scale=it->second.scale;
        event.tint=it->second.tint;
        events_.publish(event);
    }
    SpectraFxCpuEmitterRuntime& runtime_;
    SpectraFxEmitterEventRouter& events_;
    std::unordered_map<uint64_t,Data> records_;
};

} // namespace aiverse::fx
