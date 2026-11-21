// TerraWeave: bounded grass patch RHI adapter. Thousands of blades become
// one draw per visible patch, not one ECS entity / draw call per blade.
// Works with existing Renderer3D::drawModel and gfx::Device on its owner thread.
#pragma once
#include "engine/world/TerraWeaveGrassCover.h"
#include "engine/render/Model.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/TerraWeaveVertexWind.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct GrassRenderOriginCm {
    int64_t x=0,y=0,z=0;
};

struct GrassRenderQuality {
    uint32_t maxBladesPerPatch=1024; // 1..8192, stable bounded samples
    bool crossedBlades=true;         // 2 triangles instead of 1 per blade
    uint32_t maxGpuPatches=128;      // 1..256
    uint32_t maxUploadsPerFrame=2;   // 1..8
};

inline void validateGrassRenderQuality(const GrassRenderQuality& q) {
    if(!q.maxBladesPerPatch || q.maxBladesPerPatch>8192 ||
       !q.maxGpuPatches || q.maxGpuPatches>256 ||
       !q.maxUploadsPerFrame || q.maxUploadsPerFrame>8)
        throw std::invalid_argument("TerraWeave grass RHI quality");
}

inline bool sameGrassPresentation(const GrassPatch& a,const GrassPatch& b) {
    return a.key==b.key &&
        a.bounds.minX==b.bounds.minX && a.bounds.minZ==b.bounds.minZ &&
        a.bounds.maxX==b.bounds.maxX && a.bounds.maxZ==b.bounds.maxZ &&
        a.maximumBlades==b.maximumBlades &&
        a.coveragePermille==b.coveragePermille &&
        a.environment==b.environment && a.seed==b.seed;
}

using GrassSoilHeight=std::function<int32_t(int64_t,int64_t)>;

// Optional host-supplied presentation overlay (2026-11-21). TerraWeave keeps
// owning grass coverage; a host (e.g. engine/world/PhysisNatureGrass.h for
// PHYSIS wildfire) only tells the renderer how each blade LOOKS. Nothing here
// is written back to GrassPatch, Earthworks or any ECS.
struct GrassFireSample {
    uint16_t charPermille=0;   // 0 fresh .. 1000 burned to stubble/ash
    uint16_t firePermille=0;   // flames on this blade right now
};
using GrassFireSampler=std::function<GrassFireSample(int64_t xCm,int64_t zCm)>;
struct GrassPresentationOptions {
    GrassFireSampler fire;     // empty: unchanged legacy appearance
    bool windSway=false;       // root->tip vertex weights + canopyWindGraph
};
// Burning blades glow through a second, emissive primitive. Bloom of that
// emission belongs to PRISMA; actual flames/smoke to SpectraFX.
inline constexpr uint16_t kGrassBurningFirePermille=140;

