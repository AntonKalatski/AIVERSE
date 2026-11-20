#pragma once
#include "StudioTerrainGeometry.h"
#include "StudioDocumentVisuals.h"
#include "engine/render/Model.h"
namespace aiverse::studio {
// Uses exactly the existing document preview geometry, not game assets. This
// PBR representation enables the shared receiver/GI passes on that preview.
inline std::unique_ptr<render::Model> studioLightingPreview(const editor::MapDoc& doc,
                                                         bool fps,float levelHeight) {
    using namespace render;
    MeshBuilder ground,box;box.color(rgba(255,255,255));box.box({0,0,0},{1,1,1});
    const auto terrain=buildTerrainPreview(doc.data,ground,levelHeight);
    if(!terrain.quads) {
        ground.color(rgba(46,66,84));
        const float w=float(std::max(1,doc.data.w)),h=float(std::max(1,doc.data.h));
        ground.box({w*.5f,-.08f,h*.5f},{w,.12f,h});
    }
    Material base;base.metallic=0;base.roughness=.8f;
    std::vector<ModelPart> parts{{"Terrain preview",&ground,base,mat4{}}};
    for(const auto& proxy:studioDocumentProxies(doc,fps,levelHeight)) {
        auto material=base;material.vertexColors=false;material.baseColor=proxy.color;
        parts.push_back({proxy.id,&box,material,mat4::translate(proxy.center)*
            mat4::rotateY(proxy.yaw)*mat4::scale(proxy.half*2.f)});
    }
    auto model=modelFromParts("studio/lighting-preview",parts);
    if(model)model->batching.autoLod=false;
    return model;
}
}
