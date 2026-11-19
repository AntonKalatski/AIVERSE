// Stage 107 deferred: typed generic appearance packet preserves all
// material/variant/tint/geometry/graph inputs without any game module.
#include "engine/view/AppearancePacket.h"
#include <cassert>
#include <memory>
#include <vector>
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse;
    using namespace aiverse::view;
    render::Model model;
    std::vector<render::Material> materials(2);
    std::vector<int> nodeMaterial{1,0};
    AppearanceSpec spec;
    spec.model=&model;
    spec.materials=&materials;
    spec.nodeMaterial=&nodeMaterial;
    spec.variant=7;
    spec.tint={0.5f,0.6f,0.7f,0.8f};
    spec.team={0.1f,0.2f,0.3f};
    spec.flags=render::kDrawTransparent;
    spec.sourceId=42;spec.generation=3;
    Object packet;
    assert(buildAppearancePacket(spec,render::mat4::identity(),packet));
    assert(packet.id==42 && packet.generation==3);
    assert(packet.draw.model==&model);
    assert(packet.draw.materials==&materials);
    assert(packet.draw.nodeMaterial==&nodeMaterial);
    assert(packet.draw.variant==7);
    assert(packet.draw.tint.w==0.8f);
    assert(packet.draw.team.x==0.1f);
    assert(packet.draw.flags&render::kDrawTransparent);
    assert(!packet.graph);
    assert(packet.visible);
    spec.model=nullptr;
    assert(!buildAppearancePacket(spec,render::mat4::identity(),packet));
}
