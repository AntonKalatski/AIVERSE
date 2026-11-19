// NEREID waterfalls and stepped cascades, presentation-only GPU residency.
// ECS owns WaterCascadeComponent; this view owns uploaded meshes. No FPS/RTS
// coupling, no independent fluid simulation and no new RHI backend.
#pragma once
#include "engine/render/NereidWaterRenderer.h"
#include "engine/world/WorldAssets.h"
#include "engine/world/NereidWaterScene.h"
#include <aiverselib/water/NereidCascades.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>
#include <utility>

namespace aiverse::world {

struct NereidCascadeSource {
    uint64_t id=0; // stable positive ID; 0 gets a legacy index ID
    uint64_t inletWaterId=0,outletWaterId=0; // optional stable NEREID IDs
    float joinLength=2.f; // X/Z overlap extension in metres (0.1..50)
    float joinWidthBlend=1.f; // 0..1 cascade width -> linked bank width
    bool requireFootprint=true; // reject joins outside source water geometry
    lib::nereid::WaterCascadeComponent cascade;
    lib::nereid::WaterSurfaceStyle style{};
    lib::nereid::WaterCascadeMeshOptions options{};
};

inline bool sameNereidCascadeSource(const NereidCascadeSource& a,
                                   const NereidCascadeSource& b) {
    const auto& x=a.cascade;const auto& y=b.cascade;
    const auto& s=a.style;const auto& t=b.style;
    if(a.inletWaterId!=b.inletWaterId ||
       a.outletWaterId!=b.outletWaterId ||
       a.joinLength!=b.joinLength ||
       a.joinWidthBlend!=b.joinWidthBlend ||
       a.requireFootprint!=b.requireFootprint ||
       x.enabled!=y.enabled || x.widthAxis!=y.widthAxis ||
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

// Build transition geometry ONLY on a presentation-owned copy. When a
// linked water body exists, extend the top or foot ribbon into its horizontal
// surface at the body's current elevation. No changes to ECS source points.
inline lib::nereid::WaterCascadeComponent connectedNereidCascade(
        const NereidCascadeSource& source,
        std::optional<Fixed> inletLevel,
        std::optional<Fixed> outletLevel) {
    auto result=source.cascade;
    if(result.points.size()<2 || result.points.size()>126 ||
       !std::isfinite(source.joinLength) ||
       source.joinLength<=0 || source.joinLength>50.f)
        return result;
    const auto dir=[&](size_t i,size_t j) {
        const auto& a=result.points[i].position;
        const auto& b=result.points[j].position;
        Vec2 d{b.x-a.x,b.z-a.z};
        const Fixed len=d.length();
        if(len>Fixed{})return d/len;
        // Purely vertical drop: pick the forward direction perpendicular
        // to the cascade's authored horizontal ribbon-width axis.
        d={-result.widthAxis.y,result.widthAxis.x};
        const Fixed fallback=d.length();
        return fallback>Fixed{} ? d/fallback : Vec2{1_fx,0_fx};
    };
    const Fixed extension=Fixed::fromRaw(
        int64_t(std::llround(double(source.joinLength)*Fixed::kOne)));
    // Compute directions before changing the source-point indices.
    const Vec2 firstDir=dir(0,1);
    const Vec2 lastDir=dir(result.points.size()-2,result.points.size()-1);
    if(source.inletWaterId && inletLevel &&
       *inletLevel>=result.points.front().position.y) {
        auto first=result.points.front();
        first.position.x-=firstDir.x*extension;
        first.position.z-=firstDir.y*extension;
        first.position.y=*inletLevel;
        result.points.insert(result.points.begin(),first);
    }
    if(source.outletWaterId && outletLevel &&
       *outletLevel<=result.points.back().position.y) {
        auto last=result.points.back();
        last.position.x+=lastDir.x*extension;
        last.position.z+=lastDir.y*extension;
        last.position.y=*outletLevel;
        result.points.push_back(last);
    }
    return result;
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
        std::optional<Fixed> inletLevel,outletLevel;
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
            if((source.cascade.enabled &&
                !lib::nereid::validWaterCascade(source.cascade)) ||
               !render::validNereidWaterStyle(source.style) ||
               source.options.stepsPerSegment<1 ||
               source.options.stepsPerSegment>16 ||
               source.options.maxVertices<8 ||
               source.options.maxVertices>65536 ||
               !std::isfinite(source.joinLength) ||
               source.joinLength<0.1f || source.joinLength>50.f ||
               !std::isfinite(source.joinWidthBlend) ||
               source.joinWidthBlend<0.f || source.joinWidthBlend>1.f ||
               !std::isfinite(source.options.uvScale) ||
               source.options.uvScale<=0)
                throw std::invalid_argument("nereid.cascade: invalid source");
            const uint64_t id=source.id;
            if(!next.emplace(id,Slot{std::move(source)}).second)
                throw std::invalid_argument("nereid.cascade: duplicate id");
        }
        retired_.reserve(retired_.size()+slots_.size());
        for(auto& [id,slot]:next) {
            auto old=slots_.find(id);
            if(old==slots_.end())continue;
            slot.dirty=old->second.dirty ||
                !sameNereidCascadeSource(slot.source,old->second.source);
            slot.mesh=old->second.mesh;
            slot.resident=old->second.resident;
            slot.inletLevel=old->second.inletLevel;
            slot.outletLevel=old->second.outletLevel;
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
    template<class Fn> void forEachSource(Fn&& fn)const {
        for(const auto& [id,slot]:slots_)
            if(slot.source.cascade.enabled)fn(id,slot.source);
    }
    size_t residentCount()const {
        size_t n=0;for(const auto& [id,s]:slots_) { (void)id;if(s.resident)++n; }
        return n;
    }
    void update(gfx::Device& device,
                const NereidWaterScene* water=nullptr) {
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
            const auto inlet=water && slot.source.inletWaterId?
                water->surfaceLevelForId(slot.source.inletWaterId):
                std::optional<Fixed>{};
            const auto outlet=water && slot.source.outletWaterId?
                water->surfaceLevelForId(slot.source.outletWaterId):
                std::optional<Fixed>{};
            if(inlet!=slot.inletLevel || outlet!=slot.outletLevel)
                slot.dirty=true;
            slot.inletLevel=inlet;
            slot.outletLevel=outlet;
            if(!slot.dirty || attempts>=maxUploadsPerFrame_)continue;
            ++attempts;
            if(!lib::nereid::validWaterCascade(slot.source.cascade))continue;
            auto opts=slot.source.options;
            const auto& first=slot.source.cascade.points.front().position;
            opts.originX=first.x.toDouble();
            opts.originY=first.y.toDouble();
            opts.originZ=first.z.toDouble();
            const auto joined=connectedNereidCascade(
                slot.source,inlet,outlet);
            auto cpu=lib::nereid::buildWaterCascadeMesh(joined,opts);
            if(cpu.empty())continue;
            // Seam foam masks the material transition between a horizontal
            // water surface and the descending sheet. This is a visual
            // gradient on the extra connector segments, never ECS geometry.
            const bool inletStitched=joined.points.size()>slot.source.cascade.points.size() &&
                joined.points.front().position!=slot.source.cascade.points.front().position;
            const bool outletStitched=joined.points.size()>slot.source.cascade.points.size() &&
                joined.points.back().position!=slot.source.cascade.points.back().position;
            const size_t steps=opts.stepsPerSegment;
            const size_t count=cpu.vertices.size();
            if(inletStitched && count>=steps*8) {
                for(size_t i=0;i<steps;++i) {
                    const float t=float(i)/float(steps);
                    const float u=float(i+1)/float(steps);
                    const size_t base=i*8;
                    for(size_t k:{size_t(0),size_t(1),size_t(4),size_t(5)})
                        cpu.vertices[base+k].foam=std::max(
                            cpu.vertices[base+k].foam,(1.f-t)*0.9f);
                    for(size_t k:{size_t(2),size_t(3),size_t(6),size_t(7)})
                        cpu.vertices[base+k].foam=std::max(
                            cpu.vertices[base+k].foam,(1.f-u)*0.9f);
                }
            }
            if(outletStitched && count>=steps*8) {
                const size_t first=count-steps*8;
                for(size_t i=0;i<steps;++i) {
                    const float t=float(i)/float(steps);
                    const float u=float(i+1)/float(steps);
                    const size_t base=first+i*8;
                    for(size_t k:{size_t(0),size_t(1),size_t(4),size_t(5)})
                        cpu.vertices[base+k].foam=std::max(
                            cpu.vertices[base+k].foam,t*0.9f);
                    for(size_t k:{size_t(2),size_t(3),size_t(6),size_t(7)})
                        cpu.vertices[base+k].foam=std::max(
                            cpu.vertices[base+k].foam,u*0.9f);
                }
            }
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
