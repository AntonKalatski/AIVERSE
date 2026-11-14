// Authored routes between garrison points:
// map "routes" with control points, per-point column offsets and optional raw per-line nodes, the sub-spline
// builder (Catmull-Rom with nodes between control points, column offsets, end trimming), the rules keys
// (lanes.routes / lanes.groups) and the map "screen" block (MultiscreenModule). GPU-free and module-free so the
// editor and tools share the exact geometry the simulation walks (docs/game-format.md § Routes).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"

namespace aiverse::game {

// rules.json "lanes": {"routes": {...}, "groups": {...}}. Lengths in map cells; a game sets its own values.
struct RouteRules {
    int lines = 6;                                    // parallel sub-splines per route
    Fixed columnPadding = Fixed::fromRatio(1, 2);     // offset step between columns
    Fixed flyColumnPadding = Fixed::fromRatio(9, 8); // flying routes
    Fixed trim = Fixed::fromRatio(1, 4);              // cut from both ends of a sub-spline
    int smooth = 10;                                  // spline nodes between control points
    Fixed inOut = Fixed::fromRatio(5, 2);                  // flying routes: straight in / out part
    Fixed jitter = Fixed::fromRatio(1, 8);            // random diagonal offset per unit
    Fixed fade = Fixed::fromRatio(1, 2);              // alpha fade length at both ends
    Fixed coverageStep = Fixed::fromRatio(5, 4);      // tower coverage: range step of the table
    // row groups: rows of `lines` units, the garrison pays per row, sends merge, owner change closes
    bool groups = false;
    Fixed rowSpacing = Fixed::fromRatio(7, 4);        // distance walked between rows
    Fixed flyRowSpacing = Fixed::fromInt(3);
    bool flyAll = false;                              // a flying send takes the whole garrison
    bool flySingle = false;                           // one flying group per garrison at a time (the next send walks)    // flying groups
};
// Parses the optional "routes" / "groups" members of the rules "lanes" object (throws readable errors).
void loadRouteRules(const Json& lanes, RouteRules& out);

// One authored route (map "routes" entry). Positions are map cells (x, z).
struct RouteDef {
    std::string from, to;                             // entity ids (map "entities"[i].id) or "#<index>"
    std::vector<Vec2> points;                         // control points; first / last = the points
    std::vector<std::array<Fixed, 8>> offsets;        // per control point: extra offset per line
    std::vector<std::vector<Vec2>> lines;             // optional raw nodes per line
    std::vector<uint32_t> zones;                      // per control point: zone flags (game-defined bits; 2 = slowdown)
    std::vector<Fixed> slowdown;                      // per control point: slowdown coefficient (AI length only)
    std::vector<std::string> blockers;                // entity ids that close the route while alive
    // precomputed tower coverage: per tower point, covered route length for tower
    // ranges 0, step, 2 step ... (at most 21 values; the last one holds for longer ranges)
    std::vector<std::pair<std::string, std::vector<Fixed>>> coverage;
    bool flying = false;                              // flying route: fly column padding
};
// Parses one map "routes" entry; throws std::runtime_error("map route N: ...") on bad data.
RouteDef parseRoute(const Json& j, int index);
Json routeToJson(const RouteDef& r);

// The sub-splines (lines) of a route line k
// follows its raw nodes when it has at least two, else the control points with the column offset of k applied to
// the inner points; each is a Catmull-Rom node list, trimmed at both ends when long enough.
std::vector<std::vector<Vec2>> buildRouteLines(const RouteDef& r, const RouteRules& rr);
Fixed polylineLength(const std::vector<Vec2>& pts);
// AI length: control-point distances, slowdown segments stretched.
Fixed routeAiLength(const RouteDef& r);
// Lane offset label of line k: even lines to the left (-1, -2, ...), odd to the right (+1, +2, ...), so sorting by
// |offset| keeps the line order (RtsModule::lanesBetween).
inline int routeLineOffset(int k) { return (k % 2 == 0) ? -(k / 2 + 1) : (k / 2 + 1); }
// Straight flying route between two points.
RouteDef flyingRoute(Vec2 from, Vec2 to, const RouteRules& rr);

// Map "screen": the playable view is a screen-sized window over a larger map; points outside
// it do not produce. left / right / top / bottom = how far the center may move from its start
//.
struct ScreenDef {
    bool enabled = false;
    Vec2 size{};                                      // screen size in cells
    Vec2 center{};                                    // start center
    Fixed left{}, right{}, top{}, bottom{};           // movement bounds of the center
    Fixed smooth = Fixed::fromInt(10);                // view: follow rate per second
};
ScreenDef parseScreen(const Json& j, int mapW, int mapH);
// Screen groups of two points: both empty -> true; one empty -> false; else any shared group.
bool screenGroupsMatch(const std::vector<int>& a, const std::vector<int>& b);

} // namespace aiverse::game
