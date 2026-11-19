// TerraWeave GPU crown wind: first-party material ShaderGraph vertexOffset.
// Reuses Renderer3D's existing GLSL/MSL shader-injection and rigid instance U0.
// No new RHI buffers, shader backend, ECS components or per-tree asset clones.
#pragma once

#include "engine/world/TerraWeaveWind.h"
#include "engine/render/graph/ShaderGraph.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace aiverse::terraweave {

// The graph shader uses I.anim only for rigid, non-lightmapped instances
// explicitly opting into ModelDraw::vertexParameters. This Program is trusted
// engine-generated code, NOT arbitrary user-supplied shader source.
// Vertex alpha holds a 0..1 crown height weight and is ignored by the
// foliage fragment material (vertexColors=false).
inline const std::shared_ptr<const render::graph::Program>& canopyWindGraph() {
    static const auto program=[] {
        auto p=std::make_shared<render::graph::Program>();
        p->domain="material";
        p->minQuality=0; // keep the low material-quality/mobile fallback
        p->outputs["color"]="sg_input"; // unchanged fragment PBR/mask output
        p->outputs["vertexOffset"]="tw_offset";
        p->vertexGlsl=R"(
float tw_weight=clamp(sg_color.a,0.0,1.0);
tw_weight*=tw_weight;
float tw_phase=I.anim.z+sg_world.x*0.073+sg_world.z*0.051+
               sg_object.y*0.27;
float tw_wave=0.62+0.38*sin(tw_phase)+
              I.anim.w*0.25*sin(tw_phase*2.13+sg_object.x*0.43);
vec4 tw_offset=vec4(I.anim.x*tw_weight*tw_wave,0.0,
                    I.anim.y*tw_weight*tw_wave,0.0);
)";
        p->vertexMsl=R"(
float tw_weight=clamp(sg_color.a,0.0,1.0);
tw_weight*=tw_weight;
float tw_phase=I.anim.z+sg_world.x*0.073+sg_world.z*0.051+
               sg_object.y*0.27;
float tw_wave=0.62+0.38*sin(tw_phase)+
              I.anim.w*0.25*sin(tw_phase*2.13+sg_object.x*0.43);
float4 tw_offset=float4(I.anim.x*tw_weight*tw_wave,0.0,
                        I.anim.y*tw_weight*tw_wave,0.0);
)";
        return std::shared_ptr<const render::graph::Program>(std::move(p));
    }();
    return program;
}

// Existing rigid-instance I.anim = {local wind X, local wind Z, phase,
// secondary gust}. Caller supplies PHYSIS-neutral integer wind and tick.
// Convert world wind into the object's local yaw once, before GPU instancing.
// 0.35m maximum local displacement at maximum validated input.
inline render::float4 canopyWindVertexParameters(
        const ForestWindState& wind,uint64_t tick,
        float yawRadians=0.0f,float strength=1.0f) {
    if(!wind.xCmPerSec && !wind.zCmPerSec)return {};
    const float amplitude=0.35f*
        (float(wind.maxLeanMilliDegrees)/3000.0f)*
        (float(wind.branchFlexPermille)/1000.0f)*
        std::clamp(strength,0.0f,1.0f);
    const float x=amplitude*float(wind.xCmPerSec)/5000.0f;
    const float z=amplitude*float(wind.zCmPerSec)/5000.0f;
    const float c=std::cos(yawRadians),s=std::sin(yawRadians);
    constexpr float tau=6.2831853071795864769f;
    const float phase=tau*float(tick%wind.cycleTicks)/
                            float(wind.cycleTicks);
    return {c*x-s*z,s*x+c*z,phase,
            float(wind.gustPermille)/1000.0f};
}

// Conservative 0.5m local-space bound covers the maximum 0.35m xz
// component multiplied by (0.62+0.38+0.25) and diagonal displacement.
inline constexpr float kCanopyGpuWindMaxOffsetMetres=0.65f;

} // namespace aiverse::terraweave
