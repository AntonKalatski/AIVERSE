#pragma once
// STRATA cooked assets: the complete ModelAsset (cluster hierarchy, bounds,
// indices, meshlets) built once at import/cook time and loaded at run time
// instead of running buildModelAsset() on every load. Like Nanite's cooked
// data: the expensive simplification never runs on the player's device.
//
// A cooked file is bound to the exact prepared source geometry by a
// signature over the model's vertices, indices, primitive layout, the build
// options and the staging flags. A stale or foreign file is rejected and the
// caller builds as before, so cooking can only make loads faster, never wrong.
// Does NOT own GPU resources, Renderer3D or scene state.
#include "StrataModelAdapter.h"
#include <cstdint>
#include <string>

namespace aiverse::render::strata {

// Signature of the source exactly as SceneStaging would build it.
uint64_t cookSignature(const Model& model, const BuildOptions& options,
                       bool allowPreparedRootLods, bool tolerateSourceSeamDefects);

// Cooked file name for a staging key (hashed, never a path fragment).
std::string cookedFilename(const std::string& sourceKey);

// Serialize to bytes / parse from bytes (bytes come from any FileReader,
// so packaged content inside an APK or .rpak works the same as a folder).
std::string encodeCookedAsset(const ModelAsset& asset, uint64_t signature);
bool decodeCookedAsset(const std::string& bytes, uint64_t signature,
                       ModelAsset& out, std::string* error = nullptr);

// Atomic write (temp file + rename).
bool writeCookedAsset(const std::string& path, const ModelAsset& asset,
                      uint64_t signature, std::string* error = nullptr);

} // namespace aiverse::render::strata
