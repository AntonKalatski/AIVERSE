// NEREID opt-in WorldScene JSON presentation manifest.
// This parser is not an authoritative ECS loader; native games may instead
// provide an ECS snapshot via WorldScene::setNereidSurfaces.
#pragma once
#include "engine/world/NereidWaterScene.h"
#include "engine/data/Json.h"
#include <string>
#include <vector>
#include <algorithm>

namespace aiverse::world {
struct NereidManifest {
    std::vector<NereidSceneSurface> surfaces;
    uint32_t maxUploadsPerFrame=2;
    size_t maxEstimatedUploadBytes=8u*1024u*1024u;
    float visibilityRange=0;
};

namespace nereid_manifest_detail {
inline Fixed coordinate(const Json& j,const char* field,Fixed max=100000_fx) {
    if(!j.isNumber() || j.fx() < -max || j.fx() > max)
        throw JsonError(std::string("nereidWater.")+field+": invalid number");
    return j.fx();
}
inline Fixed positive(const Json& j,const char* field,Fixed max=100000_fx) {
    const Fixed x=coordinate(j,field,max);
    if(x<=0_fx)throw JsonError(std::string("nereidWater.")+field+": must be positive");
    return x;
}
inline float scalar(const Json& j,const char* field,float lo,float hi) {
    if(!j.isNumber())throw JsonError(std::string("nereidWater.")+field+": expected number");
    const float v=float(j.fx().toDouble());
    if(!std::isfinite(v)||v<lo||v>hi)
        throw JsonError(std::string("nereidWater.")+field+": outside range");
    return v;
}
inline lib::nereid::WaterKind kind(const std::string& s) {
    using K=lib::nereid::WaterKind;
    if(s=="ocean")return K::Ocean;
    if(s=="lake")return K::Lake;
    if(s=="river")return K::River;
    if(s=="pool")return K::Pool;
    if(s=="puddle")return K::Puddle;
    throw JsonError("nereidWater.kind: unknown value");
}
inline lib::nereid::WaterSurfaceLod lod(const std::string& s) {
    using L=lib::nereid::WaterSurfaceLod;
    if(s=="near")return L::Near;
    if(s=="medium")return L::Medium;
    if(s=="far")return L::Far;
    throw JsonError("nereidWater.lod: unknown value");
}
inline void color(const Json& j,float& r,float& g,float& b) {
    if(!j.isArray()||j.size()!=3)throw JsonError("nereidWater.color: expected RGB triple");
    r=scalar(j[0],"color",0.f,1.f);
    g=scalar(j[1],"color",0.f,1.f);
    b=scalar(j[2],"color",0.f,1.f);
}
inline NereidSceneSurface surface(const Json& j) {
    using namespace lib::nereid;
    if(!j.isObject())throw JsonError("nereidWater.surface: expected object");
    NereidSceneSurface s;
    if(j.has("id"))s.id=uint64_t(worldInteger(j,"id",0,1,2147483647));
    s.body.kind=kind(j.getStr("kind","lake"));
    const auto shape=j.getStr("shape","box");
    if(shape=="infinite")s.body.footprint=WaterFootprint::Infinite;
    else if(shape=="box")s.body.footprint=WaterFootprint::Box;
    else if(shape=="disc")s.body.footprint=WaterFootprint::Disc;
    else if(shape=="polygon"||shape=="river") {
        s.useGeometry=true;
        s.geometry.kind=shape=="polygon"?WaterGeometryKind::Polygon:WaterGeometryKind::RiverSpline;
    }else throw JsonError("nereidWater.shape: unknown value");
    s.body.enabled=j.getBool("enabled",true);
    s.body.surfaceY=coordinate(j["surfaceY"],"surfaceY");
    s.body.bottomY=coordinate(j["bottomY"],"bottomY");
    if(s.body.surfaceY<=s.body.bottomY)
        throw JsonError("nereidWater: surfaceY must exceed bottomY");
    if(j.has("center"))s.body.center=worldVector(j["center"],100000_fx);
    if(j.has("flow"))s.body.flow=worldVector(j["flow"],1000_fx);
    if(j.has("priority"))s.body.priority=int32_t(worldInteger(j,"priority",0,-1000000,1000000));
    if(s.body.footprint==WaterFootprint::Box&&!s.useGeometry) {
        if(!j["halfExtents"].isArray()||j["halfExtents"].size()!=2)
            throw JsonError("nereidWater.halfExtents: expected [x,z]");
        s.body.halfExtents={
            positive(j["halfExtents"][0],"halfExtents",100000_fx),
            positive(j["halfExtents"][1],"halfExtents",100000_fx)};
    }
    if(s.body.footprint==WaterFootprint::Disc&&!s.useGeometry)
        s.body.radius=positive(j["radius"],"radius");
    if(s.useGeometry) {
        const auto& pts=j["points"];
        if(!pts.isArray()||pts.size()>kMaxWaterGeometryPoints)
            throw JsonError("nereidWater.points: invalid array");
        for(const auto& p:pts.items()) {
            const size_t required=s.geometry.kind==WaterGeometryKind::Polygon?2:3;
            if(!p.isArray()||p.size()!=required)
                throw JsonError("nereidWater.points: expected [x,z] or [x,z,width]");
            WaterGeometryPoint point;
            point.x=coordinate(p[0],"point.x");
            point.z=coordinate(p[1],"point.z");
            if(required==3)point.halfWidth=positive(p[2],"point.width");
            s.geometry.points.push_back(point);
        }
        if(!validWaterGeometry(s.geometry))
            throw JsonError("nereidWater.geometry: insufficient or invalid points");
    }
    if(j.has("lod"))s.mesh.lod=lod(j.getStr("lod"));
    if(j.has("oceanPatchRadius"))
        s.mesh.oceanPatchRadius=scalar(j["oceanPatchRadius"],"oceanPatchRadius",1.f,100000.f);
    if(j.has("uvScale"))s.mesh.uvScale=scalar(j["uvScale"],"uvScale",0.0001f,128.f);
    if(j.has("foamWidth"))s.mesh.foamWidth=scalar(j["foamWidth"],"foamWidth",0.f,1000.f);
    if(j.has("maxVertices"))
        s.mesh.maxVertices=uint32_t(worldInteger(j,"maxVertices",65536,4,65536));
    if(j.has("style")) {
        const auto& v=j["style"];
        if(!v.isObject())throw JsonError("nereidWater.style: expected object");
        if(v.has("deepColor"))color(v["deepColor"],s.style.deepR,s.style.deepG,s.style.deepB);
        if(v.has("foamColor"))color(v["foamColor"],s.style.foamR,s.style.foamG,s.style.foamB);
        if(v.has("opacity"))s.style.opacity=scalar(v["opacity"],"opacity",0.001f,0.98f);
        if(v.has("specular"))s.style.specular=scalar(v["specular"],"specular",0.85f,1.f);
        if(v.has("foamStrength"))s.style.foamStrength=scalar(v["foamStrength"],"foamStrength",0.f,1.f);
    }
    return s;
}
} // namespace nereid_manifest_detail

inline NereidManifest parseNereidWaterManifest(const Json& root) {
    NereidManifest out;
    if(!root.has("nereidWater"))return out;
    const auto& config=root["nereidWater"];
    if(!config.isObject())throw JsonError("nereidWater: expected object");
    out.maxUploadsPerFrame=uint32_t(worldInteger(config,"maxUploadsPerFrame",2,1,16));
    out.maxEstimatedUploadBytes=size_t(worldInteger(config,"maxEstimatedUploadBytes",8*1024*1024,1024,64*1024*1024));
    if(config.has("visibilityRange"))out.visibilityRange=nereid_manifest_detail::scalar(config["visibilityRange"],"visibilityRange",0.f,1000000.f);
    const auto& surfaces=config["surfaces"];
    if(!surfaces.isArray()||surfaces.size()>NereidWaterScene::kMaxSurfaces)
        throw JsonError("nereidWater.surfaces: expected at most 64");
    out.surfaces.reserve(surfaces.size());
    for(const auto& j:surfaces.items())
        out.surfaces.push_back(nereid_manifest_detail::surface(j));
    // Validate explicit and synthetic identifiers before constructing GPU state.
    std::vector<uint64_t> seen;
    for(size_t i=0;i<out.surfaces.size();++i) {
        const auto id=out.surfaces[i].id?out.surfaces[i].id:
            ((uint64_t(1)<<63)|(uint64_t(i)+1));
        if(std::find(seen.begin(),seen.end(),id)!=seen.end())
            throw JsonError("nereidWater: duplicate surface id");
        seen.push_back(id);
    }
    return out;
}
} // namespace aiverse::world
