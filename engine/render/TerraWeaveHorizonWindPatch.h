// TerraWeave Horizon: bounded GPU colour-attribute updates for exact distant
// wind from an immutable PHYSIS/game wind lattice. Render-owner thread only.
// Reuses one uploaded forest vertex range, never reallocates atlas/geometry.
#pragma once
#include "engine/render/TerraWeaveVertexWind.h"
#include "engine/world/TerraWeaveWindField.h"
#include "engine/render/Model.h"
#include "engine/gfx/Gfx.h"
#include "engine/world/TerraWeaveHorizon.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct HorizonForestWindOverlay {
    uint32_t firstVertex=0;
    std::vector<render::ModelVertex> vertices; // contiguous forest-only copy
    std::vector<std::pair<int64_t,int64_t>> centersCm;
    uint64_t appliedRevision=0; // render presentation field revision, not bake
};

inline uint32_t horizonFieldWindVertexColor(
        const ForestWindState& sample,bool canopyTop) {
    const auto signedChannel=[](int32_t v)->uint32_t {
        // v is validated as part of the wind field and remains in +/-5000.
        const int64_t clamped=std::clamp<int64_t>(v,-5000,5000);
        const int64_t quantized=clamped>=0 ?
            (clamped*127+2500)/5000 :
            -((-clamped*127+2500)/5000);
        return static_cast<uint32_t>(128+quantized);
    };
    const uint32_t x=signedChannel(sample.xCmPerSec);
    const uint32_t z=signedChannel(sample.zCmPerSec);
    const uint32_t gust=std::min<uint32_t>(255u,
        (sample.gustPermille*255u+500u)/1000u);
    return (canopyTop ? 0xff000000u : 0u)|
           (gust<<16)|(z<<8)|x;
}

// Build on the CPU from a not-yet-uploaded Horizon model, on the owning
// publication thread. One combined forest primitive, two cards / cluster,
// four vertices / card. Runtime source remains the normal HorizonBake.
inline HorizonForestWindOverlay captureHorizonForestWindOverlay(
        const HorizonBake& bake,const render::Model& prepared) {
    HorizonForestWindOverlay overlay;
    if(bake.clusters.empty() || prepared.meshes.size()!=1)
        return overlay;
    const auto& primitives=prepared.meshes[0].primitives;
    const auto it=std::find_if(primitives.begin(),primitives.end(),
        [](const render::Primitive& p){return p.material==1;});
    if(it==primitives.end())return overlay;
    const size_t vertexCount=bake.clusters.size()*8u;
    constexpr size_t kMaxForestWindBytes=2u*1024u*1024u;
    if(it->vertexCount!=vertexCount ||
       vertexCount>kMaxForestWindBytes/sizeof(render::ModelVertex) ||
       it->firstVertex>prepared.vertices.size() ||
       vertexCount>prepared.vertices.size()-it->firstVertex)
        return overlay;
    overlay.firstVertex=it->firstVertex;
    overlay.vertices.assign(
        prepared.vertices.begin()+it->firstVertex,
        prepared.vertices.begin()+it->firstVertex+vertexCount);
    overlay.centersCm.reserve(bake.clusters.size());
    for(const auto& c:bake.clusters)
        overlay.centersCm.emplace_back(c.centerXcm,c.centerZcm);
    return overlay;
}

struct HorizonForestWindPatchResult {
    bool ready=false;         // overlay colour data now reflects revision
    bool uploaded=false;      // true only when an RHI buffer write occurred
    size_t bytes=0;           // exact bytes sent to the RHI
};

// All-or-nothing forest range patch; bounded to a single contiguous GPU
// write per model. Do not partially update a visible forest with mixed
// old/new PHYSIS snapshots. Call only from HorizonScene::update(device).
inline HorizonForestWindPatchResult applyHorizonForestWindOverlay(
        gfx::Device& device,render::Model& resident,
        HorizonForestWindOverlay& overlay,
        const ForestWindField* field,
        const ForestWindState& fallback,
        uint64_t desiredRevision,
        size_t remainingUploadBytes) {
    if(overlay.appliedRevision==desiredRevision)
        return {true,false,0};
    const size_t count=overlay.vertices.size();
    const size_t bytes=count*sizeof(render::ModelVertex);
    if(count==0 || count!=overlay.centersCm.size()*8u ||
       !resident.vb.valid() || !resident.ib.valid() ||
       resident.geometryPoolPage || resident.gpuBaseVertex!=0 ||
       resident.gpuFirstIndex!=0 || resident.batching.allowGeometryPool ||
       !device.canUpdateSharedBuffers() ||
       bytes>2u*1024u*1024u)
        return {};
    std::vector<render::ModelVertex> replacement=overlay.vertices;
    bool changed=false;
    for(size_t i=0;i<overlay.centersCm.size();++i){
        const auto [x,z]=overlay.centersCm[i];
        const ForestWindState local=sampleForestWindField(
            field,x,z,fallback);
        for(size_t j=0;j<8u;++j){
            const size_t index=i*8u+j;
            const uint32_t colour=field ?
                horizonFieldWindVertexColor(local,(j%4u)>=2u) :
                horizonSpatialWindVertexColor(
                    x,z,0x544552524157494eULL,(j%4u)>=2u);
            if(replacement[index].color!=colour) {
                replacement[index].color=colour;
                changed=true;
            }
        }
    }
    if(changed) {
        if(bytes>remainingUploadBytes)return {};
        const size_t offset=size_t(overlay.firstVertex)*
                            sizeof(render::ModelVertex);
        // No geometry pool for Horizon; the bounds/indices/layout are
        // unchanged. RHI updateBuffer is void: device-loss isn't detectable
        // here and still requires higher-level device recovery.
        device.updateBuffer(resident.vb,replacement.data(),bytes,offset);
        overlay.vertices.swap(replacement);
    }
    overlay.appliedRevision=desiredRevision;
    return {true,changed,changed?bytes:0};
}

} // namespace aiverse::terraweave
