#pragma once
// Opt-in bridge from AIVERSE's existing CPU Model to Strata import assets.
// This deliberately does not create render buffers or submit draw commands.
#include "StrataBuilder.h"
#include "Model.h"

#include <cstdint>
#include <cstddef>
#include <map>
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

// Per-GeometryScene optional CPU staging. Keeps exactly one Strata asset per
// *source model*, independent of the number of scene instances. No GPU handles,
// draw submission, ECS state, renderer ownership or per-frame work.
struct StagingStats {
    uint32_t stagedSources = 0;
    uint32_t skippedSources = 0;
    uint32_t failedSources = 0;
    uint32_t stagedPrimitives = 0;
    uint32_t skippedPrimitives = 0;
    uint64_t clusterNodes = 0;
    uint64_t meshlets = 0;
    uint64_t indexBytes = 0; // auxiliary Strata flat index storage only
};

class SceneStaging {
public:
    // Conservative bounds cap staging work on unexpectedly massive models.
    // A skipped or failed source never prevents the existing renderer drawing it.
    struct Limits {
        size_t maxVerticesPerSource = 250000;
        size_t maxIndicesPerSource = 750000;
    };
    SceneStaging() = default;
    explicit SceneStaging(const BuildOptions& options) : options_(options) {}
    bool stage(const std::string& key, const Model& model);
    void clear();
    const ModelAsset* find(const std::string& key) const;
    const StagingStats& stats() const { return stats_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    void setLimits(Limits value) { limits_ = value; }
private:
    BuildOptions options_{};
    Limits limits_{};
    StagingStats stats_{};
    std::map<std::string, ModelAsset> assets_;
    std::vector<std::string> warnings_;
};

} // namespace aiverse::render::strata
