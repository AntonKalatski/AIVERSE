// PHYSIS chunk streaming for large worlds (2026-11-22).
//
// The grid is split into square chunks (chunkCells x chunkCells). Any entity
// with an ActivityAnchor (a player, a camera rig, a vehicle, a scripted
// region — the genre does not matter) keeps the chunks within its radius
// simulated; all other chunks are DORMANT: their weather, surface, fire and
// local flood are frozen until an anchor comes near again. Dormant cells act
// as walls for flood transfer (no water is lost or created at the border) and
// never ignite, but active cells still read their frozen values.
//
// Determinism: anchors and the mask are authoritative ECS data, saved and
// hashed; the mask is recomputed by the "physis.streaming" system at the
// start of every tick from anchors in ECS order, so all peers agree.
// With no anchor at all every chunk is active (old behaviour), so enabling
// streaming never freezes a world by accident.
//
// Cost: climate/surface/disaster work is done only for active cells; light
// O(cells) bookkeeping scans remain. Chunk data stays resident in memory: this
// is simulation streaming (what is updated), not paging cells to disk.
//
// Map: "physis": { "streaming": { "chunkCells": 32 },
//                  "anchors": [ { "x": 0, "z": 0, "radiusMeters": 2048 } ] }
#pragma once
#include "Physis.h"
#include "PhysisChunkMask.h"
#include "../core/Archive.h"
#include <algorithm>

namespace aiverse::physis {

struct ActivityAnchor {
    Vec3 position{};                          // Fixed world metres (x/z used)
    Fixed radiusMeters = Fixed::fromInt(1024);
};

inline void io(Archive& a, ActiveChunks& v) {
    a(v.chunkCells, v.chunksX, v.chunksZ, v.all, v.revision, v.active);
}
inline void io(Archive& a, ActivityAnchor& v) { a(v.position, v.radiusMeters); }

// Creates the mask (all active). Call once after initialize().
inline void enableStreaming(World& world, std::int32_t chunkCells = 32) {
    const auto* state = world.resourceConst<State>();
    if (!state) throw std::logic_error("PHYSIS not initialized");
    if (chunkCells < 4 || chunkCells > 1024)
        throw std::invalid_argument("PHYSIS streaming chunkCells must be 4..1024");
    ActiveChunks mask;
    mask.chunkCells = chunkCells;
    mask.chunksX = (state->settings.width + chunkCells - 1) / chunkCells;
    mask.chunksZ = (state->settings.height + chunkCells - 1) / chunkCells;
    mask.active.assign(std::size_t(mask.chunksX) * std::size_t(mask.chunksZ), 1);
    world.setResource<ActiveChunks>(mask);
}

// Recompute the mask from all ActivityAnchor components. Deterministic: pure
// Fixed/integer math, chunk rectangles test circle-vs-box on the X/Z plane.
inline void updateActiveChunks(World& world) {
    const auto* state = world.resourceConst<State>();
    const auto* current = world.resourceConst<ActiveChunks>();
    if (!state || !current) return;
    ActiveChunks next = *current;
    std::fill(next.active.begin(), next.active.end(), std::uint8_t(0));
    bool anyAnchor = false;
    const auto& s = state->settings;
    const std::int64_t chunkMeters = std::int64_t(next.chunkCells) * s.cellSizeMeters;
    world.eachExistingConst<ActivityAnchor>([&](Entity, const ActivityAnchor& a) {
        anyAnchor = true;
        if (a.radiusMeters <= Fixed{}) return;
        const std::int64_t r = a.radiusMeters.floorInt() + 1;   // round up
        const std::int64_t px = a.position.x.floorInt(), pz = a.position.z.floorInt();
        const std::int64_t gx0 = std::int64_t(s.originX) * s.cellSizeMeters;
        const std::int64_t gz0 = std::int64_t(s.originZ) * s.cellSizeMeters;
        const auto first = [&](std::int64_t v, std::int64_t origin) {
            return floorDiv(v - origin, chunkMeters);
        };
        const std::int64_t cx0 = std::max<std::int64_t>(0, first(px - r, gx0));
        const std::int64_t cx1 = std::min<std::int64_t>(next.chunksX - 1, first(px + r, gx0));
        const std::int64_t cz0 = std::max<std::int64_t>(0, first(pz - r, gz0));
        const std::int64_t cz1 = std::min<std::int64_t>(next.chunksZ - 1, first(pz + r, gz0));
        for (std::int64_t cz = cz0; cz <= cz1; ++cz)
            for (std::int64_t cx = cx0; cx <= cx1; ++cx) {
                const std::int64_t minX = gx0 + cx * chunkMeters, minZ = gz0 + cz * chunkMeters;
                const std::int64_t nx = std::clamp(px, minX, minX + chunkMeters);
                const std::int64_t nz = std::clamp(pz, minZ, minZ + chunkMeters);
                const std::int64_t dx = px - nx, dz = pz - nz;
                // radius <= 2^31 m keeps r*r in range; positions are grid-bounded.
                if (dx * dx + dz * dz <= r * r)
                    next.active[std::size_t(cz) * std::size_t(next.chunksX) + std::size_t(cx)] = 1;
            }
    });
    next.all = !anyAnchor;
    if (next.all) std::fill(next.active.begin(), next.active.end(), std::uint8_t(1));
    if (next.all != current->all || next.active != current->active) {
        ++next.revision;
        world.setResource<ActiveChunks>(next);
    }
}

inline std::size_t activeCellCount(const World& world) {
    const auto* state = world.resourceConst<State>();
    if (!state) return 0;
    const auto* mask = world.resourceConst<ActiveChunks>();
    std::size_t n = 0;
    for (std::size_t i = 0; i < state->cells.size(); ++i)
        if (cellSimulated(mask, state->settings.width, i)) ++n;
    return n;
}

inline Entity addActivityAnchor(World& world, Vec3 position, Fixed radiusMeters) {
    const Entity e = world.create();
    world.add<ActivityAnchor>(e, ActivityAnchor{position, radiusMeters});
    return e;
}

} // namespace aiverse::physis
