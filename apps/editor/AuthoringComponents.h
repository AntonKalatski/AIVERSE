// Opt-in authored entity presentation components. Document JSON is canonical.
// Legacy MapDoc pos owns X/Z; Transform owns Y/yaw/uniform scale. No mutable mirror.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "engine/core/FixedMath.h"
#include "engine/render/SceneHierarchyRender.h"
#include <cmath>
namespace aiverse::editor {
inline bool authoredEntity(const DocEntity& e){return e.extra["components"]["Transform"].isObject();}
inline std::string authoredModelAsset(const DocEntity& e){return e.extra["components"]["StaticModel"].getStr("asset");}
inline LocalTransform authoredLocalTransform(const DocEntity& e){
    const auto& t=e.extra["components"]["Transform"];
    if(!std::isfinite(e.x)||!std::isfinite(e.z)||std::abs(e.x)>1000000||std::abs(e.z)>1000000)throw JsonError("entity position extent");
    LocalTransform out;out.position={Fixed::fromRaw(int64_t(std::llround(double(e.x)*Fixed::kOne))),{},Fixed::fromRaw(int64_t(std::llround(double(e.z)*Fixed::kOne)))};
    for(auto key:{"y","yaw","scale"})if(t.has(key)&&!t[key].isNumber())throw JsonError("entity Transform requires numeric fields");
    out.position.y=t.getFx("y");const auto yaw=t.getFx("yaw"),scale=t.getFx("scale",1_fx);
    if(fxAbs(out.position.y)>1000000_fx||fxAbs(yaw)>360000_fx||scale<Fixed::fromRatio(1,1000)||scale>10000_fx)throw JsonError("entity Transform out of range");
    out.rotation.y=fxSinTurns(yaw/720_fx);out.rotation.w=fxCosTurns(yaw/720_fx);out.scale={scale,scale,scale};return out;
}
inline render::mat4 authoredModelMatrix(const DocEntity& e){return render::hierarchyMatrix(hierarchy_detail::localAffine(authoredLocalTransform(e)));}
inline DocEntity editAuthoredPlacement(const DocEntity& source,const std::string& key,const Json& value){
    if(!authoredEntity(source)||!value.isNumber()||(key!="y"&&key!="yaw"&&key!="scale"))throw JsonError("invalid authored Transform property");
    if(source.extra["components"].has("NereidWater")&&((key=="yaw"&&value.fx()!=0_fx)||(key=="scale"&&value.fx()!=1_fx)))throw JsonError("NEREID entity supports translation only");
    auto result=source;auto components=result.extra["components"];auto transform=components["Transform"];transform.set(key,value);components.set("Transform",transform);result.extra.set("components",components);(void)authoredLocalTransform(result);return result;
}
} // namespace aiverse::editor
