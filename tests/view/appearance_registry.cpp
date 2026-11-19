// Stage 109 deferred: transactional material/graph publication must not
// break the last visible appearance or reset a stable graph clock.
#include "engine/view/AppearanceAssetRegistry.h"
#include <cassert>
#include <string>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}

int main() {
    using namespace aiverse;
    using view::AppearanceAssetRegistry;

    render::Model model;
    model.materials.resize(1);
    model.materials[0].name="body";
    AppearanceAssetRegistry registry;
    AppearanceAssetRegistry::Request request;
    request.model=&model;
    request.sourceId=123;
    request.sources={"assets/materials.json","models/character.glb"};
    request.materialLibrary=Json::parse(R"({
        "base":{"roughness":0.8,"metallic":0.2},
        "paint":{"base":"base","roughness":0.4}
    })");
    request.materialOverrides={Json::string("paint")};
    request.parameters.set("speed",0.75f);

    std::string error;
    assert(registry.publish("character",request,{}, {},&error));
    auto first=registry.find("character");
    assert(first && first->version==1 && first->generation==0);
    assert(first->materials[0].roughness==0.4f);
    assert(first->materials[0].metallic==0.2f);
    assert(!registry.dirty("character"));
    view::Object packet;
    assert(first->packet(render::mat4{},packet));
    assert(packet.id==123 && packet.generation==0);
    assert(packet.draw.materials==&first->materials);
    assert(packet.parameters.values.at("speed")==0.75f);

    assert(registry.invalidateSources({"assets/materials.json"})==1);
    assert(registry.dirty("character"));
    assert(registry.find("character")==first); // old live snapshot survives
    request.materialLibrary=Json::parse(R"({
        "base":{"roughness":0.8,"metallic":0.2},
        "paint":{"base":"base","roughness":0.3}
    })");
    assert(registry.publish("character",request,{}, {},&error));
    auto second=registry.find("character");
    assert(second && second!=first && second->version>first->version);
    assert(!registry.dirty("character"));
    assert(second->materials[0].roughness==0.3f);
    assert(first->materials[0].roughness==0.4f); // pinned snapshot immutable

    // A failed shader program cannot silently erase an already working one.
    request.materialOverrides={Json::parse(R"({
        "shaderGraph":{"nodes":[],"domain":"invalid"}
    })")};
    assert(!registry.publish("character",request,{}, {},&error));
    assert(!error.empty());
    assert(registry.find("character")==second);

    // A valid external graph becomes an immutable single-writer View source.
    request.materialOverrides={Json::string("paint")};
    request.animationGraph=Json::string("graphs/character.json");
    request.graphReader=[](const std::string& file,std::string& content){
        if(file!="graphs/character.json")return false;
        content=R"({"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})";
        return true;
    };
    assert(registry.publish("character",request,{}, {},&error));
    auto animated=registry.find("character");
    assert(animated && animated->graph && animated->generation!=0);
    assert(animated->packet(render::mat4{},packet));
    assert(packet.graph==animated->graph);
    assert(packet.id==123 && packet.generation==animated->generation);
    assert(packet.parameters.values.at("speed")==0.75f);

    // Material-only rebuild keeps the graph pointer and animation generation.
    request.materialLibrary=Json::parse(R"({
        "paint":{"roughness":0.23}
    })");
    assert(registry.invalidateSources({"assets/materials.json"})==1);
    assert(registry.publish("character",request,{}, {},&error));
    auto materialOnly=registry.find("character");
    assert(materialOnly && materialOnly->graph==animated->graph);
    assert(materialOnly->generation==animated->generation);
    assert(materialOnly->version>animated->version);

    // The same graph path is recompiled after its source is invalidated.
    assert(registry.invalidateSources({"graphs/character.json"})==1);
    assert(registry.dirty("character"));
    assert(registry.publish("character",request,{}, {},&error));
    auto newGraph=registry.find("character");
    assert(newGraph->graph!=materialOnly->graph);
    assert(newGraph->generation>materialOnly->generation);

    // A broken external graph must not displace the previous animation.
    request.graphReader=[](const std::string&,std::string& out){
        out=R"({"nodes":[{"id":"broken","kind":"unknown"}],"output":"broken"})";
        return true;
    };
    registry.invalidateSources({"graphs/character.json"});
    assert(!registry.publish("character",request,{}, {},&error));
    assert(registry.find("character")==newGraph);
    assert(registry.dirty("character"));

    registry.erase("character");
    assert(!registry.find("character"));
    assert(!registry.dirty("character"));
    assert(newGraph->packet(render::mat4{},packet)); // caller still pins snapshot
}
