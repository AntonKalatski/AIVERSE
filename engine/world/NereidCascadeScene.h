// NEREID waterfalls and stepped cascades, presentation-only GPU residency.
// ECS owns WaterCascadeComponent; this view owns uploaded meshes. No FPS/RTS
// coupling, no independent fluid simulation and no new RHI backend.
#pragma once
#include "engine/render/NereidWaterRenderer.h"
#include "engine/world/WorldAssets.h"
#include <aiverselib/water/NereidCascades.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace aiverse::world {

struct NereidCascadeSource {
    uint64_t id=0; // stable positive ID; 0 gets a legacy index ID
    lib::nereid::WaterCascadeComponent cascade;
    lib::nereid::WaterSurfaceStyle style{};
    lib::nereid::WaterCascadeMeshOptions options{};
};

inline bool sameNereidCascadeSource(const NereidCascadeSource& a,
                                   const NereidCascadeSource& b) {
    const auto& x=a.cascade;const auto& y=b.cascade;
    const auto& s=a.style;const auto& t=b.style;
    if(x.enabled!=y.enabled || x.widthAxis!=y.widthAxis ||
       x.foam!=y.foam || x.speed!=y.speed ||
       x.points.size()!=y.points.size() ||
       a.options.uvScale!=b.options.uvScale ||
       a.options.stepsPerSegment!=b.options.stepsPerSegment ||
       a.options.maxVertices!=b.options.maxVertices ||
       s.deepR!=t.deepR || s.deepG!=t.deepG || s.deepB!=t.deepB ||
       s.foamR!=t.foamR || s.foamG!=t.foamG || s.foamB!=t.foamB ||
       s.foamStrength!=t.foamStrength || s.opacity!=t.opacity ||
       s.specular!=t.specular || s.refractionStrength!=t.refractionStrength ||
       s.absorption!=t.absorption || s.shoreFoamDepth!=t.shoreFoamDepth ||
       s.depthTint!=t.depthTint || s.underwaterDistortion!=t.underwaterDistortion ||
       s.underwaterScattering!=t.underwaterScattering)return false;
    for(size_t i=0;i<x.points.size();++i)
        if(x.points[i].position!=y.points[i].position ||
           x.points[i].halfWidth!=y.points[i].halfWidth)return false;
    return true;
}

inline render::Mesh uploadNereidCascade(gfx::Device& device,
        const lib::nereid::WaterCascadeMesh& cpu,
        lib::nereid::WaterSurfaceStyle style) {
    if(cpu.empty() || !render::validNereidWaterStyle(style))return {};
    render::MeshBuilder builder;
    for(const auto& v:cpu.vertices) {
        const float foam=std::clamp(v.foam*style.foamStrength,0.f,1.f);
        const auto channel=[foam](float deep,float rim) {
            return uint8_t(std::clamp(deep+(rim-deep)*foam,0.f,1.f)*255.f+0.5f);
        };
        const uint32_t tint=render::rgba(
            channel(style.deepR,style.foamR),
            channel(style.deepG,style.foamG),
            channel(style.deepB,style.foamB),255);
        builder.addVertex({v.x,v.y,v.z},{v.nx,v.ny,v.nz},tint);
    }
    for(size_t i=0;i+2<cpu.indices.size();i+=3)
        builder.addTriangle(cpu.indices[i],cpu.indices[i+1],cpu.indices[i+2]);
    return builder.upload(device);
}

