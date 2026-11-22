// PHYSIS tornado (2026-11-22): a moving vortex as a deterministic ECS phenomenon.
//
// A tornado is an ordinary StormEvent (kind Tornado, PhysisStorm.h: tangential
// + inflow + updraft wind that every PHYSIS wind reader already sees through
// windWithStormsAt) plus a TornadoTrack on the same entity that adds:
//   * a path: authored drift, deterministic wander (own RNG state) or waypoints;
//   * a life: start / duration of the StormEvent and a spin-up / decay ramp that
//     scales the vortex wind (TornadoTrack::intensity, 0..1);
//   * effects on the world, once per committed PHYSIS tick:
//       - fire (PhysisDisasters HazardCell): blown out in the core, fanned in
//         the ring (only where fuel remains);
//       - debris: light pieces torn up in the core, carried by the same wind
//         (quadratic drag + PHYSIS gravity), landing and expiring
//         (TornadoDebrisField resource — no entities, so other modules'
//         entity allocation is not disturbed);
//       - opt-in host objects (Transform + NatureCarriable): lifted when the
//         wind exceeds their lift speed, carried, dropped;
//       - damage to host entities with a Transform through a host callback
//         (Simulation: GameModule::environmentDamage) — the module applies its
//         own health / armour rules, PHYSIS keeps no health copy;
//       - vegetation: trees/structures with NatureStress receivers already load
//         from windWithStormsAt (forces -> nature stress -> fracture outbox).
// Fixed-point only, stable entity-index order, explicit RNG: lockstep / replay
// safe. No renderer, audio, Box3D, NEREID, TerraWeave or genre dependency.
#pragma once
#include "PhysisStorm.h"
#include "engine/core/Archive.h"
#include "engine/core/FixedMath.h"
#include "engine/sim/CommonComponents.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace aiverse::physis {

enum class TornadoPathMode : std::uint8_t { Drift = 0, Wander = 1, Waypoints = 2 };

// Lives on the StormEvent entity. All rates are per committed PHYSIS tick
// (the map loader converts authored per-second values with the tick rate).
struct TornadoTrack {
    TornadoPathMode mode = TornadoPathMode::Drift;
    Fixed speedPerTick{};            // metres per tick (Wander, Waypoints)
    Fixed heading{};                 // turns [0,1): 0 = +X, 0.25 = +Z
    Fixed turnPerTick{};             // max random heading change per tick, turns
    std::uint64_t rng = 1;           // explicit RNG state (wander, debris)
    Vec3 boundsMin{}, boundsMax{};   // x/z wander box; empty box = PHYSIS grid
    std::vector<Vec3> waypoints;     // x/z, y ignored
    std::uint32_t nextWaypoint = 0;
    bool loop = false;
    std::uint32_t rampTicks = 0;     // spin-up and decay
    Fixed baseTangential{}, baseUpdraft{}, baseInflow{};
    Fixed intensity{};               // 0..1 published every tick
    Vec3 lastMove{};                 // last tick's displacement (presentation)
    // effects
    Fixed damagePerTick{};           // at intensity 1 inside the core
    Fixed damageRadiusMeters{};      // 0 = 2 x core (eye) radius
    Fixed fireSuppression{};         // share of core fire removed per tick, 0..1
    Fixed fireFanning{};             // relative ring fire growth per tick, 0..1
    std::uint32_t maxDebris = 0;
    Fixed debrisPerTick{};           // spawn rate at intensity 1
    Fixed debrisAccumulator{};
    Fixed debrisMassKg = Fixed::fromRatio(1, 2);
    Fixed debrisAreaSqMeters = Fixed::fromRatio(1, 20);
    std::uint32_t debrisLifeTicks = 400;
};

struct TornadoDebrisPiece {
    std::uint32_t id = 0;            // stable presentation key
    Entity owner{};                  // tornado entity that tore it up
    Vec3 position{}, velocity{};     // metres, m/s
    Fixed massKg = Fixed::fromRatio(1, 2);
    Fixed areaSqMeters = Fixed::fromRatio(1, 20);
    std::uint32_t ageTicks = 0, lifeTicks = 400;
    std::uint32_t restTicks = 0;     // ticks on the ground
    bool airborne = true;
};

struct TornadoDebrisField {
    std::vector<TornadoDebrisPiece> pieces; // creation order (stable)
    std::uint32_t nextId = 1;
};

