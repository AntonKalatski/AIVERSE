// NEREID stage 7C: opt-in Renderer3D bridge for the universal water mesh.
// Game/scene owns the uploaded GPU handles and must release/rebuild them on
// content/LOD/quality change or device loss. Never called from Simulation.
#pragma once
#include "Renderer3D.h"
#include <aiverselib/water/NereidWaterSurfaceMesh.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::render {

// All color composition is presentation-only. Per-vertex shore strength
// tints the wave surface toward foam; shader animates normals/reflections.
inline uint32_t nereidWaterVertexColor(
        const lib::nereid::WaterSurfaceVertex& vertex,
        const lib::nereid::WaterSurfaceStyle& style={}) {
    const float foam=std::clamp(vertex.shore*style.foamStrength,0.0f,1.0f);
    auto channel=[foam](float deep,float rim) {
        const float x=std::clamp(deep+(rim-deep)*foam,0.0f,1.0f);
        return uint8_t(x*255.0f+0.5f);
    };
    return rgba(channel(style.deepR,style.foamR),
                channel(style.deepG,style.foamG),
                channel(style.deepB,style.foamB),255);
}
inline bool validNereidWaterStyle(const lib::nereid::WaterSurfaceStyle& s) {
    return std::isfinite(s.deepR)&&std::isfinite(s.deepG)&&std::isfinite(s.deepB)&&
           std::isfinite(s.foamR)&&std::isfinite(s.foamG)&&std::isfinite(s.foamB)&&
           std::isfinite(s.foamStrength)&&std::isfinite(s.opacity)&&
           std::isfinite(s.specular)&&s.opacity>0 && s.opacity<0.99f &&
           s.specular>=0.85f && s.specular<=1.f &&
           s.foamStrength>=0 && s.foamStrength<=1;
}

// Upload at content/render setup, not every simulation tick.
// A successfully built mesh has its own static VB/IB; release it explicitly.
inline Mesh uploadNereidWaterSurface(gfx::Device& device,
        const lib::nereid::WaterSurfaceMesh& surface,
        lib::nereid::WaterSurfaceStyle style={}) {
    if(surface.empty() || !validNereidWaterStyle(style))return {};
    MeshBuilder builder;
    for(const auto& v:surface.vertices)
        builder.addVertex({v.x,v.y,v.z},{0.f,1.f,0.f},
                          nereidWaterVertexColor(v,style));
    for(size_t i=0;i<surface.indices.size();i+=3)
        builder.addTriangle(surface.indices[i],surface.indices[i+1],
                            surface.indices[i+2]);
    return builder.upload(device);
}
inline void drawNereidWaterSurface(Renderer3D& renderer,const Mesh& mesh,
        lib::nereid::WaterSurfaceStyle style={}) {
    if(mesh.indexCount==0 || !validNereidWaterStyle(style))return;
    renderer.drawMesh(mesh,mat4::identity(),
                      {1.f,1.f,1.f,style.opacity},
                      kDrawNoCastShadow|kDrawWaterSurface,
                      style.specular);
}
inline void releaseNereidWaterSurface(gfx::Device& device,Mesh& mesh) {
    if(mesh.vb.valid())device.destroyBuffer(mesh.vb);
    if(mesh.ib.valid())device.destroyBuffer(mesh.ib);
    mesh={};
}

} // namespace aiverse::render
