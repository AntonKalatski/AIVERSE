// Traffic network from a spline road network (RtsTrafficRoads.cpp, docs/traffic.md § Spline roads).
#pragma once
#include <cstdint>
#include <vector>
#include "RtsRoadNet.h"
#include "../../libraries/include/aiverselib/genre/Traffic.h"

namespace aiverse::game {

// Curved pieces per drivable edge, junctions per node (arms on the four sides by direction), the cell maps, and the
// sidewalk mask (cells of sidewalks / shoulders) for the pedestrians. `decks` (the map's bridges and tunnels, centre
// lines in cells, docs/driving.md § Levels) join the roads that stop at their mouths: a deck whose two mouths each
// have a node of a drivable road within 6.5 cells (an open road end, a junction on the bank) becomes a piece between
// those nodes (the lanes of a road there), so traffic crosses it.
lib::genre::TrafficNet buildTrafficNetFromRoads(int w, int h, const RoadNetwork& n, const std::vector<RoadTypeDef>& types,
                                                const RoadGeometry& G, const lib::genre::TrafficNetConfig& cfg,
                                                std::vector<uint8_t>& sidewalk,
                                                const std::vector<std::vector<Vec2>>& decks = {});

} // namespace aiverse::game
