// Human-specific material graphs for the existing AIVERSE PBR renderer.
// All operations compile to paired GLSL/MSL via ShaderGraph: no renderer fork.
#pragma once
#include "HumanDNA.h"
#include "engine/render/Model.h"
#include "engine/render/graph/ShaderGraph.h"

namespace aiverse::vivid {
enum class HumanSurface : std::uint8_t {
    Skin, Iris, Sclera, Cornea, Hair, Eyelash, Teeth
};
struct HumanSurfaceMaterials {
    render::Material skin,iris,sclera,cornea,hair,eyelash,teeth;
};
render::Material humanMaterial(HumanSurface surface,const HumanDNA& dna);
// Preserve imported scan albedo/normal/roughness/AO/emissive texture slots
// when upgrading a glTF/FBX material to VIVID human-specific shading.
render::Material humanMaterialFromSource(HumanSurface surface,const HumanDNA& dna,
                                         const render::Material& source);
HumanSurfaceMaterials humanMaterials(const HumanDNA& dna);
}
