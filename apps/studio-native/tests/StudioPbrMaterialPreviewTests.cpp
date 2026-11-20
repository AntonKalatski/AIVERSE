#include "apps/studio-native/StudioPbrMaterialPreview.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace aiverse;
using namespace aiverse::studio;
int main() {
    auto material=studioPbrPreviewMaterial(Json::parse(R"({
        "color":[0.25,0.5,0.9,0.8],"metallic":0.95,"roughness":0.15,
        "emissive":[1,0.1,0.0],"emissiveStrength":2.5,"alpha":"blend",
        "doubleSided":true,"castShadow":false
    })"),"BlueSteel");
    assert(material.name=="BlueSteel");
    assert(material.metallic>.9f && material.roughness<.2f);
    assert(material.baseColor.x==.25f && material.baseColor.w==.8f);
    assert(material.emissiveStrength==2.5f);
    assert(material.alpha==render::AlphaMode::Blend && material.transparent);
    assert(material.doubleSided && !material.castShadow);
    material=studioPbrPreviewMaterial(Json::parse(R"({
      "metallic":500,"roughness":-2,"emissiveStrength":9999,"alpha":"mask","cutoff":0.34
    })"),"Clamped");
    assert(material.metallic==1 && material.roughness>=.04f);
    assert(material.emissiveStrength==100);
    assert(material.alpha==render::AlphaMode::Mask);
    assert(std::abs(material.alphaCutoff-.34f)<.001f);
    material=studioPbrPreviewMaterial(Json::object(),"Default");
    assert(material.shading==render::ShadingModel::Pbr);
    assert(material.vertexColors==false);
    assert(material.roughness>=0 && material.roughness<=1);
    std::cout<<"Studio PBR material adapter PASS\n";
    return 0;
}
