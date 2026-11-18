// SpectraFX graph-v2: typed value-node DAG + module pipeline frontend.
// V2 is a strict, purely compile-time frontend to the existing effect parser
// and CPU runtime. It never interprets arbitrary code or mutates ECS state.
#pragma once
#include "SpectraValueGraph.h"
#include "VfxGraph.h"
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::fx::vfx2 {
inline bool isOutput(std::string_view t) {
    return t=="SpriteOutput" || t=="MeshOutput" || t=="RibbonOutput";
}
inline const char* moduleKey(std::string_view t) {
    if(t=="NoiseModule")return "noise";
    if(t=="CollisionModule")return "collision";
    if(t=="TrailModule")return "trail";
    if(t=="LightModule")return "light";
    if(t=="ForceModule")return "force";
    if(t=="VelocityModule")return "velocityOverLife";
    if(t=="SubEmitterModule")return "subEmitters";
    if(t=="TriggerModule")return "trigger";
    if(t=="InheritVelocityModule")return "inheritVelocity";
    if(t=="SpeedLimitModule")return "speedLimitAxes";
    if(t=="ColorBySpeedModule")return "colorBySpeed";
    if(t=="SizeBySpeedModule")return "sizeBySpeed";
    if(t=="SpinBySpeedModule")return "spinBySpeed";
    if(t=="LifeBySpeedModule")return "lifeBySpeed";
    return nullptr;
}
inline bool isStage(std::string_view t) {
    if(t=="Spawn"||t=="Initialize"||t=="Update"||isOutput(t)||moduleKey(t))
        return true;
    return t=="ShapeModule"||t=="FlipbookModule"||t=="MaterialModule"||
           t=="SortModule"||t=="CustomModule";
}
inline bool nestedSet(Json& root,const std::string& dotted,Json value) {
    if(!root.isObject() || dotted.empty() || dotted.size()>128)return false;
    Json* target=&root;
    size_t from=0;
    unsigned depth=0;
    while(from<dotted.size()) {
        const size_t dot=dotted.find('.',from);
        const std::string key=dotted.substr(from,dot==std::string::npos?
                                            std::string::npos:dot-from);
        if(key.empty() || key.size()>64 || ++depth>6)return false;
        for(char c:key)
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                 (c>='0'&&c<='9')||c=='_'))return false;
        if(dot==std::string::npos) {
            target->set(key,std::move(value));
            return true;
        }
        Json* next=target->find(key);
        if(!next) { target->set(key,Json::object()); next=target->find(key); }
        if(!next || !next->isObject())return false;
        target=next;
        from=dot+1;
    }
    return false;
}
} // namespace aiverse::fx::vfx2