// Opt-in for host objects (crates, barrels, props, light vehicles) that the
// wind may carry. PHYSIS writes Transform::pos only while airborne. Do not put
// it on bodies a physics engine (Box3D) owns: use applyPhysisToBody there.
struct NatureCarriable {
    Fixed massKg = Fixed::fromInt(5);
    Fixed areaSqMeters = Fixed::fromRatio(1, 4);
    Fixed dragCoefficient = Fixed::fromInt(1);
    Fixed liftWindSpeed = Fixed::fromInt(20); // horizontal m/s that lifts it
    Vec3 velocity{};
    Fixed groundY{};                 // where it rests (set when lifted)
    std::uint32_t airborneTicks = 0;
    bool airborne = false;
    bool enabled = true;
};

inline void io(Archive& a, TornadoTrack& v) {
    a(v.mode, v.speedPerTick, v.heading, v.turnPerTick, v.rng, v.boundsMin,
      v.boundsMax, v.waypoints, v.nextWaypoint, v.loop, v.rampTicks,
      v.baseTangential, v.baseUpdraft, v.baseInflow, v.intensity, v.lastMove,
      v.damagePerTick, v.damageRadiusMeters, v.fireSuppression, v.fireFanning,
      v.maxDebris, v.debrisPerTick, v.debrisAccumulator, v.debrisMassKg,
      v.debrisAreaSqMeters, v.debrisLifeTicks);
}
inline void io(Archive& a, TornadoDebrisPiece& v) {
    a(v.id, v.owner, v.position, v.velocity, v.massKg, v.areaSqMeters,
      v.ageTicks, v.lifeTicks, v.restTicks, v.airborne);
}
inline void io(Archive& a, TornadoDebrisField& v) { a(v.pieces, v.nextId); }
inline void io(Archive& a, NatureCarriable& v) {
    a(v.massKg, v.areaSqMeters, v.dragCoefficient, v.liftWindSpeed, v.velocity,
      v.groundY, v.airborneTicks, v.airborne, v.enabled);
}

// Host damage: target entity (has a Transform), amount (host units, e.g. hp),
// source = the tornado entity. Called in stable entity-index order.
struct TornadoHooks {
    Fixed secondsPerTick = Fixed::fromRatio(1, 20);
    void* user = nullptr;
    void (*damage)(void* user, Entity target, Fixed amount, Entity source) = nullptr;
};

struct TornadoTickStats {
    std::uint32_t active = 0, debris = 0, carried = 0, damaged = 0;
};

namespace tornado_detail {
inline std::uint64_t nextRandom(std::uint64_t& state) {
    state = mix64(state + 0x9e3779b97f4a7c15ull);
    return state;
}
// Uniform in [-1, 1) as Fixed.
inline Fixed signedUnit(std::uint64_t& state) {
    const auto r = nextRandom(state);
    return Fixed::fromRaw(std::int64_t(r >> 47) - Fixed::kOne);
}
// Uniform in [0, 1).
inline Fixed unit(std::uint64_t& state) {
    return Fixed::fromRaw(std::int64_t(nextRandom(state) >> 48));
}
inline Fixed wrapTurns(Fixed t) {
    std::int64_t r = t.raw % Fixed::kOne;
    if (r < 0) r += Fixed::kOne;
    return Fixed::fromRaw(r);
}
inline Fixed horizontalDistance(Vec3 a, Vec3 b) {
    const Fixed dx = a.x - b.x, dz = a.z - b.z;
    // Guard Q47.16 overflow for far-apart points: callers cull by box first.
    if (fxAbs(dx) > Fixed::fromInt(1000000) || fxAbs(dz) > Fixed::fromInt(1000000))
        return Fixed::fromInt(2000000);
    return fxSqrt(dx * dx + dz * dz);
}
// 1 inside the core, linear to 0 at `outer`.
inline Fixed falloff(Fixed distance, Fixed core, Fixed outer) {
    if (distance <= core) return Fixed::fromInt(1);
    if (distance >= outer || outer <= core) return Fixed{};
    return (outer - distance) / (outer - core);
}
inline Fixed groundAt(const World& world, Vec3 p, Fixed fallback) {
    const Entity cell = cellAtPosition(world, p);
    if (cell == kNullEntity) return fallback;
    const auto* c = world.tryGetConst<Cell>(cell);
    return c ? c->elevation : fallback;
}
// One explicit-Euler step of a wind-carried point mass. Quadratic drag
// relaxes velocity toward the local wind without overshoot; then gravity.
inline void carryStep(const World& world, Vec3& position, Vec3& velocity,
                      Fixed massKg, Fixed areaSqMeters, Fixed dragCoefficient,
                      Fixed dt) {
    const Vec3 wind = windWithStormsAt(world, position);
    const Vec3 rel = wind - velocity;
    const Fixed speed = rel.length();
    const Fixed mass = fxMax(massKg, Fixed::fromRatio(1, 100));
    const Fixed k = Fixed::fromRatio(1225, 2000) * dragCoefficient * areaSqMeters / mass;
    const Fixed blend = fxClamp(k * speed * dt, Fixed{}, Fixed::fromInt(1));
    velocity = velocity + rel * blend;
    velocity = velocity + gravityAt(world, position) * dt;
    const Fixed cap = Fixed::fromInt(120);
    velocity.x = fxClamp(velocity.x, -cap, cap);
    velocity.y = fxClamp(velocity.y, -cap, cap);
    velocity.z = fxClamp(velocity.z, -cap, cap);
    position = position + velocity * dt;
}
} // namespace tornado_detail

