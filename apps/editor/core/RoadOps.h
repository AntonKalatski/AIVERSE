// Spline roads in the editor (docs/roads.md § Editor): the map's "roadNetwork" lives in MapDoc `extra`. The edit
// helpers here are shared by the Scene tool («Дороги-сплайны», EditorRoadTool.cpp) and the MCP operations
// road_types, road_type_set, road_network, road_add, road_node_set, road_edge_set, road_split, road_delete.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "Workspace.h"
#include "../../../modules/rts/RtsRoadNet.h"

namespace aiverse::editor {

void registerRoadOps(Workspace& ws);

namespace roads {

using game::RoadNetwork;
using game::RoadTypeDef;

std::vector<RoadTypeDef> types(const Workspace& ws);            // rules "roadTypes" over the built-in library
RoadNetwork read(const MapDoc& doc, const std::vector<RoadTypeDef>& types);   // throws on bad data
void write(MapDoc& doc, const RoadNetwork& n);                   // no beginEdit / touched: the caller does that

// What a point snaps to: an existing node, a point on an edge (split there), or nothing (a new node).
struct Snap {
    enum Kind { None, Node, Edge } kind = None;
    int32_t node = 0;                 // Node
    int edge = -1;                    // Edge: edge index; t = Bezier parameter, s = distance along it
    Fixed t{}, s{};
    Fixed lateral{};                  // signed offset from the centre line (lane picking)
    Vec2 p{};                         // the snapped point
};
Snap snap(const RoadNetwork& n, const game::RoadGeometry& g, Vec2 p, Fixed radius);
// lane of an edge under a lateral offset: direction (+1 forward / -1 backward, in the edge's own a -> b sense) and
// index (centre outwards); false when the offset is not on a lane
bool laneAt(const RoadTypeDef& t, const game::RoadEdge& e, Fixed lateral, int& dir, int& lane);

// Splits edge `ei` at Bezier parameter t (de Casteljau: the shape is kept); returns the new node id. Branches attached
// to the old edge are re-attached to the half that ends at their node.
int32_t splitEdge(RoadNetwork& n, int ei, Fixed t);
int32_t nodeFor(RoadNetwork& n, const Snap& s, Fixed y);         // existing / split / new node

struct AddSpec {
    std::string type;
    Snap a, b;
    Fixed ya{}, yb{};                 // heights of new end nodes (metres over the ground)
    bool curved = false;
    Vec2 c1{}, c2{};
    Fixed y1{}, y2{};
    bool heights = false;             // y1 / y2 given (else the straight slope)
    game::RoadAttach attachA, attachB;   // edge = index of the parent edge in `n` before the split (-1: none)
    bool reversed = false;
};
// Adds one edge (and its end nodes). Returns the new edge id; throws std::runtime_error with a readable message.
int32_t addEdge(RoadNetwork& n, const std::vector<RoadTypeDef>& types, const AddSpec& spec);
// Dirt and gravel roads do not ford rivers: where edge `id` (an unpaved one) crosses water its stretch over the
// water and `approachMetres` either side becomes an asphalt bridge ("rural_bridge", raised over the water), so the
// road runs dirt -> asphalt -> bridge -> asphalt -> dirt. Returns how many bridges were made.
int bridgeWater(RoadNetwork& n, const std::vector<RoadTypeDef>& types, int32_t id, const std::function<bool(Vec2)>& water,
                Fixed cellMetres, Fixed approachMetres = Fixed::fromInt(16));
// the map's water cells ('~') as a predicate over map cell coordinates
std::function<bool(Vec2)> waterOf(const MapDoc& doc);
void deleteEdge(RoadNetwork& n, int32_t id);                     // and nodes left without edges
void deleteNode(RoadNetwork& n, int32_t id);                     // with its edges
void moveNode(RoadNetwork& n, int32_t id, Vec2 p, Fixed y);      // the neighbouring control points follow
// Bezier parameter of the edge point nearest to p
Fixed nearestT(const RoadNetwork& n, int ei, Vec2 p);
// quadratic through a control point -> cubic control points
void quadraticToCubic(Vec2 a, Vec2 q, Vec2 b, Vec2& c1, Vec2& c2);

} // namespace roads
} // namespace aiverse::editor
