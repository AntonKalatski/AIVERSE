// Road kit of the view: procedural tiling textures (asphalt, concrete, cobbles, paving, gravel, dirt, grass) and the
// built-in prop shapes (lamp, tree, bush, flowerbed, bollard, pole, bench, sign) as triangles per kit material.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../../engine/render/Math3D.h"
#include "../../../engine/render/Model.h"

namespace aiverse::game::roadkit {

// RGBA8 tile of `size` x `size` (seamless) for a kit material (RtsRoadView::Mat); empty: plain colour material
render::Image texture(int mat, int size = 256);
// base colour (linear) and roughness / metallic of a kit material
render::Material material(int mat);

struct Tri { int mat; render::float3 a, b, c; };

// Road paint decals: one atlas of white marks (tinted per quad) in a 4 x 4 grid. Arrows and text point to v = 0.
enum Mark : int { kMarkLine = 0, kMarkArrowS, kMarkArrowL, kMarkArrowR, kMarkArrowSL, kMarkArrowSR, kMarkStop, kMarkBus, kMarkArrowLR, kMarkCount };
std::vector<uint32_t> markingAtlas(int cell);          // (4 * cell)^2 RGBA8 (r in the low byte), white + alpha
inline render::float4 markUv(int mark) {                // u0, v0, u1, v1 of a mark (with a small inset)
    const float c = 0.25f, x = float(mark % 4) * c, y = float(mark / 4) * c, in = c * 0.02f;
    return {x + in, y + in, x + c - in, y + c - in};
}
// a built-in prop shape at the origin, up +y, facing +x (the road's forward), `s` world units per metre
void shape(const std::string& id, float s, std::vector<Tri>& out);

} // namespace aiverse::game::roadkit
