// Stage 110 deferred: authored FPS/RTS material layers and node variants
// use the same genre-independent CPU policy and transaction builder.
#include "engine/view/AppearanceAssetRegistry.h"
#include <cassert>
#include <string>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}

int main() {
    using namespace aiverse;
    render::Model model;
    model.path="models/unit.glb";
    model.materials.resize(2);
    model.materials[0].name="Body";
    model.materials[1].name="Armor";
    model.nodes.resize(3);
    model.nodes[0].name="Root";
    model.nodes[0].children={1};
    model.nodes[1].name="Arm";
    model.nodes[1].mesh=0;
    model.nodes[1].children={2};
    model.nodes[2].name="ArmPlate";
    model.nodes[2].mesh=0;

    const Json rules=Json::parse(R"({
        "materials":{
            "*":{"roughness":0.8},
            "Armor":{"roughness":0.3,"metallic":0.5}
        },
        "assign":{"Arm*":"glow","Miss*":{"roughness":0.2}}
    })");
    const Json library=Json::parse(R"({
        "glow":{"emissive":[0.2,0.4,0.6],"emissiveStrength":3}
    })");
    std::vector<std::string> warnings;
    auto warn=[&](const std::string& msg){warnings.push_back(msg);};
    auto choice=view::AppearanceChoiceAuthoring::resolve(model,rules,warn);
    assert(choice.materialLayers.size()==2);
    assert(choice.materialLayers[0].size()==1);
    assert(choice.materialLayers[1].size()==2);
    assert(choice.extraMaterials.size()==1);
    assert(choice.extraMaterials[0].name=="glow");
    assert(choice.nodeMaterial.size()==3);
    assert(choice.nodeMaterial[0]==-1);
    assert(choice.nodeMaterial[1]==2 && choice.nodeMaterial[2]==2);
    assert(choice.variant==-1);
    assert(warnings.size()==1); // unmatched authored node pattern

    view::AppearanceAssetRegistry assets;
    view::AppearanceAssetRegistry::Request request;
    request.model=&model;
    request.appearance=rules;
    request.materialLibrary=library;
    request.sources={"assets/looks.json"};
    std::string error;
    assert(assets.publish("unit",request,{},warn,&error));
    const auto first=assets.find("unit");
    assert(first && first->materials.size()==3);
    assert(first->materials[0].roughness==0.8f);
    assert(first->materials[1].roughness==0.3f);
    assert(first->materials[1].metallic==0.5f);
    assert(first->materials[2].emissiveStrength==3.f);
    assert(first->nodeMaterial[1]==2 && first->nodeMaterial[2]==2);
    assert(first->generation==0);

    view::Object packet;
    assert(first->packet(render::mat4{},packet));
    assert(packet.draw.materials==&first->materials);
    assert(packet.draw.nodeMaterial==&first->nodeMaterial);

    // Unchanged graph inputs are allowed to keep playback generation across
    // a material-only update; Model reimports invalidate source identity.
    request.animationGraph=Json::parse(R"({
        "nodes":[{"id":"bind","kind":"bind"}],
        "output":"bind"
    })");
    request.sourceId=17;
    assert(assets.publish("unit",request,{},warn,&error));
    const auto animated=assets.find("unit");
    assert(animated && animated->graph && animated->generation>0);
    const auto originalGeneration=animated->generation;
    assert(assets.invalidateSources({"assets/looks.json"})==1);
    assert(assets.publish("unit",request,{},warn,&error));
    assert(assets.find("unit")->generation==originalGeneration);
    request.modelRevision=1; // same Model address, changed underlying import
    assert(assets.publish("unit",request,{},warn,&error));
    assert(assets.find("unit")->generation>originalGeneration);
    const auto gen=assets.find("unit")->generation;
    assert(assets.invalidateSources({"models/unit.glb"})==1);
    assert(assets.publish("unit",request,{},warn,&error));
    assert(assets.find("unit")->generation>gen);
    assert(assets.find("unit")->model==&model);
}
