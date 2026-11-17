// Shared GPU geometry pages. Rigid models use the same vertex/index buffer
// without baking node transforms, allowing indexed Multi-Draw across assets.
#pragma once
#include "Model.h"

namespace aiverse::render {
bool globalGeometryPoolingEnabled();
void setGlobalGeometryPoolingEnabled(bool enabled);
bool uploadModelToGeometryPool(gfx::Device& dev, Model& m);
void releaseModelGeometryPool(gfx::Device& dev, Model& m);
}