// Pure CPU model generation; only one or two opaque slender triangles per
// sampled blade. No per-patch alpha textures or unique shader variants.
// The sparse, stable stratified sample reacts to coverage/light/age without
// scanning a 262144-blade patch or allocating persistent blade arrays.
inline std::unique_ptr<render::Model> makeGrassPatchModel(
        const GrassPatch& patch,const GrassSoilHeight& ground,
        GrassRenderQuality quality={},
        const GrassPresentationOptions* presentation=nullptr) {
    validateGrassRenderQuality(quality);
    const bool sway=presentation && presentation->windSway;
    const GrassFireSampler* fireSampler=
        presentation && presentation->fire ? &presentation->fire : nullptr;
    if(!ground)throw std::invalid_argument("TerraWeave grass ground sampler");
    const auto draw=prepareGrassPatch(patch);
    if(draw.bladeCount==0)return {};
    auto model=std::make_unique<render::Model>();
    model->path="terraweave/grass/patch-"+std::to_string(patch.key);
    model->batching.autoLod=false;
    model->batching.allowGeometryPool=false;

    render::Material grass;
    grass.name="TerraWeave Grass Patch";
    grass.baseColor={0.38f,0.64f,0.25f,1.0f};
    grass.roughness=0.95f;
    grass.doubleSided=true;
    grass.vertexColors=true;
    grass.castShadow=false; // avoid expensive per-blade shadow casters
    if(sway)grass.shaderGraph=canopyWindGraph();
    model->materials.push_back(grass);
    if(fireSampler) {
        render::Material burning=grass;
        burning.name="TerraWeave Grass Burning";
        burning.baseColor={1.0f,1.0f,1.0f,1.0f};
        burning.emissive={1.0f,0.42f,0.08f};
        burning.emissiveStrength=3.0f;
        model->materials.push_back(burning);
    }
    std::vector<uint32_t> burningIndices;
    render::MeshDef mesh;
    mesh.name="Grass Batch";
    render::Primitive primitive;
    primitive.material=0;
    const auto appearance=plantAppearance(patch.environment);
    const uint32_t candidates=std::min(
        quality.maxBladesPerPatch,patch.maximumBlades);
    const uint32_t keep=uint32_t(
        uint64_t(patch.coveragePermille)*
        appearance.foliagePermille/1000u);
    if(candidates) {
        model->vertices.reserve(size_t(candidates)*
            (quality.crossedBlades?6u:3u));
        model->indices.reserve(size_t(candidates)*
            (quality.crossedBlades?6u:3u));
    }
    const int64_t spanX=patch.bounds.maxX-patch.bounds.minX;
    const int64_t spanZ=patch.bounds.maxZ-patch.bounds.minZ;
    const int32_t baseWidthCm=6;
    const int32_t baseHeightCm=26;
    const uint32_t light=patch.environment.lightPermille;
    const auto rgba=[](uint32_t r,uint32_t g,uint32_t b){
        return (0xffu<<24) | (b<<16) | (g<<8) | r;
    };
    bool burningBlade=false;
    const auto emitTriangle=[&](render::float3 a,render::float3 b,
                                render::float3 tip,uint32_t color){
        const auto normal=render::normalize(
            render::cross(b-a,tip-a));
        for(const auto& p:{a,b,tip}) {
            const bool top=p.y>std::max(a.y,b.y);
            render::ModelVertex v;
            v.pos=p;
            v.nrm=normal;
            // Wind sway: alpha = root(0)..tip(1) weight for canopyWindGraph.
            v.color=sway ? ((color&0x00ffffffu)|(top?0xff000000u:0u)) : color;
            v.uv={top?0.5f:0.0f,top?1.0f:0.0f};
            (burningBlade?burningIndices:model->indices).push_back(
                static_cast<uint32_t>(model->vertices.size()));
            model->vertices.push_back(v);
        }
    };
    for(uint32_t slot=0;slot<candidates;++slot) {
        // Stable stratification of a large logical grass population:
        // changing coverage does not move the survivors.
        const uint32_t logicalIndex=uint32_t(
            uint64_t(slot)*patch.maximumBlades/candidates);
        const uint64_t h=detail::mix64(
            patch.seed^patch.key^
            (uint64_t(logicalIndex)*0x9e3779b97f4a7c15ULL));
        if(h%1000u>=keep)continue;
        const int64_t x=patch.bounds.minX+
            int64_t((h>>13)%uint64_t(spanX));
        const int64_t z=patch.bounds.minZ+
            int64_t((h>>35)%uint64_t(spanZ));
        const int32_t y=ground(x,z);
        if(y< -1000000 || y>1000000)
            throw std::out_of_range("TerraWeave grass soil height");
        const int32_t visualSize=std::max<int32_t>(
            1,int32_t(appearance.sizePermille)*
            (750+int32_t((h>>20)%501u))/1000);
        float bladeHeight=float(baseHeightCm)*visualSize*0.00001f;
        const float bladeWidth=float(baseWidthCm)*
            (850+float((h>>46)%301u))/100000.f;
        const float mx=float(x-patch.bounds.minX)*0.01f;
        const float mz=float(z-patch.bounds.minZ)*0.01f;
        const float my=float(y)*0.01f;
        const float bias=(float(int32_t((h>>48)%101u)-50))*0.0003f;
        const uint32_t shade=uint32_t(std::clamp<int>(
            int(light*220u/1000u)+int((h>>29)%36u),70,255));
        uint32_t color=rgba(shade,shade,
            uint32_t(std::max(1u,shade*3u/4u)));
        burningBlade=false;
        if(fireSampler) {
            const auto burn=(*fireSampler)(x,z);
            const uint32_t charred=std::min<uint32_t>(burn.charPermille,1000u);
            const uint32_t fire=std::min<uint32_t>(burn.firePermille,1000u);
            // Burned blades shrink to stubble and turn to ash; a per-blade
            // threshold keeps the front ragged instead of a hard line.
            const uint32_t edge=uint32_t((h>>40)%200u);
            const uint32_t c=charred>edge ? std::min<uint32_t>(1000u,
                (charred-edge)*1000u/std::max<uint32_t>(1u,1000u-edge)) : 0u;
            bladeHeight*=float(1000u-c*82u/100u)/1000.0f;
            const auto mixc=[&](uint32_t from,uint32_t to,uint32_t t){
                return (from*(1000u-t)+to*t)/1000u;
            };
            const uint32_t ash=uint32_t(26+(h>>51)%22u);
            const uint32_t r=mixc(shade,ash+6u,c);
            const uint32_t g=mixc(shade,ash+2u,c);
            const uint32_t b=mixc(std::max(1u,shade*3u/4u),ash,c);
            color=rgba(r,g,b);
            if(fire>=kGrassBurningFirePermille && c<950u) {
                burningBlade=true;
                const uint32_t hot=std::min<uint32_t>(255u,150u+fire*105u/1000u);
                color=rgba(255u,hot*2u/3u,hot/5u);
            }
        }
        emitTriangle({mx-bladeWidth,my,mz},
                     {mx+bladeWidth,my,mz},
                     {mx+bias,my+bladeHeight,mz+bias},color);
        if(quality.crossedBlades)
            emitTriangle({mx,my,mz-bladeWidth},
                         {mx,my,mz+bladeWidth},
                         {mx-bias,my+bladeHeight,mz+bias},color);
    }
    primitive.firstVertex=0;
    primitive.vertexCount=static_cast<uint32_t>(model->vertices.size());
    primitive.firstIndex=0;
    primitive.indexCount=static_cast<uint32_t>(model->indices.size());
    if(!primitive.indexCount && burningIndices.empty())
        return {}; // completely bare patch: no GPU object
    if(primitive.indexCount)mesh.primitives.push_back(primitive);
    if(!burningIndices.empty()) {
        render::Primitive glowing=primitive;
        glowing.material=1;
        glowing.firstIndex=static_cast<uint32_t>(model->indices.size());
        glowing.indexCount=static_cast<uint32_t>(burningIndices.size());
        model->indices.insert(model->indices.end(),
                              burningIndices.begin(),burningIndices.end());
        mesh.primitives.push_back(glowing);
    }
    model->meshes.push_back(std::move(mesh));
    render::Node node;
    node.name="Grass Patch";
    node.mesh=0;
    model->nodes.push_back(std::move(node));
    model->finalize();
    return model;
}