class NereidCascadeScene final {
    struct Slot {
        NereidCascadeSource source;
        render::Mesh mesh{};
        bool resident=false;
        bool dirty=true;
        double originX=0,originY=0,originZ=0;
    };
public:
    static constexpr size_t kMaxCascades=32;
    void setCascades(std::vector<NereidCascadeSource> incoming) {
        if(incoming.size()>kMaxCascades)
            throw std::invalid_argument("nereid.cascade: too many cascades");
        // Validate and allocate before touching any GPU handles.
        std::map<uint64_t,Slot> next;
        for(size_t i=0;i<incoming.size();++i) {
            auto source=std::move(incoming[i]);
            if(!source.id)source.id=(uint64_t(1)<<62)|(i+1);
            if(!next.emplace(source.id,Slot{std::move(source)}).second)
                throw std::invalid_argument("nereid.cascade: duplicate id");
        }
        retired_.reserve(retired_.size()+slots_.size());
        for(auto& [id,slot]:next) {
            auto old=slots_.find(id);
            if(old==slots_.end())continue;
            slot.dirty=!sameNereidCascadeSource(slot.source,old->second.source);
            slot.mesh=old->second.mesh;
            slot.resident=old->second.resident;
            slot.originX=old->second.originX;
            slot.originY=old->second.originY;
            slot.originZ=old->second.originZ;
            old->second.mesh={};
        }
        for(auto& [id,old]:slots_) {
            (void)id;
            if(old.mesh.vb.valid() || old.mesh.ib.valid())
                retired_.push_back(old.mesh);
        }
        slots_.swap(next);
    }

    void setMaxUploadsPerFrame(uint32_t count) {
        if(count<1 || count>8)
            throw std::invalid_argument("nereid.cascade: upload count 1..8");
        maxUploadsPerFrame_=count;
    }
    size_t count()const{return slots_.size();}
    size_t residentCount()const {
        size_t n=0;for(const auto& [id,s]:slots_) { (void)id;if(s.resident)++n; }
        return n;
    }
    void update(gfx::Device& device) {
        for(auto& old:retired_)render::releaseNereidWaterSurface(device,old);
        retired_.clear();
        uint32_t attempts=0;
        for(auto& [id,slot]:slots_) {
            (void)id;
            if(!slot.source.cascade.enabled) {
                render::releaseNereidWaterSurface(device,slot.mesh);
                slot.resident=false;slot.dirty=true;
                continue;
            }
            if(!slot.dirty || attempts>=maxUploadsPerFrame_)continue;
            ++attempts;
            if(!lib::nereid::validWaterCascade(slot.source.cascade))continue;
            auto opts=slot.source.options;
            const auto& first=slot.source.cascade.points.front().position;
            opts.originX=first.x.toDouble();
            opts.originY=first.y.toDouble();
            opts.originZ=first.z.toDouble();
            const auto cpu=lib::nereid::buildWaterCascadeMesh(
                slot.source.cascade,opts);
            if(cpu.empty())continue;
            auto replacement=uploadNereidCascade(device,cpu,slot.source.style);
            if(!replacement.vb.valid() || !replacement.ib.valid() ||
               replacement.indexCount==0) {
                render::releaseNereidWaterSurface(device,replacement);
                continue;
            }
            render::releaseNereidWaterSurface(device,slot.mesh);
            slot.mesh=replacement;
            slot.originX=opts.originX;
            slot.originY=opts.originY;
            slot.originZ=opts.originZ;
            slot.resident=true;slot.dirty=false;
        }
    }
    void draw(render::Renderer3D& renderer,Cell origin,int cellSize)const {
        const double x=double(origin.x)*cellSize;
        const double y=double(origin.y)*cellSize;
        const double z=double(origin.z)*cellSize;
        for(const auto& [id,slot]:slots_) {
            (void)id;
            if(!slot.resident || !slot.source.cascade.enabled)continue;
            const auto xf=render::mat4::translate({
                float(slot.originX-x),float(slot.originY-y),
                float(slot.originZ-z)});
            const auto& style=slot.source.style;
            renderer.drawMesh(slot.mesh,xf,
                {style.absorption,style.shoreFoamDepth,
                 style.depthTint,style.opacity},
                render::kDrawNoCastShadow|render::kDrawWaterSurface|
                    render::kDrawNereidCascade,
                style.specular,style.refractionStrength);
        }
    }
    void shutdown(gfx::Device& device) {
        for(auto& old:retired_)render::releaseNereidWaterSurface(device,old);
        retired_.clear();
        for(auto& [id,slot]:slots_) {
            (void)id;
            render::releaseNereidWaterSurface(device,slot.mesh);
            slot.resident=false;slot.dirty=true;
        }
    }
private:
    std::map<uint64_t,Slot> slots_;
    std::vector<render::Mesh> retired_;
    uint32_t maxUploadsPerFrame_=2;
};

} // namespace aiverse::world
