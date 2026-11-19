// Deferred native compile fixture: generic model source provider API.
// The real Metal/Vulkan/GL loading and GPU lifetime tests are separate.
#include "engine/render/ModelAssetProvider.h"
#include <cassert>
#include <string>
#include <type_traits>
#include <vector>

int main() {
    using namespace aiverse;
    using render::ModelAssetProvider;
    static_assert(std::is_constructible_v<ModelAssetProvider,render::FileReader>);
    static_assert(!std::is_copy_constructible_v<ModelAssetProvider>);
    static_assert(std::is_same_v<
        decltype(std::declval<ModelAssetProvider&>().resources()),
        render::ModelResourceCache&>);
    render::FileReader reader=[](const std::string&,std::string&) {
        return false;
    };
    ModelAssetProvider provider(reader);
    assert(provider.find("models/missing.glb")==nullptr);
    assert(provider.resources().generation()==0);
    assert(provider.resources().pendingRetirements()==0);
    assert(provider.warnings().empty());
    provider.clearWarnings();
    return 0;
}
