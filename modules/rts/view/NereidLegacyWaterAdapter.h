// Read-only legacy editor waterNetwork -> NEREID import bridge.
// This adapter is allowed to depend on RTS/editor-time RtsWater; the public
// NEREID component and sampling library are genre-independent.
// It does NOT edit, rebake or replace map cells, shores or waterNetwork.
#pragma once
#include "RtsWater.h"
#include <aiverselib/water/NereidRasterImport.h>

namespace aiverse::game::water {

// Use EXACTLY the same legacy rasterizer as the editor's water bake.
// The caller provides NEREID's Fixed cellMeters explicitly; it must match
// legacyCellMeters semantically (e.g. 4_fx with legacy 4.0f). Passing the
// already-baked cell raster to waterBodiesFromRaster is preferred if maps
// may have been altered by bridges or other post-bake tools.
inline std::vector<lib::nereid::WaterBodyComponent> importWaterNetworkToNereid(
        const Network& network,int width,int height,float legacyCellMeters,
        const lib::nereid::WaterRasterImportConfig& config) {
    if(!(legacyCellMeters>0.0f))
        throw std::invalid_argument("legacy water cellMeters must be positive");
    const Geometry geometry=buildGeometry(network,width,height,legacyCellMeters);
    const auto wet=waterCells(network,geometry,width,height,legacyCellMeters);
    return lib::nereid::waterBodiesFromRaster(wet,width,height,config);
}

} // namespace aiverse::game::water
