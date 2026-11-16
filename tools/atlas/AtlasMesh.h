// Convex meshes of atlas images (host tools only).
//
// buildMesh() returns a convex polygon (x0 y0 x1 y1 ..., content pixels, on the 1/kMeshUnits grid)
// that covers every pixel with alpha != 0: the convex hull of those pixels' squares, simplified to
// about `maxVertices` corners by cutting the cheapest edges (each cut adds the least area outside
// the hull), clipped to the image, then grown by a 1/kMeshUnits safety margin and checked
// exactly. Drawing the polygon instead of the rectangle saves fill rate on mostly transparent
// sprites and, in tight groups, frees the rest of the rectangle for other images.
// Empty when the polygon would not save at least `minSaving` of the rectangle (draw the rectangle).
#pragma once
#include <vector>

#include "engine/atlas/AtlasFormat.h"

namespace aiverse::atlas {

std::vector<float> buildMesh(const Image& img, int maxVertices = 8, float minSaving = 0.12f);

double polygonArea(const std::vector<float>& poly);   // absolute, in pixels^2

} // namespace aiverse::atlas
