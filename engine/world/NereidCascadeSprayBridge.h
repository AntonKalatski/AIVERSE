// Optional NEREID waterfall-to-SpectraFX bridge for continuous bottom spray.
// The host owns SpectraFX graphs, update/render; NEREID supplies optical
// cascade geometry/position and never mutates ECS or advances particles.
#pragma once
#include "engine/world/WorldScene.h"
#include "engine/fx/SpectraFX.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace aiverse::world {

class NereidCascadeSprayBridge final {
public:
    explicit NereidCascadeSprayBridge(spectra::Runtime& particles)
        :particles_(particles){}
    void setGraph(std::string graph,float scale=1.f) {
        const float safeScale=std::isfinite(scale)?
            std::clamp(scale,0.f,16.f):0.f;
        const bool changed=graph!=graph_ || scale_!=safeScale;
        graph_=std::move(graph);
        scale_=safeScale;
        if(changed || graph_.empty() || scale_<=0.f)resetFoot();
    }
    // Optional intermediate cascade impacts. The graph key is supplied
    // by the native game, not hard-coded into NEREID.
    void setStepGraph(std::string graph,float scale=1.f,
                      float minDrop=0.5f) {
        const float safeScale=std::isfinite(scale)?
            std::clamp(scale,0.f,16.f):0.f;
        const float safeDrop=std::isfinite(minDrop)?
            std::clamp(minDrop,0.05f,1000.f):0.5f;
        if(graph!=stepGraph_ || stepScale_!=safeScale ||
           minDrop_!=safeDrop)resetSteps();
        stepGraph_=std::move(graph);
        stepScale_=safeScale;
        minDrop_=safeDrop;
        if(stepGraph_.empty() || stepScale_<=0.f)resetSteps();
    }
    // Sync once per presentation frame after the host's ECS read barrier.
    // A waterfall may move without reconstructing its particle definition.
    void sync(const WorldScene& scene) {
        std::set<uint64_t> live;
        std::set<std::pair<uint64_t,size_t>> liveSteps;
        if((!graph_.empty() && scale_>0.f) ||
           (!stepGraph_.empty() && stepScale_>0.f)) {
            scene.forEachNereidCascade([&](uint64_t id,
                    const NereidCascadeSource& source) {
                const auto& cascade=source.cascade;
                if(!lib::nereid::validWaterCascade(cascade))return;
                live.insert(id);
                const auto& foot=cascade.points.back();
                const auto& head=cascade.points.front();
                const fx::float3 location={
                    float(foot.position.x.toDouble()),
                    float(foot.position.y.toDouble()),
                    float(foot.position.z.toDouble())};
                const fx::float3 direction={
                    float(foot.position.x.toDouble()-head.position.x.toDouble()),
                    float(foot.position.y.toDouble()-head.position.y.toDouble()),
                    float(foot.position.z.toDouble()-head.position.z.toDouble())};
                const float width=float(foot.halfWidth.toDouble());
                if(!graph_.empty() && scale_>0.f) {
                    const float strength=std::clamp(scale_*width,0.05f,16.f);
                    auto it=handles_.find(id);
                    if(it==handles_.end() || !it->second.valid() ||
                       !particles_.playing(it->second)) {
                        const auto handle=particles_.play(graph_,location,direction,strength);
                        if(handle.valid())handles_[id]=handle;
                    }else particles_.move(it->second,location,direction);
                }
                // A separate stable emitter for each eligible intermediate
                // drop. The last point already has a foot emitter.
                if(!stepGraph_.empty() && stepScale_>0.f) {
                    size_t presented=0;
                    const auto& points=cascade.points;
                    for(size_t i=1;i+1<points.size() && presented<8;++i) {
                        const auto& upper=points[i-1];
                        const auto& lower=points[i];
                        const double drop=upper.position.y.toDouble()-
                                          lower.position.y.toDouble();
                        if(drop<double(minDrop_))continue;
                        const auto key=std::make_pair(id,i);
                        liveSteps.insert(key);
                        ++presented;
                        const fx::float3 stepPos={
                            float(lower.position.x.toDouble()),
                            float(lower.position.y.toDouble()),
                            float(lower.position.z.toDouble())};
                        const fx::float3 stepDir={
                            float(lower.position.x.toDouble()-upper.position.x.toDouble()),
                            float(lower.position.y.toDouble()-upper.position.y.toDouble()),
                            float(lower.position.z.toDouble()-upper.position.z.toDouble())};
                        const float stepStrength=std::clamp(
                            stepScale_*float(lower.halfWidth.toDouble())*
                            std::clamp(float(drop)*0.2f,0.25f,2.f),
                            0.05f,16.f);
                        auto step=stepHandles_.find(key);
                        if(step==stepHandles_.end() || !step->second.valid() ||
                           !particles_.playing(step->second)) {
                            const auto effect=particles_.play(stepGraph_,
                                               stepPos,stepDir,stepStrength);
                            if(effect.valid())stepHandles_[key]=effect;
                        }else particles_.move(step->second,stepPos,stepDir);
                    }
                }
            });
        }
        for(auto it=handles_.begin();it!=handles_.end();) {
            if(live.contains(it->first)) {++it;continue;}
            particles_.stop(it->second);
            it=handles_.erase(it);
        }
        for(auto it=stepHandles_.begin();it!=stepHandles_.end();) {
            if(liveSteps.contains(it->first)) {++it;continue;}
            particles_.stop(it->second);
            it=stepHandles_.erase(it);
        }
    }
    void resetFoot() {
        for(const auto& [id,handle]:handles_) {
            (void)id;particles_.kill(handle);
        }
        handles_.clear();
    }
    void resetSteps() {
        for(const auto& [key,handle]:stepHandles_) {
            (void)key;particles_.kill(handle);
        }
        stepHandles_.clear();
    }
    // Host must reset on rollback or when replacing its particle runtime.
    void reset() {
        resetFoot();
        resetSteps();
    }
    size_t tracked()const{return handles_.size()+stepHandles_.size();}
private:
    spectra::Runtime& particles_;
    std::string graph_;
    float scale_=1.f;
    std::map<uint64_t,spectra::Handle> handles_;
    std::map<std::pair<uint64_t,size_t>,spectra::Handle> stepHandles_;
    std::string stepGraph_;
    float stepScale_=1.f;
    float minDrop_=0.5f;
};

} // namespace aiverse::world
