// Static model-preview bridge: emit a complete, borrowed View model packet
// without constructing a legacy RtsView or an animation player.
// RTS host selects resource/variant and supplies presentation appearance.
// The renderer owns actual model drawing and GPU state.
#pragma once
#include "engine/view/View.h"
#include <algorithm>
#include <cstdint>

namespace aiverse::game::view_adapter {

struct StaticPreviewModel {
    const render::Model* model=nullptr;
    const std::vector<render::Material>* materials=nullptr;
    const std::vector<int>* nodeMaterial=nullptr;
    render::GeometryPolicy geometry{};
    render::mat4 fix{};
    render::float4 tint{1,1,1,1};
    render::float3 team{1,1,1};
    int variant=-1;
    bool structure=false,shape=false,air=false,transparent=false;
    bool staticObject=false;
    float scale=1.f,sx=1.f,sz=1.f,flyHeight=0.f;
};

inline bool buildStaticPreviewPacket(const StaticPreviewModel& spec,
                                     float yaw,view::Object& packet) {
    if(!spec.model)return false;
    const float k=spec.scale;
    const render::float3 p{0,spec.air?spec.flyHeight:0,0};
    render::mat4 base;
    if(spec.structure && spec.shape)
        base=render::mat4::translate(p)*render::mat4::scale({
            spec.sx*k,k*std::max(spec.sx,spec.sz)*.75f,spec.sz*k});
    else
        base=render::mat4::translate(p)*render::mat4::rotateY(yaw)*
             render::mat4::scale({k,k,k});
    packet={};
    packet.draw.model=spec.model;
    packet.draw.geometry=spec.geometry;
    packet.draw.world=base*spec.fix;
    packet.draw.materials=spec.materials;
    packet.draw.nodeMaterial=spec.nodeMaterial;
    packet.draw.variant=spec.variant;
    packet.draw.tint=spec.tint;
    packet.draw.team=spec.team;
    if(spec.transparent)packet.draw.flags|=render::kDrawTransparent;
    if(spec.staticObject && !spec.air)
        packet.draw.flags|=render::kDrawStatic;
    // No graph is attached: never create a second animation owner.
    return true;
}

inline void submitStaticPreview(render::Renderer3D& renderer,
                                const view::Object& packet) {
    View::submitModel(renderer,packet.draw);
}

} // namespace aiverse::game::view_adapter
