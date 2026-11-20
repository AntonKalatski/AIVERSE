// Stage 106 deferred: expired resource owner registrations never dereference
// stale cache pointers, including when the group outlives a scoped owner.
#include "engine/render/RenderResourceOwnerGroup.h"
#include <cassert>
#include <cstdint>
#include <memory>
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse::render;
    RenderResourceOwnerGroup group;
    {
        auto transient=std::make_unique<ModelResourceCache>();
        transient->insertIfAbsent("model.glb",std::make_unique<Model>());
        group.add(*transient);
        group.add(*transient);
        assert(group.size()==1);
        const auto live=group.snapshotOwners();
        assert(live.uniqueOwners==1 && live.residentModels==1);
        assert(live.expiredRegistrations==0);
    }
    // The weak marker expired during ModelResourceCache destruction.
    // The pointer address is stale but MUST NOT be dereferenced.
    assert(group.size()==0);
    const auto expired=group.snapshotOwners();
    assert(expired.uniqueOwners==0 && expired.residentModels==0);
    assert(expired.expiredRegistrations==1);
    {
        ModelResourceCache replacement;
        replacement.insertIfAbsent("next.glb",std::make_unique<Model>());
        group.add(replacement);
        assert(group.size()==1);
        assert(group.snapshotOwners().residentModels==1);
        assert(group.snapshotOwners().expiredRegistrations==1);
    }
    assert(group.size()==0);
    assert(group.snapshotOwners().expiredRegistrations==2);
    group.clear();
    assert(group.snapshotOwners().expiredRegistrations==0);
}
