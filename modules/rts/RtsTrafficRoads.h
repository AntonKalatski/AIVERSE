// Traffic network from a spline road network (RtsTrafficRoads.cpp, docs/traffic.md § Spline roads).
#pragma once
#include <cstdint>
#include <vector>
#include "RtsRoadNet.h"
#include "../../libraries/include/aiverselib/genre/Traffic.h"

namespace aiverse::game {

// Curved pieces per drivable edge, junctions per node (arms on the four sides by direction), the cell maps, and the
// sidewalk mask (cells of sidewalks / shoulders) for the pedestrians.
lib::genre::TrafficNet buildTrafficNetFromRoads(int w, int h, const RoadNetwork& n, const std::vector<RoadTypeDef>& types,
                                                const RoadGeometry& G, const lib::genre::TrafficNetConfig& cfg,
                                                std::vector<uint8_t>& sidewalk);

} // namespace aiverse::game
