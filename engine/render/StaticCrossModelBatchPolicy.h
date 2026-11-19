#pragma once
// GPU-page compatibility for opt-in static CPU MDI batching.
//
// Each model keeps its own primitive, transform, culling, LOD selection and
// source material. Only *after* view selection can compatible rigid primitives
// from different models sharing a GPU geometry arena join one MDI submission.
// Material/pipeline equivalence is checked separately by Renderer3D.
#include "Model.h"

namespace aiverse::render {

inline bool staticCrossModelPageCompatible(const Model& a, const Model& b) {
    return &a != &b &&
        a.geometryPoolPage && b.geometryPoolPage &&
        a.geometryPoolPage == b.geometryPoolPage &&
        a.vb.valid() && a.ib.valid() &&
        a.vb == b.vb && a.ib == b.ib &&
        !a.geometry && !b.geometry;
}

} // namespace aiverse::render
