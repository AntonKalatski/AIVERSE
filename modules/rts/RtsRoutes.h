// Runtime state of authored routes, row groups and the multiscreen view (RtsRoutes.cpp; data in RtsRoutesData.h).
#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include "../../engine/core/ECS.h"
#include "RtsRoutesData.h"

namespace aiverse::game {

// A group being sent along a route: spawns a row of up to
// `lines` units whenever the row in front has walked rowSpacing; the garrison pays per row.
struct SendGroup {
    uint32_t id = 0;
    Entity from = kNullEntity, to = kNullEntity;
    uint8_t owner = 0;
    int need = 0, spawned = 0;
    Fixed rowDist{};
    bool flying = false;
    uint8_t payer = 0;                      // 0: owner; else the garrison's owner pays (hypnotised send captured by the caster)
};

// Per walker on an authored route: the lane position without the offset, the
// random diagonal offset, and the end fade the offset and the view use.
struct RouteWalk {
    Vec3 base{};
    Fixed jitter{};
    Fixed alpha = Fixed::fromInt(1);
};

// Query for units walking routes / lanes.
struct WalkerQuery {
    Vec3 pos{};
    Fixed range{};                           // x radius
    Fixed aspect = Fixed::fromInt(1);        // z radius = range x aspect (isometric maps: 0.5)
    uint8_t owner = 0;                       // walkers hostile to this player are returned
    Fixed minFromEnds{};                     // skip walkers closer than this to either route end
    Vec3 facing{};                           // with sectorCos > -1: only walkers inside the forward sector
    Fixed sectorCos = Fixed::fromInt(-1);    // cos of the half angle (30 deg sector: cos 15 deg = 0.9659); -1 = all around
};

struct RouteState {
    bool active = false;                    // map routes / screen or rules lanes.groups: then saved and hashed
    std::vector<Entity> ends;               // 2 per route: current from / to garrison (re-resolved at the route ends)
    std::vector<int32_t> laneRoute;         // per GameState::lanes entry: the route it was built from
    std::vector<Fixed> laneScale;           // per lane: its length / length of line 0 (rows stay abreast)
    std::vector<uint8_t> laneFlying;        // per lane: 1 = a flying route
    std::vector<uint8_t> routeOpen;         // per route: no blocker alive and both ends present
    std::map<uint64_t, RouteWalk> walks;    // entity (packed) -> walker on a route
    std::vector<SendGroup> groups;          // open groups in creation order
    uint32_t signature = 0;                 // ends / blockers the lanes were built for
    Vec3 screenCenter{};                    // current screen center
};

} // namespace aiverse::game