// Device-owned residency/culling. Caller publishes new patch snapshots after
// one ECS tick and provides a terrain sampler valid on the current owner
// thread only. No worker captures mutable Earthworks or runs gameplay code.
class GrassPatchRenderer {
public:
    explicit GrassPatchRenderer(GrassRenderQuality quality={})
        :quality_(quality) {validateGrassRenderQuality(quality_);}
    size_t residentPatches()const{return resident_.size();}
    const GrassRenderQuality& quality()const{return quality_;}
    void setQuality(GrassRenderQuality quality) {
        validateGrassRenderQuality(quality);
        if(quality.maxBladesPerPatch!=quality_.maxBladesPerPatch ||
           quality.crossedBlades!=quality_.crossedBlades)
            for(const auto& [key,entry]:resident_){
                (void)entry;dirty_.insert(key);
            }
        quality_=quality;
    }

    // Optional presentation overlay (see GrassPresentationOptions). The
    // signature callback returns a cheap appearance hash per patch rectangle
    // (e.g. NaturePresentationFrame::signature); a patch is rebuilt only when
    // its hash changes, still within maxUploadsPerFrame.
    void setPresentation(GrassPresentationOptions options,
                         std::function<uint64_t(const RectCm&)> signature={}) {
        const bool layout=options.windSway!=presentation_.windSway ||
                          bool(options.fire)!=bool(presentation_.fire);
        presentation_=std::move(options);
        signature_=std::move(signature);
        if(layout)invalidateAll();
    }
    void clearPresentation(){setPresentation({}, {});}
    // Per-frame wind for windSway (world m/s, x/z), animation phase radians
    // and gust 0..1. Bounded tip lean 0.14 m at >= 16 m/s.
    void setWind(float xMetresPerSecond,float zMetresPerSecond,
                 float phaseRadians,float gust=0.0f) {
        const auto finite=[](float v){return std::isfinite(v)?v:0.0f;};
        const float x=finite(xMetresPerSecond),z=finite(zMetresPerSecond);
        const float speed=std::sqrt(x*x+z*z);
        const float lean=0.14f*std::clamp(speed/16.0f,0.0f,1.0f);
        const float k=speed>1e-4f?lean/speed:0.0f;
        wind_={x*k,z*k,finite(phaseRadians),std::clamp(finite(gust),0.0f,1.0f)};
    }

    // Mark only grass whose footprint intersects disturbed ground.
    // Native ECS rollback/save restore invalidates all retained render
    // geometry without touching authoritative grass coverage.
    void invalidateAll(){
        for(const auto& [id,patch]:resident_){
            (void)patch;
            dirty_.insert(id);
        }
    }

