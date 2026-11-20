#pragma once
#include "HumanAppearance.h"
namespace aiverse::vivid {
// Content-relative asset keys are resolved exclusively by the host FileReader.
// Recipe v1: hiddenMeshes:string[], items:[{id,asset,slot,enabled,bone,translation,
// rotation,scale,hideMeshes:string[],masks:[{material,texture}]}].
// Masks: red=visibility (black hides), UV0, multiplied with original alpha.
void bakeHumanBody(render::Model& model,const HumanAppearance& look);
void validateHumanWardrobe(const Json& recipe);
Json inspectHumanWardrobe(const render::Model& model);
std::unique_ptr<render::Model> assembleHumanCharacter(const std::string& base,
    const render::FileReader& read,const Json& wardrobe,const HumanAppearance& look,
    int textureSize=1024);
}
