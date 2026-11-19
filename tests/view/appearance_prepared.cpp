// Stage 111 deferred: host-resolved appearance snapshots are published
// without replaying texture callbacks or building another animation clock.
#include "engine/view/AppearanceAssetRegistry.h"
#include <cassert>
#include <string>
#include <memory>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}

int main() {
    using namespace aiverse;
    render::Model model;
    model.path="models/unit.glb";
    model.materials.resize(1);
    model.materials[0].name="Body";
    auto graph=render::AnimationGraph::compile(model,Json::parse(
        R"({"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})"));

    view::AppearanceAssetRegistry registry;
    view::AppearanceAssetRegistry::Asset staged;
    staged.model=&model;
    staged.materials=model.materials;
    staged.materials[0].roughness=0.3f;
    staged.graph=graph;
    staged.sourceId=42;
    staged.graphDefinition="bind graph";
    staged.sources={"materials.json",model.path};

    std::string error;
    assert(registry.publishPrepared("character",staged,&error));
    auto first=registry.find("character");
    assert(first && first->version==1 && first->generation>0);
    assert(first->materials[0].roughness==0.3f);
    assert(first->graph==graph);
    view::Object packet;
    assert(first->packet(render::mat4{},packet));
    assert(packet.graph==graph && packet.draw.materials==&first->materials);

    // A material-only publication pins old materials while retaining
    // the exact compiled graph pointer/generation of the active player.
    staged.materials[0].roughness=0.6f;
    assert(registry.invalidateSources({"materials.json"})==1);
    assert(registry.publishPrepared("character",staged,&error));
    auto next=registry.find("character");
    assert(next && next!=first && next->version>first->version);
    assert(next->generation==first->generation && next->graph==first->graph);
    assert(first->materials[0].roughness==0.3f);
    assert(next->materials[0].roughness==0.6f);

    // Reject an invalid prepared node-map without modifying a working
    // appearance, dirty status or animation generation.
    assert(registry.invalidateSources({model.path})==1);
    const auto good=registry.find("character");
    staged.nodeMaterial={500};
    assert(!registry.publishPrepared("character",staged,&error));
    assert(!error.empty());
    assert(registry.dirty("character"));
    assert(registry.find("character")==good);
    staged.nodeMaterial.clear();

    auto other=render::AnimationGraph::compile(model,Json::parse(
        R"({"nodes":[{"id":"bind2","kind":"bind"}],"output":"bind2"})"));
    staged.graph=other;
    assert(registry.publishPrepared("character",staged,&error));
    auto changed=registry.find("character");
    assert(changed->graph==other && changed->generation>good->generation);
    assert(good->graph==graph);
    registry.clear();
    assert(!registry.find("character"));
    // Caller-pinned snapshot remains renderable until the host detaches it
    // and then releases model GPU/CPU storage.
    assert(changed->packet(render::mat4{},packet));
}
