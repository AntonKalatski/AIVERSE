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
        const bool changed=graph!=graph_;
        graph_=std::move(graph);
        scale_=std::isfinite(scale)?std::clamp(scale,0.f,16.f):0.f;
        if(changed || graph_.empty() || scale_<=0.f)reset();
    }
    // Sync once per presentation frame after the host's ECS read barrier.
    // A waterfall may move without reconstructing its particle definition.
    void sync(const WorldScene& scene) {
        std::set<uint64_t> live;
        if(!graph_.empty() && scale_>0.f) {
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
                const float strength=std::clamp(scale_*width,0.05f,16.f);
                auto it=handles_.find(id);
                if(it==handles_.end() || !it->second.valid() ||
                   !particles_.playing(it->second)) {
                    const auto handle=particles_.play(graph_,location,direction,strength);
                    if(handle.valid())handles_[id]=handle;
                }else particles_.move(it->second,location,direction);
            });
        }
        for(auto it=handles_.begin();it!=handles_.end();) {
            if(live.contains(it->first)) {++it;continue;}
            particles_.stop(it->second);
            it=handles_.erase(it);
        }
    }
    // Host must reset on rollback or when replacing its particle runtime.
    void reset() {
        for(const auto& [id,handle]:handles_) {
            (void)id;
            particles_.kill(handle);
        }
        handles_.clear();
    }
    size_t tracked()const{return handles_.size();}
private:
    spectra::Runtime& particles_;
    std::string graph_;
    float scale_=1.f;
    std::map<uint64_t,spectra::Handle> handles_;
};

} // namespace aiverse::world