namespace aiverse::fx {

// V2 graph schema: values may be embedded as {"$node":"math_id"} or
// {"$param":"name"}; normalized-age expressions use
// {"$curve":"expression_id","samples":33} and compile into FxRange LUT keys.
// links[] with {from,to,toPort} are typed value connections into a node's
// property (a simple dotted path); {from,to} are execution/module links.
//
// Example:
// {"version":2,"parameters":{"speed":4},
//  "nodes":[{"id":"speed","type":"Parameter","name":"speed"},
//           {"id":"double","type":"Multiply","properties":{
//               "a":{"$node":"speed"},"b":2}},
//           {"id":"sp","type":"Spawn","properties":{"burst":12}},
//           {"id":"init","type":"Initialize","properties":{}},
//           {"id":"noise","type":"NoiseModule","properties":{"strength":0.2}},
//           {"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:soft"}}],
//  "links":[{"from":"sp","to":"init"},{"from":"init","to":"noise"},
//           {"from":"noise","to":"out"},
//           {"from":"double","to":"init","toPort":"speed"}],
//  "outputs":["out"]}
//
// All new module types convert to strict "Update" nodes in v1 with their
// property names mapped to FxEffect modules. Validation is delegated to
// the same parseEffect CPU backend as every existing graph.
// Static expression DAGs are compile-time evaluated, not per-particle GPU
// kernels. Only explicitly sampled $curve expressions vary over life.
inline bool compileSpectraGraphV2(const Json& graph,const Json& overrides,
                                 EffectDef& out,std::vector<std::string>& errors) {
    errors.clear();
    out={};
    auto fail=[&](const std::string& why) {
        errors.push_back("vfx graph v2: "+why);
        return false;
    };
    if(!graph.isObject() || graph.getInt("version")!=2)
        return fail("expected graph version 2");
    for(const auto& [key,unused]:graph.members()) {
        (void)unused;
        if(key!="version" && key!="parameters" && key!="nodes" &&
           key!="links" && key!="outputs" && key!="forceFields" &&
           key!="metadata" && key!="description")
            return fail("unknown root key '"+key+"'");
    }
    const Json& nodes=graph["nodes"],&links=graph["links"],&outputs=graph["outputs"];
    if(!nodes.isArray()||nodes.size()==0||nodes.size()>256)
        return fail("nodes must have 1..256 entries");
    if(!links.isArray()||links.size()>512)
        return fail("links must be an array with at most 512 entries");
    if(!outputs.isArray()||outputs.size()==0||outputs.size()>16)
        return fail("outputs must have 1..16 entries");

    std::map<std::string,Json> copied;
    std::map<std::string,std::string> types;
    for(const Json& original:nodes.items()) {
        const std::string id=original.getStr("id"), type=original.getStr("type");
        if(!vfx2::isStage(type) && !vfx2::valueNodeType(type))
            return fail("unknown node type '"+type+"' at '"+id+"'");
        if(original.has("properties") && !original["properties"].isObject())
            return fail("properties at '"+id+"' must be an object");
        if(original.has("position")) {
            const Json& p=original["position"];
            if(!p.isArray()||p.size()!=2||
               !p[size_t(0)].isNumber()||!p[size_t(1)].isNumber()||
               !std::isfinite(p[size_t(0)].toDouble())||
               !std::isfinite(p[size_t(1)].toDouble())||
               std::abs(p[size_t(0)].toDouble())>100000||
               std::abs(p[size_t(1)].toDouble())>100000)
                return fail("invalid node position at '"+id+"'");
        }
        types[id]=type;
        copied.emplace(id,original);
    }
    Json executionLinks=Json::array();
    std::set<std::pair<std::string,std::string>> destinationPorts;
    for(const Json& link:links.items()) {
        if(!link.isObject()||!link["from"].isString()||!link["to"].isString())
            return fail("link must have string from/to");
        const std::string from=link.getStr("from"),to=link.getStr("to");
        if(!copied.count(from)||!copied.count(to)||from==to)
            return fail("invalid graph link '"+from+"' -> '"+to+"'");
        const bool port=link.has("toPort");
        for(const auto& [key,unused]:link.members()) {
            (void)unused;
            if(key!="from"&&key!="to"&&key!="toPort"&&key!="fromPort")
                return fail("unknown link key '"+key+"'");
        }
        if(port) {
            const std::string target=link.getStr("toPort");
            if(!vfx2::valueNodeType(types.at(from)))
                return fail("data source must be a value node '"+from+"'");
            if(!destinationPorts.emplace(to,target).second)
                return fail("duplicate writer to '"+to+"."+target+"'");
            if(link.has("fromPort") && link.getStr("fromPort")!="value")
                return fail("value nodes expose only the value output");
            Json props=copied.at(to)["properties"];
            if(!props.isObject())props=Json::object();
            Json reference=Json::object();
            reference.set("$node",Json::string(from));
            if(!vfx2::nestedSet(props,target,std::move(reference)))
                return fail("invalid data input path '"+to+"."+target+"'");
            copied.at(to).set("properties",std::move(props));
        } else {
            if(link.has("fromPort"))
                return fail("execution link cannot declare fromPort");
            if(vfx2::valueNodeType(types.at(from))||vfx2::valueNodeType(types.at(to)))
                return fail("data-node links need toPort");
            Json flat=Json::object();
            flat.set("from",Json::string(from));
            flat.set("to",Json::string(to));
            executionLinks.push(std::move(flat));
        }
    }

    // Data links modify a COPY of the entire graph. The typed evaluator
    // must read that augmented copy as well; otherwise edges between two
    // math nodes would resolve against stale original properties.
    Json augmented=Json::array();
    for(const Json& n:nodes.items())augmented.push(copied.at(n.getStr("id")));
    vfx2::ValueGraph values(augmented,graph["parameters"],overrides,errors);
    if(!values.initialize() || !values.validateAll())return false;

    // Stage nodes keep their original authored IDs and positions but become
    // existing v1 pipeline nodes. Property expressions resolve before overlay.
    Json stages=Json::array();
    for(const Json& original:nodes.items()) {
        const std::string id=original.getStr("id"),type=types.at(id);
        if(vfx2::valueNodeType(type))continue;
        Json properties=Json::object();
        if(copied.at(id).has("properties") &&
           !values.resolve(copied.at(id)["properties"],properties))return false;
        Json stage=Json::object();
        stage.set("id",Json::string(id));
        if(type=="Spawn"||type=="Initialize"||type=="Update"||vfx2::isOutput(type))
            stage.set("type",Json::string(type));
        else {
            stage.set("type",Json::string("Update"));
            if(const char* key=vfx2::moduleKey(type)) {
                Json module=Json::object();
                if(type=="SubEmitterModule") {
                    // Existing CPU schema expects an array of event handlers.
                    // Keep properties an object for editable node pins while
                    // lowering its items array to subEmitters at compilation.
                    if(!properties.has("items") || !properties["items"].isArray())
                        return fail("SubEmitterModule needs properties.items array");
                    module.set(key,properties["items"]);
                } else module.set(key,std::move(properties));
                properties=std::move(module);
            } // Shape/Flipbook/Material/Sort/Custom carry flat emitter fields.
        }
        stage.set("properties",std::move(properties));
        if(original.has("position"))stage.set("position",original["position"]);
        stages.push(std::move(stage));
    }
    for(const Json& id:outputs.items()) {
        if(!id.isString()||!types.count(id.str())||!vfx2::isOutput(types.at(id.str())))
            return fail("output must reference a renderer stage");
    }
    Json v1=Json::object();
    v1.set("version",Json::number(int64_t(1)));
    v1.set("parameters",Json::object()); // references fully compiled/resolved
    v1.set("nodes",std::move(stages));
    v1.set("links",std::move(executionLinks));
    v1.set("outputs",outputs);
    if(graph.has("forceFields")) {
        Json fields;
        if(!values.resolve(graph["forceFields"],fields))return false;
        v1.set("forceFields",std::move(fields));
    }
    // Call the existing authoritative v1 compiler and FxEffect parser.
    return compileSpectraGraph(v1,Json::object(),out,errors);
}
inline bool compileSpectraGraphV2(const Json& graph,EffectDef& out,
                                 std::vector<std::string>& errors) {
    return compileSpectraGraphV2(graph,Json::object(),out,errors);
}

} // namespace aiverse::fx
