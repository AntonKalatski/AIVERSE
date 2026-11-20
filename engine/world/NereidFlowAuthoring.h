// Typed authoring operations; hosts own Undo/save transactions.
#pragma once
#include "engine/world/NereidWaterManifest.h"
namespace aiverse::world {
inline Json editNereidFlow(const Json& root,const Json& input){
    auto result=root;auto water=result["nereidWater"];
    if(!input["id"].isNumber())throw JsonError("NEREID flow requires id");
    size_t found=water["surfaces"].size();
    for(size_t i=0;i<water["surfaces"].size();++i)if(water["surfaces"][i]["id"].integer()==input["id"].integer())found=i;
    if(found==water["surfaces"].size())throw JsonError("NEREID water id not found");
    auto surface=water["surfaces"][found];auto grade=surface["grade"].isObject()?surface["grade"]:Json::object();
    const auto mode=input.getStr("mode","automatic");if(mode!="automatic"&&mode!="manual")throw JsonError("NEREID flow mode");
    grade.set("automaticCurrent",Json::fromBool(mode=="automatic"));
    if(input.has("flow"))surface.set("flow",input["flow"]);
    for(auto key:{"slopeDegrees","downhillHeadingDegrees","responseSeconds","maxSpeed","origin"})if(input.has(key))grade.set(key,input[key]);
    if(!grade.has("slopeDegrees")&&!grade.has("risePerMetre"))grade.set("slopeDegrees",Json::number(mode=="automatic"?1.0:0.0));
    surface.set("grade",grade);water.member("surfaces").at(found)=surface;result.set("nereidWater",water);
    (void)parseNereidWaterManifest(result);return result;
}
inline Json nereidFlowState(const Json& root){
    auto result=root["nereidWater"].isObject()?root["nereidWater"]:Json::object().set("surfaces",Json::array());
    auto computed=Json::array();for(const auto& s:parseNereidWaterManifest(root).surfaces){
        const auto& g=s.mesh.grade;const Vec3 p{g.origin.x,{},g.origin.y};
        const auto body=lib::nereid::gradedWaterBody(s.body,s.mesh.useGrade?&g:nullptr,p,s.useGeometry?&s.geometry:nullptr);
        auto e=Json::object(),flow=Json::array();flow.push(Json::number(body.flow.x.toDouble()));flow.push(Json::number(body.flow.y.toDouble()));flow.push(Json::number(body.flow.z.toDouble()));e.set("id",Json::number(int64_t(s.id)));e.set("flow",flow);computed.push(e);
    }result.set("computed",computed);return result;
}
}
