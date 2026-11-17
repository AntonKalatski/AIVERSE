// Shared GPU geometry pages. Rigid models use the same vertex/index buffer
// without baking node transforms, allowing indexed Multi-Draw across assets.
#pragma once
#include "Model.h"

namespace aiverse::render {
struct GeometryPoolStats {
    size_t pages = 0;
    size_t allocatedBytes = 0;
    size_t occupiedBytes = 0;
    size_t liveModels = 0;
};
GeometryPoolStats geometryPoolStats(gfx::Device& dev);
bool globalGeometryPoolingEnabled();
void setGlobalGeometryPoolingEnabled(bool enabled);
bool uploadModelToGeometryPool(gfx::Device& dev, Model& m);
void releaseModelGeometryPool(gfx::Device& dev, Model& m);
}
