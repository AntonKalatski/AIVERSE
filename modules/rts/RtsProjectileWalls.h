#pragma once
// Walls of unit types (type "walls": [x, z, w, d] boxes, cells) that stop projectiles of weapons with "hitsWalls".
#include "modules/rts/RtsModule.h"
#include <optional>
namespace aiverse::game {
struct WallContact { std::optional<Fixed> distance; Entity entity; };
inline WallContact projectileWall(World& world, const Rules& rules, Vec3 from, Vec3 direction, Fixed length, Fixed radius) {
    WallContact best;
    world.each<Unit, Transform>([&](Entity e, const Unit& u, const Transform& t) {
        if (u.hp.raw <= 0 || u.type >= rules.types.size() || rules.types[u.type].walls.empty()) return;
        if (fxAbs(t.pos.x - from.x) > length + Fixed::fromInt(20) || fxAbs(t.pos.z - from.z) > length + Fixed::fromInt(20)) return;
        for (const auto& box : rules.types[u.type].walls) {
            Fixed enter{}, leave = length;
            auto axis = [&](Fixed p, Fixed d, Fixed center, Fixed width) {
                const Fixed half = width / 2 + radius;
                if (!d.raw) return p >= center - half && p <= center + half;
                Fixed a = (center - half - p) / d, b = (center + half - p) / d;
                if (a > b) std::swap(a, b);
                enter = fxMax(enter, a); leave = fxMin(leave, b); return enter <= leave;
            };
            if (axis(from.x, direction.x, t.pos.x + box[0], box[2]) && axis(from.z, direction.z, t.pos.z + box[1], box[3]) &&
                (!best.distance || enter < *best.distance || (enter == *best.distance && e.index < best.entity.index)))
                best = {enter, e};
        }
    });
    return best;
}
}
