// Shared GPU geometry pages. Rigid models use the same vertex/index buffer
// without baking node transforms, allowing indexed Multi-Draw across assets.
#pragma once
#include "Model.h"
#include <span>

namespace aiverse::render {
struct GeometryPoolStats {
    size_t pages = 0;
    size_t allocatedBytes = 0;
    size_t occupiedBytes = 0;
    size_t liveModels = 0;
};
GeometryPoolStats geometryPoolStats(gfx::Device& dev);
// The device's shared pages are counted ONCE by page identity across the
// entire supplied model set, which may combine independent host caches.
// Do not add these values per-host: the same page can span many owners.
// Only pages registered for this device are counted. This is allocator
// capacity/occupied accounting, NOT exact driver VRAM usage.
struct GeometryPoolReferencedStats {
    size_t uniquePages=0,referenceModels=0;
    size_t allocatedBytes=0,occupiedBytes=0;
};
GeometryPoolReferencedStats geometryPoolReferencedStats(
    gfx::Device& dev,std::span<const Model* const> models);
bool globalGeometryPoolingEnabled();
void setGlobalGeometryPoolingEnabled(bool enabled);
bool uploadModelToGeometryPool(gfx::Device& dev, Model& m);
void releaseModelGeometryPool(gfx::Device& dev, Model& m);
}
