#pragma once
// Opt-in bridge from AIVERSE's existing CPU Model to Strata import assets.
// This deliberately does not create render buffers or submit draw commands.
#include "StrataBuilder.h"
#include "Model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::render::strata {

struct ModelPrimitiveAsset {
    uint32_t mesh = 0;
    uint32_t primitive = 0;
    uint32_t material = 0;
    Asset geometry; // owned CPU indices/meshlets/hierarchy
};

struct ModelPrimitiveSkip {
    uint32_t mesh = 0;
    uint32_t primitive = 0;
    std::string reason;
};
struct ModelAsset {
    std::vector<ModelPrimitiveAsset> primitives;
    std::vector<ModelPrimitiveSkip> skipped;
};

// Asset extraction is read-only and transactional. Preserves LOD0, node
// hierarchy, material IDs, geometry attributes and all Model indices.
// Excludes incompatible dynamic, translucent and skin/morph inputs.
// Does NOT automatically activate Strata in the production renderer.
bool buildModelAsset(const Model& model, const BuildOptions& options,
                     ModelAsset& out, std::string* error = nullptr);

} // namespace aiverse::render::strata
