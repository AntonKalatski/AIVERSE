// SpectraFX: route generic ECS presentation cues to canonical particle graphs.
// The existing SpectraGraphAssets compiles Spawn/Initialize/Update/Output to
// EffectDef. ParticleSystem owns CPU simulation and RHI rendering.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include "SpectraFX.h"
#include "SpectraFxCueBridge.h"

namespace aiverse::fx {

// This is the particle graph adapter, not the cue-processing SpectraFxGraph
// and not the experimental SpectraFxCpuEmitterRuntime program descriptor.
// All effects stay presentation-only and are absent from ECS save/checksums.
class SpectraFxGraphPlayback {
public:
    struct Route {
        std::string graph;
        Json overrides = Json::object();
        bool enabled = true;
    };
    struct Stats {
        uint64_t received=0,played=0,moved=0,stopped=0,killed=0;
        uint64_t invalid=0,missing=0,failed=0,replaced=0;
    };

    SpectraFxGraphPlayback(ParticleSystem& particles,SpectraGraphAssets& assets)
        : particles_(particles), assets_(assets) {}

    void route(uint32_t cueId,Route data) {
        routes_[cueId]=std::move(data);
    }
    void unroute(uint32_t cueId) {routes_.erase(cueId);}
    void clearRoutes() {routes_.clear();}
    const Route* find(uint32_t id) const {
        auto it=routes_.find(id);
        return it==routes_.end()?nullptr:&it->second;
    }

    // A key == 0 plays a one-shot; nonzero keys attach to moving ECS
    // objects. Hosts must generate keys using entity generation + cue slot.
    bool dispatch(const SpectraFxCue& cue) {
        ++stats_.received;
        if(cue.action != SpectraFxCue::Action::Play) {
            if(!cue.key) {++stats_.invalid;return false;}
            auto it=active_.find(cue.key);
            if(it==active_.end()) {++stats_.missing;return false;}
            if(cue.action==SpectraFxCue::Action::Move) {
                particles_.move(it->second,cue.position,cue.direction);
                ++stats_.moved;return true;
            }
            if(cue.action==SpectraFxCue::Action::Kill) {
                particles_.kill(it->second);
                ++stats_.killed;
            } else {
                particles_.stop(it->second);
                ++stats_.stopped;
            }
            active_.erase(it);
            return true;
        }
        const Route* rule=find(cue.id);
        if(!rule || !rule->enabled || rule->graph.empty()) {
            ++stats_.missing;return false;
        }
        // Load may return a known-good fallback on an invalid hot reload.
        // Error information remains accessible via lastLoad().
        lastLoad_=assets_.load(rule->graph,rule->overrides);
        if(lastLoad_.effect<0) {++stats_.failed;return false;}
        const FxHandle handle=particles_.play(lastLoad_.effect,cue.position,
                                               cue.direction,cue.scale,cue.tint);
        if(!handle.valid()) {++stats_.failed;return false;}
        if(cue.key) {
            auto it=active_.find(cue.key);
            if(it!=active_.end()) {
                particles_.kill(it->second);
                it->second=handle;
                ++stats_.replaced;
            } else active_.emplace(cue.key,handle);
        }
        ++stats_.played;
        return true;
    }

    template<class Range> void dispatchAll(const Range& cues) {
        for(const auto& cue:cues) dispatch(cue);
    }

    // Recompile changed graph sources on the next Play. Running effects
    // remain owned by ParticleSystem, which replaces cached definitions.
    void invalidateAssets() {assets_.invalidate();}
    void invalidateAsset(const std::string& name) {assets_.invalidate(name);}

    // Called after ParticleSystem::update to drop completed emitter handles
    // from the adapter map. Does not kill lingering particles.
    void prune() {
        for(auto it=active_.begin();it!=active_.end();) {
            if(!particles_.playing(it->second)) it=active_.erase(it);
            else ++it;
        }
    }

    // Explicit rollback / new match / scene reset: kill tracked instances.
    void reset() {
        for(const auto& p:active_) particles_.kill(p.second);
        active_.clear();
    }

    size_t attached() const {return active_.size();}
    const Stats& stats() const {return stats_;}
    const SpectraGraphLoadResult& lastLoad() const {return lastLoad_;}
    void resetStats() {stats_={};}
private:
    ParticleSystem& particles_;
    SpectraGraphAssets& assets_;
    std::unordered_map<uint32_t,Route> routes_;
    std::unordered_map<uint64_t,FxHandle> active_;
    SpectraGraphLoadResult lastLoad_;
    Stats stats_{};
};

} // namespace aiverse::fx
