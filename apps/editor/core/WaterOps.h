// Water in the editor (docs/water.md § Editor): the map's "waterNetwork" lives in MapDoc `extra`. The operations
// water_types, water_network, water_set, water_delete, water_shores and water_bake edit it; every change bakes the
// water into the map's cells ('~' under the water, '#' under cliffs, the tiles layer's chars if "tiles" names them)
// and recomputes the "auto" shore segments. The window «Вода» calls the same operations.
#pragma once
#include <string>
#include "Workspace.h"
#include "../../../modules/rts/view/RtsWater.h"

namespace aiverse::editor {

void registerWaterOps(Workspace& ws);

namespace waterops {

game::water::ShoreTypes types(const Workspace& ws);   // rules "shoreTypes" over the built-in library
// Bakes `n` into the map: restores what the previous bake changed, writes water / cliff cells, auto shores,
// stores the network with its new "baked" record. Returns a summary (cells, segments per body, land parts).
Json bake(Workspace& ws, game::water::Network n);

} // namespace waterops
} // namespace aiverse::editor
