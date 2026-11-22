#include "Workspace.h"
#include "engine/actor/ActorModule.h"
namespace aiverse::editor {
void registerFpsOps(Workspace& ws) {
    auto schema=Json::parse(R"({"type":"object","properties":{"rules":{"type":"object"},"map":{"type":"object"}},"additionalProperties":false})");
    ws.registerOp({"fps_get","Read the actor-session rules (rules.fps) and the open world-space map.",Json::parse(R"({"type":"object","properties":{}})"),[&ws](const Json&) {
        if(!ws.map.data.worldSpace)throw std::runtime_error("open a project with world-space maps first");
        Json result=Json::object();result.set("rules",ws.project.rulesJson);result.set("map",ws.map.toJson());return result;
    }});
    ws.registerOp({"fps_set","Validate and apply actor-session rules / world-space map atomically. Saves remain explicit.",schema,[&ws](const Json& args) {
        if(!ws.map.data.worldSpace)throw std::runtime_error("open a project with world-space maps first");
        auto rules=args.has("rules")?args["rules"]:ws.project.rulesJson;
        auto map=args.has("map")?args["map"]:ws.map.toJson();
        auto config=actor::readConfig(rules);Simulation sim({1,config.tickRate,0},std::make_unique<actor::Module>());sim.setup(rules,map);
        if(args.has("map")){ws.map.beginEdit();ws.map.data.extra = map; ws.map.data.worldSpace = true;ws.map.data.name=map.getStr("name","FPS");ws.map.touched();}
        if(args.has("rules")){ws.project.rulesJson=rules;ws.project.rulesDirty=true;ws.project.reloadRules();}
        ws.note({Change::Map,0,0,-1,-1,-1,"FPS updated"});Json result=Json::object();result.set("ok",Json::fromBool(true));return result;
    }});
}
}
