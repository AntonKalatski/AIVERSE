// Genre-independent GI binding contract for custom PBR shaders (terrain,
// foliage, water, user shader pipelines). The caller owns shader compilation
// and declares U6/tex13 and optional U7/tex14/15, Metal U8/tex16 exactly as
// defined in GiDynamicShaders.h + GiCascadeShaders.h.
#pragma once
#include "GiTextureBudget.h"
#include <cstdint>

namespace aiverse::render {

enum class GiCustomShaderMode : uint8_t {
    Off = 0, Single = 1, Validity = 2, DualCascade = 3, TripleCascade = 4
};
constexpr bool giCustomModeUsesValidity(GiCustomShaderMode mode) {
    return mode>=GiCustomShaderMode::Validity;
}
constexpr bool giCustomModeUsesFar(GiCustomShaderMode mode) {
    return mode>=GiCustomShaderMode::DualCascade;
}
constexpr bool giCustomModeUsesThird(GiCustomShaderMode mode) {
    return mode>=GiCustomShaderMode::TripleCascade;
}
constexpr int giCustomModeTextureSlots(GiCustomShaderMode mode) {
    return mode==GiCustomShaderMode::Off ? 0 :
           giFragmentTextureSlotsRequired(giCustomModeUsesValidity(mode),
                                          giCustomModeUsesFar(mode),
                                          giCustomModeUsesThird(mode));
}
constexpr bool giCustomModeFits(GiCustomShaderMode mode,int fragmentSlots,
                                bool metal) {
    if(mode==GiCustomShaderMode::Off)return false;
    if(mode==GiCustomShaderMode::TripleCascade && !metal)return false;
    const int required=giCustomModeTextureSlots(mode);
    return required>0 && required<=fragmentSlots;
}
} // namespace aiverse::render