inline bool isTornado(const StormEvent& e) { return e.kind == StormKind::Tornado; }

// Spin-up / decay envelope for the climate tick `tick`; 0 when inactive.
inline Fixed tornadoEnvelope(const StormEvent& e, const TornadoTrack& t,
                             std::uint64_t tick) {
    if (!stormActive(e, tick)) return Fixed{};
    if (t.rampTicks == 0) return Fixed::fromInt(1);
    const std::uint64_t age = tick - e.startTick;
    Fixed env = Fixed::fromRatio(std::int64_t(std::min<std::uint64_t>(age + 1, t.rampTicks)),
                                 std::int64_t(t.rampTicks));
    if (e.durationTicks != 0) {
        const std::uint64_t left = std::uint64_t(e.durationTicks) - age; // >= 1 while active
        if (left < t.rampTicks)
            env = fxMin(env, Fixed::fromRatio(std::int64_t(left), std::int64_t(t.rampTicks)));
    }
    return env;
}

// Pairs a fresh track with its storm: remembers the authored wind as the base
// and keeps drift only for TornadoPathMode::Drift. Call before adding both.
inline void prepareTornado(StormEvent& storm, TornadoTrack& track) {
    storm.kind = StormKind::Tornado;
    track.baseTangential = storm.tangentialWind;
    track.baseUpdraft = storm.updraft;
    track.baseInflow = storm.radialInflow;
    if (track.mode != TornadoPathMode::Drift) storm.driftPerTick = {};
    if (track.rng == 0) track.rng = 1;
}

// Creates a tornado entity at runtime (persistence must have been registered
// with InstallOptions::enableTornadoes for saves/replays to include it).
inline Entity spawnTornado(World& world, StormEvent storm, TornadoTrack track) {
    prepareTornado(storm, track);
    const Entity e = world.create();
    world.add<StormEvent>(e, storm);
    world.add<TornadoTrack>(e, std::move(track));
    return e;
}

