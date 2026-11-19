// Stage 99: generic model source contract (native run deferred).
#include "engine/render/ModelResourceCache.h"
#include "modules/rts/view/RtsModelResourceCache.h"
#include "modules/rts/view/RtsModelPreparationService.h"
#include <cassert>
#include <memory>
#include <type_traits>
#include <vector>

using namespace aiverse;
int main() {
    static_assert(std::is_same_v<
        game::view_adapter::RtsModelResourceCache,
        render::ModelResourceCache>);
    static_assert(std::is_same_v<
        game::view_adapter::RtsModelPreparationService,
        render::ModelPreparationService>);
    render::ModelResourceCache owner;
    assert(owner.generation()==0);
    auto failed=owner.insertIfAbsent("models/missing.glb",nullptr);
    assert(failed==nullptr && owner.contains("models/missing.glb"));
    assert(owner.generation()==1);
    auto geometry=std::make_unique<render::Model>();
    geometry->path="models/hero.glb";
    render::Model* hero=owner.insertIfAbsent("models/hero.glb#geometry",
                                             std::move(geometry));
    assert(hero!=nullptr && owner.generation()==2);
    assert(owner.find("models/hero.glb#geometry")==hero);
    const auto affected=owner.affectedBySource("models/hero.glb");
    assert(affected.size()==1 && affected[0].model==hero);
    assert(owner.affectedBySource("models/missing.glb").size()==1);
    assert(render::ModelResourceCache::sourceForKey(
        "models/./hero.glb#geometry")=="models/hero.glb");
    const auto names=owner.modelFiles();
    assert(names.size()==2);
    assert(owner.pendingRetirements()==0);
}
