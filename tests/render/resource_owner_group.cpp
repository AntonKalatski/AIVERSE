// Stage 105 deferred CPU-only regression: independent owners aggregated
// without counting duplicate host registrations or borrowed images as VRAM.
// Do not construct a GPU Device or pretend estimates equal driver heap bytes.
#include "engine/render/RenderResourceOwnerGroup.h"
#include <cassert>
#include <cstdint>
#include <memory>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse::render;
    ModelResourceCache scene,portrait,raw;
    auto model=[](uint32_t id) {
        auto out=std::make_unique<Model>();
        out->vb={id};
        out->vertices.resize(8);
        out->ib={id+20};
        out->indices.resize(12);
        // Model A and B may refer to the same externally owned texture
        // handle; neither owns it, so neither is billed for that image.
        Image borrowed;
        borrowed.tex={100};
        borrowed.width=128;
        borrowed.height=128;
        borrowed.borrowedTexture=true;
        out->images.push_back(std::move(borrowed));
        return out;
    };
    scene.insertIfAbsent("scene.glb",model(1));
    portrait.insertIfAbsent("portrait.glb",model(2));
    raw.insertIfAbsent("raw.glb",model(3));
    RenderResourceOwnerGroup group;
    group.add(scene);
    group.add(portrait);
    group.add(raw);
    group.add(scene); // idempotent registration
    assert(group.size()==3);
    const auto healthy=group.snapshotOwners();
    assert(healthy.uniqueOwners==3);
    assert(healthy.residentModels==3);
    assert(healthy.retiredModels==0);
    assert(healthy.uniqueTextureOwners==3);
    assert(healthy.uniqueTextures==0); // borrowed handles not cache-owned
    assert(healthy.geometryPages.uniquePages==0);
    assert(healthy.health==RenderResourceHealth::Ready);
    const auto bytes=uint64_t(3)*(8*sizeof(ModelVertex)+12*sizeof(uint32_t));
    assert(healthy.residentModelKnownBytes==bytes);
    assert(healthy.estimatedKnownBytes()==bytes);
    group.setEstimatedBudgetBytes(bytes);
    assert(!group.mayAdmitNewSources(healthy));
    group.setEstimatedBudgetBytes(bytes+1);
    assert(group.mayAdmitNewSources(healthy));
    const auto unhealthy=group.snapshotOwners(false);
    assert(unhealthy.health==RenderResourceHealth::DeviceUnhealthy);
    assert(!group.mayAdmitNewSources(unhealthy));
    group.clear();
    assert(group.size()==0);
    assert(group.snapshotOwners().estimatedKnownBytes()==0);
}