namespace tornado_detail {
inline void moveTornado(const World& world, StormEvent& storm, TornadoTrack& t) {
    const Vec3 before = storm.center;
    if (t.mode == TornadoPathMode::Wander && t.speedPerTick > Fixed{}) {
        t.heading = wrapTurns(t.heading + signedUnit(t.rng) * t.turnPerTick);
        Vec3 lo = t.boundsMin, hi = t.boundsMax;
        if (!(hi.x > lo.x && hi.z > lo.z)) {
            if (const auto* s = world.resourceConst<State>()) {
                const auto cs = std::int64_t(s->settings.cellSizeMeters);
                lo = {Fixed::fromInt(std::int64_t(s->settings.originX) * cs), Fixed{},
                      Fixed::fromInt(std::int64_t(s->settings.originZ) * cs)};
                hi = {lo.x + Fixed::fromInt(std::int64_t(s->settings.width) * cs), Fixed{},
                      lo.z + Fixed::fromInt(std::int64_t(s->settings.height) * cs)};
            }
        }
        Vec3 next = storm.center;
        next.x = next.x + fxCosTurns(t.heading) * t.speedPerTick;
        next.z = next.z + fxSinTurns(t.heading) * t.speedPerTick;
        if (hi.x > lo.x && hi.z > lo.z) {
            if (next.x < lo.x || next.x > hi.x) {          // reflect off X walls
                t.heading = wrapTurns(Fixed::fromRatio(1, 2) - t.heading);
                next.x = fxClamp(next.x, lo.x, hi.x);
            }
            if (next.z < lo.z || next.z > hi.z) {          // reflect off Z walls
                t.heading = wrapTurns(-t.heading);
                next.z = fxClamp(next.z, lo.z, hi.z);
            }
        }
        storm.center.x = next.x;
        storm.center.z = next.z;
    } else if (t.mode == TornadoPathMode::Waypoints && t.speedPerTick > Fixed{} &&
               !t.waypoints.empty()) {
        Fixed budget = t.speedPerTick;
        for (int guard = 0; guard < 4 && budget > Fixed{} &&
                            t.nextWaypoint < t.waypoints.size(); ++guard) {
            const Vec3 target = t.waypoints[t.nextWaypoint];
            const Fixed dist = horizontalDistance(storm.center, target);
            if (dist <= budget) {
                storm.center.x = target.x; storm.center.z = target.z;
                budget -= dist;
                ++t.nextWaypoint;
                if (t.nextWaypoint >= t.waypoints.size() && t.loop) t.nextWaypoint = 0;
            } else {
                storm.center.x = storm.center.x + (target.x - storm.center.x) * budget / dist;
                storm.center.z = storm.center.z + (target.z - storm.center.z) * budget / dist;
                t.heading = wrapTurns(fxAtan2Turns(target.z - before.z, target.x - before.x));
                budget = Fixed{};
            }
        }
    }
    // Drift mode: advanceStormTracks already applied StormEvent::driftPerTick.
    t.lastMove = storm.center - before;
}

inline void fireEffects(World& world, const StormEvent& storm, const TornadoTrack& t,
                        Fixed intensity) {
    if ((t.fireSuppression <= Fixed{} && t.fireFanning <= Fixed{}) ||
        !world.resourceConst<DisasterState>()) return;
    const auto* state = world.resourceConst<State>();
    if (!state) return;
    const auto cs = std::int64_t(state->settings.cellSizeMeters);
    const Fixed outer = storm.outerRadiusMeters, core = storm.eyeRadiusMeters;
    const std::int64_t x0 = floorDiv((storm.center.x - outer).floorInt(), cs);
    const std::int64_t x1 = floorDiv((storm.center.x + outer).floorInt(), cs);
    const std::int64_t z0 = floorDiv((storm.center.z - outer).floorInt(), cs);
    const std::int64_t z1 = floorDiv((storm.center.z + outer).floorInt(), cs);
    const std::int64_t gx0 = std::max<std::int64_t>(x0, state->settings.originX);
    const std::int64_t gz0 = std::max<std::int64_t>(z0, state->settings.originZ);
    const std::int64_t gx1 = std::min<std::int64_t>(x1, std::int64_t(state->settings.originX) + state->settings.width - 1);
    const std::int64_t gz1 = std::min<std::int64_t>(z1, std::int64_t(state->settings.originZ) + state->settings.height - 1);
    for (std::int64_t gz = gz0; gz <= gz1; ++gz)
        for (std::int64_t gx = gx0; gx <= gx1; ++gx) {
            const Entity e = cellAt(world, std::int32_t(gx), std::int32_t(gz));
            if (e == kNullEntity) continue;
            auto* hz = world.tryGet<HazardCell>(e);
            if (!hz || hz->fire <= Fixed{}) continue;
            const Vec3 mid{Fixed::fromInt(gx * cs) + Fixed::fromRatio(cs, 2), Fixed{},
                           Fixed::fromInt(gz * cs) + Fixed::fromRatio(cs, 2)};
            // A cell overlaps the core if its centre is within core + half a cell.
            const Fixed d = horizontalDistance(mid, storm.center);
            const Fixed half = Fixed::fromRatio(cs, 2);
            if (d <= core + half) {
                const Fixed cut = fxClamp(t.fireSuppression * intensity, Fixed{}, Fixed::fromInt(1));
                hz->fire = fxClamp(hz->fire - hz->fire * cut, Fixed{}, Fixed::fromInt(1));
            } else if (d < outer + half && hz->fuel > Fixed{}) {
                const Fixed ring = falloff(d, core + half, outer + half);
                const Fixed gain = t.fireFanning * intensity * ring;
                hz->fire = fxClamp(hz->fire + hz->fire * gain, Fixed{}, Fixed::fromInt(1));
            }
        }
}
} // namespace tornado_detail

