// NEREID — bounded presentation-only expanding ripple rings.
// Consumes deterministic ECS interaction snapshots, but owns no simulation
// state, GPU buffer, or genre-specific VFX implementation.
#pragma once
#include "engine/render/Renderer3D.h"
#include <aiverselib/water/NereidInteractionPresentation.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::world {

struct NereidRipple {
    uint64_t waterId=0;      // packed generation-safe entity from WaterInteractionOutput
    uint64_t sourceId=0;
    lib::nereid::WaterInteractionKind kind=lib::nereid::WaterInteractionKind::None;
    double x=0,y=0,z=0;
    float age=0, lifetime=3.f, speed=3.f;
    float initialRadius=1, amplitude=1, width=0.2f;
};

// Draws camera-origin-relative geometry using the existing overlay triangle
// queue. Not a physical displacement or a readback from deterministic water.
class NereidRippleField {
public:
    static constexpr size_t kMaxRipples=256;
    void clear() {ripples_.clear();lastTick_=0;hasTick_=false;}
    void setLimit(size_t max) {limit_=std::clamp(max,size_t(1),kMaxRipples);
        if(ripples_.size()>limit_)
            ripples_.erase(ripples_.begin(),ripples_.begin()+std::ptrdiff_t(ripples_.size()-limit_));
    }
    size_t size()const{return ripples_.size();}
    const std::vector<NereidRipple>& ripples()const{return ripples_;}
    // Select at most four nearest ripple rings for one water body.
    // A body snapshot uses Entity::packed()+1 as its ID; events contain
    // Entity::packed() directly. Render coordinates match WorldScene origin.
    std::array<render::float4,16> shaderRipples(
            uint64_t waterId,double originX,double originZ,
            double targetX,double targetZ) const {
        std::array<render::float4,16> result{};
        struct Candidate {double distanceSquared=0;size_t index=0;};
        std::vector<Candidate> candidates;
        candidates.reserve(ripples_.size());
        for(size_t i=0;i<ripples_.size();++i) {
            const auto& r=ripples_[i];
            if(waterId==0 || waterId!=r.waterId+1)continue;
            const double dx=r.x-targetX,dz=r.z-targetZ;
            const double d2=dx*dx+dz*dz;
            // Do not clip by distance from the mesh origin: a polygon or
            // a wide lake can span kilometres. The fragment shader gates
            // each ring spatially against the actual pixel position.
            candidates.push_back({d2,i});
        }
        std::sort(candidates.begin(),candidates.end(),[](auto a,auto b) {
            if(a.distanceSquared==b.distanceSquared)return a.index<b.index;
            return a.distanceSquared<b.distanceSquared;
        });
        for(size_t i=0;i<std::min(size_t(16),candidates.size());++i) {
            const auto& r=ripples_[candidates[i].index];
            const float fade=std::clamp(1.f-r.age/r.lifetime,0.f,1.f);
            const float radius=r.initialRadius+r.speed*r.age;
            result[i]={float(r.x-originX),float(r.z-originZ),radius,
                       std::min(r.amplitude*fade*0.12f,2.f)};
        }
        return result;
    }
    // Supplying the same simulated tick twice must not duplicate VFX events.
    // Call in ascending tick order; a rollback starts a new visual timeline.
    template<class EventCallback>
    void ingest(const std::vector<lib::nereid::WaterPresentationEvent>& events,
                uint32_t completedTick,EventCallback&& onEvent) {
        if(hasTick_ && completedTick==lastTick_)return;
        if(hasTick_ && completedTick<lastTick_)ripples_.clear();
        lastTick_=completedTick;hasTick_=true;
        for(const auto& item:events) {
            const auto& e=item.event;
            if(e.tick!=completedTick || e.kind==lib::nereid::WaterInteractionKind::None)
                continue;
            // Callback is a host SpectraFX/audio hook; its implementation
            // must not modify authoritative ECS while the read phase runs.
            onEvent(item);
            if(e.kind!=lib::nereid::WaterInteractionKind::Enter &&
               e.kind!=lib::nereid::WaterInteractionKind::Exit &&
               e.kind!=lib::nereid::WaterInteractionKind::SurfaceWake &&
               e.kind!=lib::nereid::WaterInteractionKind::SwitchBody)
                continue;
            NereidRipple r;
            r.waterId=uint64_t(e.water.packed());
            r.sourceId=uint64_t(item.source.packed());
            r.kind=e.kind;
            r.x=e.position.x.toDouble();
            r.y=e.position.y.toDouble();
            r.z=e.position.z.toDouble();
            r.initialRadius=std::clamp(float(e.radius.toDouble()),0.05f,100.f);
            r.amplitude=std::clamp(float(e.strength.toDouble()),0.f,16.f);
            if(e.kind==lib::nereid::WaterInteractionKind::SurfaceWake) {
                r.lifetime=2.2f;r.speed=2.5f;r.width=0.11f;
            }else{
                r.lifetime=3.5f;r.speed=4.5f;r.width=0.22f;
            }
            if(ripples_.size()>=limit_)ripples_.erase(ripples_.begin());
            ripples_.push_back(r);
        }
    }
    void ingest(const std::vector<lib::nereid::WaterPresentationEvent>& events,
                uint32_t completedTick) {
        ingest(events,completedTick,[](const auto&){});
    }
    // Independent render-time ageing: never feeds a simulation checksum.
    void advance(float deltaSeconds) {
        if(!std::isfinite(deltaSeconds) || deltaSeconds<=0)return;
        const float dt=std::min(deltaSeconds,0.25f);
        for(auto& r:ripples_)r.age+=dt;
        ripples_.erase(std::remove_if(ripples_.begin(),ripples_.end(),
            [](const NereidRipple& r){return r.age>=r.lifetime;}),ripples_.end());
    }
    // Alpha-blended annular quads, generated only for currently live ripples.
    // World origin uses double subtraction before conversion to float,
    // avoiding far-world precision loss. Y is lifted slightly to avoid z-fight.
    void draw(render::Renderer3D& renderer,double originX,double originY,
              double originZ)const {
        constexpr int segments=24;
        constexpr float tau=6.28318530717958647692f;
        for(const auto& r:ripples_) {
            const float fade=std::clamp(1.f-r.age/r.lifetime,0.f,1.f);
            const uint8_t alpha=uint8_t(std::clamp(r.amplitude*fade*42.f,0.f,160.f));
            if(alpha==0)continue;
            const float radius=r.initialRadius+r.speed*r.age;
            const float halfWidth=r.width*(1.f+r.age*0.2f);
            const float inner=std::max(0.f,radius-halfWidth);
            const float outer=radius+halfWidth;
            const float x=float(r.x-originX),y=float(r.y-originY)+0.035f,
                        z=float(r.z-originZ);
            const uint32_t color=render::rgba(190,220,230,alpha);
            for(int j=0;j<segments;++j) {
                const float t0=tau*float(j)/segments;
                const float t1=tau*float(j+1)/segments;
                const render::float3 a{x+std::cos(t0)*inner,y,z+std::sin(t0)*inner};
                const render::float3 b{x+std::cos(t1)*inner,y,z+std::sin(t1)*inner};
                const render::float3 c{x+std::cos(t1)*outer,y,z+std::sin(t1)*outer};
                const render::float3 d{x+std::cos(t0)*outer,y,z+std::sin(t0)*outer};
                renderer.triangle(a,b,c,color);
                renderer.triangle(a,c,d,color);
            }
        }
    }
private:
    std::vector<NereidRipple> ripples_;
    size_t limit_=kMaxRipples;
    uint32_t lastTick_=0;
    bool hasTick_=false;
};

} // namespace aiverse::world