    void notifyTerrainEdited(const EarthworkChange& change){
        if(change.vertices.empty())return;
        for(const auto& [id,resident]:resident_)
            if(resident.patch.bounds.minX<change.dirty.maxX &&
               resident.patch.bounds.maxX>change.dirty.minX &&
               resident.patch.bounds.minZ<change.dirty.maxZ &&
               resident.patch.bounds.maxZ>change.dirty.minZ)
                dirty_.insert(id);
    }

    // Submit at most N new/rebuilt GPU patch meshes. Old models stay resident
    // during upload failure; unused patches retire only once required patches
    // are ready (streamer load-before-unload).
    size_t update(gfx::Device& device,
                  std::span<const GrassPatch> visible,
                  const GrassSoilHeight& ground) {
        if(!ground || visible.size()>quality_.maxGpuPatches)
            throw std::invalid_argument("TerraWeave grass visible patch budget");
        std::map<uint64_t,GrassPatch> requested;
        for(const auto& patch:visible) {
            (void)prepareGrassPatch(patch);
            if(!patch.key || !requested.emplace(patch.key,patch).second)
                throw std::invalid_argument("TerraWeave grass duplicate key");
        }
        size_t uploaded=0;
        for(const auto& [id,patch]:requested) {
            const auto old=resident_.find(id);
            const uint64_t look=signature_?signature_(patch.bounds):0;
            if(old!=resident_.end() &&
               !dirty_.contains(id) &&
               sameGrassPresentation(patch,old->second.patch) &&
               old->second.signature==look)
                continue;
            if(uploaded>=quality_.maxUploadsPerFrame)break;
            const bool overlay=presentation_.windSway||bool(presentation_.fire);
            auto candidate=makeGrassPatchModel(patch,ground,quality_,
                                               overlay?&presentation_:nullptr);
            if(candidate) {
                render::prepareModelUpload(*candidate);
                if(!render::uploadModel(device,*candidate) ||
                   !candidate->vb.valid() || !candidate->ib.valid()) {
                    render::releaseModel(device,*candidate);
                    break; // retain previously published model and retry
                }
            }
            Resident next{patch,std::move(candidate),look};
            if(old!=resident_.end()){
                if(old->second.model)
                    render::releaseModel(device,*old->second.model);
                old->second=std::move(next);
            } else resident_.emplace(id,std::move(next));
            dirty_.erase(id);
            ++uploaded;
        }
        bool complete=true;
        for(const auto& [id,patch]:requested) {
            const auto found=resident_.find(id);
            if(found==resident_.end() ||
               dirty_.contains(id) ||
               !sameGrassPresentation(patch,found->second.patch)) {
                complete=false;
                break;
            }
        }
        if(complete)
            for(auto it=resident_.begin();it!=resident_.end();){
                if(!requested.contains(it->first)){
                    if(it->second.model)
                        render::releaseModel(device,*it->second.model);
                    dirty_.erase(it->first);
                    it=resident_.erase(it);
                }else ++it;
            }
        return uploaded;
    }

    void draw(render::Renderer3D& renderer,
              GrassRenderOriginCm origin={})const{
        for(const auto& [id,resident]:resident_){
            (void)id;
            if(!resident.model)continue;
            render::ModelDraw draw;
            draw.model=resident.model.get();
            draw.world=render::mat4::translate({
                float(resident.patch.bounds.minX-origin.x)*0.01f,
                float(-origin.y)*0.01f,
                float(resident.patch.bounds.minZ-origin.z)*0.01f
            });
            draw.flags=render::kDrawStatic|render::kDrawNoCastShadow;
            if(presentation_.windSway) {
                draw.useVertexParameters=true;
                draw.vertexParameters={wind_.x,wind_.y,wind_.z,wind_.w};
                draw.maxVertexOffsetMetres=0.25f;
            }
            renderer.drawModel(draw);
        }
    }

    void shutdown(gfx::Device& device){
        for(auto& [id,entry]:resident_) {
            (void)id;
            if(entry.model)render::releaseModel(device,*entry.model);
        }
        resident_.clear();
        dirty_.clear();
    }
private:
    struct Resident {
        GrassPatch patch{};
        std::unique_ptr<render::Model> model;
        uint64_t signature=0;
    };
    GrassRenderQuality quality_;
    GrassPresentationOptions presentation_;
    std::function<uint64_t(const RectCm&)> signature_;
    render::float4 wind_{0,0,0,0};
    std::map<uint64_t,Resident> resident_;
    std::set<uint64_t> dirty_;
};

} // namespace aiverse::terraweave