// The tornado pass. Runs once per committed PHYSIS tick, after the climate /
// storm / disaster update (install() registers it as "physis.tornadoes").
inline TornadoTickStats advanceTornadoes(World& world, const TornadoHooks& hooks = {}) {
    using namespace tornado_detail;
    TornadoTickStats stats;
    const auto* state = world.resourceConst<State>();
    if (!state) return stats;
    const std::uint64_t now = state->tick;          // next climate tick to run
    const Fixed dt = hooks.secondsPerTick > Fixed{} ? hooks.secondsPerTick
                                                    : Fixed::fromRatio(1, 20);
    // Stable order: entity index, never pool (dense) order.
    std::vector<Entity> tornadoes;
    world.eachExistingConst<TornadoTrack>([&](Entity e, const TornadoTrack&) {
        if (const auto* s = world.tryGetConst<StormEvent>(e); s && isTornado(*s))
            tornadoes.push_back(e);
    });
    std::sort(tornadoes.begin(), tornadoes.end(),
              [](Entity a, Entity b) { return a.packed() < b.packed(); });

    // 1) Debris already in the air (last tick's wind field).
    auto* field = world.resource<TornadoDebrisField>();
    std::vector<std::pair<Entity, std::uint32_t>> alive; // owner -> count
    if (field) {
        std::vector<TornadoDebrisPiece> kept;
        kept.reserve(field->pieces.size());
        for (auto p : field->pieces) {
            ++p.ageTicks;
            if (p.airborne) {
                carryStep(world, p.position, p.velocity, p.massKg, p.areaSqMeters,
                          Fixed::fromInt(1), dt);
                const Fixed ground = groundAt(world, p.position, Fixed{});
                if (p.position.y <= ground) {
                    p.position.y = ground;
                    p.velocity = {};
                    p.airborne = false;
                }
            } else {
                // Resting pieces can be picked up again by strong updraft.
                const Vec3 w = windWithStormsAt(world, p.position + Vec3{Fixed{}, Fixed::fromInt(1), Fixed{}});
                if (w.y > Fixed::fromInt(6)) { p.airborne = true; p.restTicks = 0; }
                else ++p.restTicks;
            }
            if (p.ageTicks >= p.lifeTicks || p.restTicks > 60) continue;
            auto it = std::find_if(alive.begin(), alive.end(),
                                   [&](const auto& a) { return a.first == p.owner; });
            if (it == alive.end()) alive.push_back({p.owner, 1}); else ++it->second;
            kept.push_back(p);
        }
        field->pieces = std::move(kept);
    }

    // 2) Each tornado: life envelope, effects at the current centre, move.
    std::vector<std::pair<Entity, Fixed>> hits;
    for (const Entity e : tornadoes) {
        auto* storm = world.tryGet<StormEvent>(e);
        auto* track = world.tryGet<TornadoTrack>(e);
        if (!storm || !track) continue;
        const Fixed intensity = tornadoEnvelope(*storm, *track, now);
        track->intensity = intensity;
        storm->tangentialWind = track->baseTangential * intensity;
        storm->updraft = track->baseUpdraft * intensity;
        storm->radialInflow = track->baseInflow * intensity;
        if (intensity <= Fixed{}) { track->lastMove = {}; continue; }
        ++stats.active;

        fireEffects(world, *storm, *track, intensity);

        // Debris torn up inside the core.
        if (track->maxDebris > 0 && track->debrisPerTick > Fixed{}) {
            if (!field) {
                world.setResource<TornadoDebrisField>(TornadoDebrisField{});
                field = world.resource<TornadoDebrisField>();
                storm = world.tryGet<StormEvent>(e);
                track = world.tryGet<TornadoTrack>(e);
            }
            track->debrisAccumulator = fxMin(track->debrisAccumulator +
                track->debrisPerTick * intensity, Fixed::fromInt(64));
            std::uint32_t count = 0;
            for (const auto& a : alive) if (a.first == e) count = a.second;
            while (track->debrisAccumulator >= Fixed::fromInt(1) && count < track->maxDebris &&
                   field->pieces.size() < 4096) {
                track->debrisAccumulator -= Fixed::fromInt(1);
                TornadoDebrisPiece p;
                p.id = field->nextId++;
                if (field->nextId == 0) field->nextId = 1;
                p.owner = e;
                const Fixed angle = unit(track->rng);
                const Fixed r = storm->eyeRadiusMeters * (Fixed::fromRatio(1, 4) +
                                unit(track->rng) * Fixed::fromRatio(3, 4));
                p.position = storm->center;
                p.position.x = p.position.x + fxCosTurns(angle) * r;
                p.position.z = p.position.z + fxSinTurns(angle) * r;
                p.position.y = groundAt(world, p.position, storm->center.y) + Fixed::fromRatio(1, 2);
                const Fixed scale = Fixed::fromRatio(1, 2) + unit(track->rng);
                p.massKg = track->debrisMassKg * scale;
                p.areaSqMeters = track->debrisAreaSqMeters;
                p.lifeTicks = track->debrisLifeTicks;
                p.velocity = windWithStormsAt(world, p.position) * Fixed::fromRatio(1, 2);
                p.velocity.y = p.velocity.y + Fixed::fromInt(3);
                field->pieces.push_back(p);
                ++count;
            }
        }

        // Damage to host entities near the core.
        if (hooks.damage && track->damagePerTick > Fixed{}) {
            const Fixed core = storm->eyeRadiusMeters;
            const Fixed reach = track->damageRadiusMeters > Fixed{} ?
                fxMax(track->damageRadiusMeters, core) : core * 2;
            world.eachExistingConst<Transform>([&](Entity target, const Transform& tr) {
                if (fxAbs(tr.pos.x - storm->center.x) > reach ||
                    fxAbs(tr.pos.z - storm->center.z) > reach) return;
                if (world.tryGetConst<StormEvent>(target)) return;
                const Fixed w = falloff(horizontalDistance(tr.pos, storm->center), core, reach);
                const Fixed amount = track->damagePerTick * intensity * w;
                if (amount > Fixed{}) hits.push_back({target, amount});
            });
            std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
                return a.first.packed() < b.first.packed();
            });
            for (const auto& [target, amount] : hits) hooks.damage(hooks.user, target, amount, e);
            stats.damaged += std::uint32_t(hits.size());
            hits.clear();
            // The callback may have changed pools: re-fetch our components.
            storm = world.tryGet<StormEvent>(e);
            track = world.tryGet<TornadoTrack>(e);
            if (!storm || !track) continue;
        }

        moveTornado(world, *storm, *track);
    }

    // 3) Opt-in host objects carried by the (now updated) wind.
    std::vector<Entity> carriables;
    world.eachExistingConst<NatureCarriable>([&](Entity e, const NatureCarriable& c) {
        if (c.enabled && world.tryGetConst<Transform>(e)) carriables.push_back(e);
    });
    std::sort(carriables.begin(), carriables.end(),
              [](Entity a, Entity b) { return a.packed() < b.packed(); });
    for (const Entity e : carriables) {
        auto* c = world.tryGet<NatureCarriable>(e);
        auto* tr = world.tryGet<Transform>(e);
        if (!c || !tr) continue;
        if (!c->airborne) {
            const Vec3 w = windWithStormsAt(world, tr->pos + Vec3{Fixed{}, Fixed::fromInt(1), Fixed{}});
            const Fixed hx = fxAbs(w.x), hz = fxAbs(w.z);
            if (hx < c->liftWindSpeed && hz < c->liftWindSpeed &&
                fxSqrt(w.x * w.x + w.z * w.z) < c->liftWindSpeed) continue;
            c->airborne = true;
            c->groundY = tr->pos.y;
            c->airborneTicks = 0;
            c->velocity = {Fixed{}, Fixed::fromInt(2), Fixed{}};
        }
        Vec3 p = tr->pos;
        carryStep(world, p, c->velocity, c->massKg, c->areaSqMeters, c->dragCoefficient, dt);
        ++c->airborneTicks;
        const Fixed ground = fxMax(groundAt(world, p, c->groundY), c->groundY);
        if (p.y <= ground || c->airborneTicks > 2400) {
            p.y = ground;
            c->velocity = {};
            c->airborne = false;
        } else {
            ++stats.carried;
        }
        tr->pos = p;
    }
    if (field) stats.debris = std::uint32_t(field->pieces.size());
    return stats;
}

} // namespace aiverse::physis
