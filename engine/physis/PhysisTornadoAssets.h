// PHYSIS tornadoes from map data: the "physis.tornadoes" array (2026-11-22).
// Shared by the runtime installer (PhysisProjectConfig.h) and the editor/MCP
// validator, so both accept exactly the same documents. Authored per-second
// values are converted to per-tick values with the simulation tick rate.
//
//   "tornadoes": [{
//     "id": "twister",                         // stable ASCII id (spawn order)
//     "center": [20, 0, 12],                   // metres, or [x, z]
//     "coreRadiusMeters": 3,                   // peak-wind ring (StormEvent eye)
//     "outerRadiusMeters": 14, "heightMeters": 60,
//     "tangentialWind": 45, "updraft": 18, "radialInflow": 6,   // m/s
//     "clockwise": false,
//     "startTick": 40, "durationTicks": 1800,  // or startSeconds / durationSeconds
//     "rampSeconds": 5,                        // spin-up and decay (or rampTicks)
//     "path": {"mode": "wander", "speed": 2, "turnDegreesPerSecond": 30,
//              "seed": 3, "heading": 0, "bounds": [4, 4, 60, 44]},
//          // {"mode": "waypoints", "speed": 2.5, "points": [[10,10],[50,30]], "loop": true}
//          // {"mode": "drift", "velocity": [1, 0, 0]}     (m/s)
//     "damagePerSecond": 8, "damageRadiusMeters": 6,
//     "fire": {"suppression": 0.6, "fanning": 0.4},       // shares per second
//     "debris": {"max": 48, "perSecond": 8, "massKg": 0.5,
//                "areaSqMeters": 0.05, "lifeSeconds": 20}
//   }]
#pragma once
#include "PhysisTornado.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::physis {

struct AuthoredTornado {
    std::string id;
    StormEvent storm{};
    TornadoTrack track{};
};

namespace tornado_asset_detail {
[[noreturn]] inline void fail(const std::string& id, const std::string& what) {
    throw std::invalid_argument("PHYSIS tornado '" + id + "': " + what);
}
inline Fixed number(const Json& o, const char* key, Fixed fallback, Fixed lo, Fixed hi,
                    const std::string& id) {
    const Json& v = o[key];
    if (v.isNull()) return fallback;
    if (!v.isNumber()) fail(id, std::string(key) + " must be a number");
    const Fixed f = v.fx();
    if (f < lo || f > hi) fail(id, std::string(key) + " out of range");
    return f;
}
inline std::uint64_t integer(const Json& o, const char* key, std::uint64_t fallback,
                             std::uint64_t maximum, const std::string& id) {
    const Json& v = o[key];
    if (v.isNull()) return fallback;
    if (!v.isNumber() || v.numberLiteral().find_first_of(".eE-") != std::string::npos)
        fail(id, std::string(key) + " must be a non-negative integer");
    const auto n = v.integer(-1);
    if (n < 0 || std::uint64_t(n) > maximum) fail(id, std::string(key) + " out of range");
    return std::uint64_t(n);
}
inline bool flag(const Json& o, const char* key, bool fallback, const std::string& id) {
    const Json& v = o[key];
    if (v.isNull()) return fallback;
    if (v.type() != Json::Type::Bool) fail(id, std::string(key) + " must be boolean");
    return v.boolean();
}
// [x, z] or [x, y, z]
inline Vec3 point(const Json& v, const std::string& id, const char* what) {
    if (!v.isArray() || (v.size() != 2 && v.size() != 3))
        fail(id, std::string(what) + " must be [x, z] or [x, y, z]");
    for (std::size_t i = 0; i < v.size(); ++i)
        if (!v[i].isNumber()) fail(id, std::string(what) + " contains a non-number");
    const Fixed range = Fixed::fromInt(1000000);
    Vec3 p = v.size() == 2 ? Vec3{v[0].fx(), Fixed{}, v[1].fx()}
                           : Vec3{v[0].fx(), v[1].fx(), v[2].fx()};
    if (fxAbs(p.x) > range || fxAbs(p.y) > range || fxAbs(p.z) > range)
        fail(id, std::string(what) + " outside +-1000000 m");
    return p;
}
inline std::uint64_t ticks(const Json& o, const char* tickKey, const char* secondKey,
                           std::uint64_t fallback, std::uint32_t rate, const std::string& id) {
    const bool hasTicks = !o[tickKey].isNull(), hasSeconds = !o[secondKey].isNull();
    if (hasTicks && hasSeconds) fail(id, std::string(tickKey) + " and " + secondKey + " are exclusive");
    if (hasTicks) return integer(o, tickKey, fallback, 2147483647u, id);
    if (!hasSeconds) return fallback;
    const Fixed s = number(o, secondKey, Fixed{}, Fixed{}, Fixed::fromInt(10000000), id);
    return std::uint64_t((s * std::int64_t(rate) + Fixed::fromRatio(1, 2)).floorInt());
}
inline Fixed perTick(Fixed perSecond, std::uint32_t rate) {
    return perSecond / std::int64_t(std::max<std::uint32_t>(1, rate));
}
inline std::string id(const Json& o) {
    const Json& v = o["id"];
    if (!v.isString()) fail("", "stable string id required");
    std::string s = v.str();
    if (s.empty() || s.size() > 64) fail(s, "id must have 1..64 characters");
    for (unsigned char c : s)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '_' && c != '-' && c != '.')
            fail(s, "id accepts only ASCII letters, digits, '_', '-', '.'");
    return s;
}
} // namespace tornado_asset_detail

// Parses physisBlock["tornadoes"] (absent = empty). Throws std::invalid_argument
// on malformed data, before anything is installed. Sorted by id.
inline std::vector<AuthoredTornado> parseTornadoes(const Json& physisBlock,
                                                   std::uint32_t ticksPerSecond = 20) {
    using namespace tornado_asset_detail;
    std::vector<AuthoredTornado> out;
    if (!physisBlock.isObject()) return out;
    const Json& list = physisBlock["tornadoes"];
    if (list.isNull()) return out;
    if (!list.isArray()) throw std::invalid_argument("PHYSIS tornadoes must be an array");
    if (list.size() > 64) throw std::invalid_argument("PHYSIS tornadoes: at most 64");
    const std::uint32_t rate = std::max<std::uint32_t>(1, ticksPerSecond);
    const Fixed zero{}, one = Fixed::fromInt(1);
    for (const Json& o : list.items()) {
        if (!o.isObject()) throw std::invalid_argument("PHYSIS tornado must be an object");
        AuthoredTornado a;
        a.id = id(o);
        StormEvent& s = a.storm;
        TornadoTrack& t = a.track;
        s.kind = StormKind::Tornado;
        s.center = point(o["center"], a.id, "center");
        s.eyeRadiusMeters = number(o, "coreRadiusMeters", Fixed::fromInt(4), Fixed::fromRatio(1, 4),
                                   Fixed::fromInt(2000), a.id);
        s.outerRadiusMeters = number(o, "outerRadiusMeters", s.eyeRadiusMeters * 5,
                                     Fixed::fromRatio(1, 2), Fixed::fromInt(20000), a.id);
        if (s.outerRadiusMeters <= s.eyeRadiusMeters)
            fail(a.id, "outerRadiusMeters must exceed coreRadiusMeters");
        s.heightMeters = number(o, "heightMeters", Fixed::fromInt(80), one, Fixed::fromInt(20000), a.id);
        s.tangentialWind = number(o, "tangentialWind", Fixed::fromInt(45), zero, Fixed::fromInt(150), a.id);
        s.updraft = number(o, "updraft", Fixed::fromInt(15), zero, Fixed::fromInt(100), a.id);
        s.radialInflow = number(o, "radialInflow", Fixed::fromInt(5), zero, Fixed::fromInt(100), a.id);
        s.precipitationBoost = number(o, "precipitationBoost", Fixed::fromRatio(1, 4), zero, one, a.id);
        s.pressureDrop = number(o, "pressureDrop", Fixed::fromInt(20), zero, Fixed::fromInt(150), a.id);
        s.clockwise = flag(o, "clockwise", false, a.id);
        s.enabled = flag(o, "enabled", true, a.id);
        s.startTick = ticks(o, "startTick", "startSeconds", 0, rate, a.id);
        s.durationTicks = std::uint32_t(std::min<std::uint64_t>(
            ticks(o, "durationTicks", "durationSeconds", 0, rate, a.id), 2147483647u));
        t.rampTicks = std::uint32_t(std::min<std::uint64_t>(
            ticks(o, "rampTicks", "rampSeconds", rate * 3u, rate, a.id), 2147483647u));

        const Json& path = o["path"];
        if (!path.isNull()) {
            if (!path.isObject()) fail(a.id, "path must be an object");
            const std::string mode = path.getStr("mode", "drift");
            const Fixed speed = number(path, "speed", Fixed::fromInt(2), zero, Fixed::fromInt(100), a.id);
            if (mode == "wander") {
                t.mode = TornadoPathMode::Wander;
                t.speedPerTick = perTick(speed, rate);
                const Fixed turn = number(path, "turnDegreesPerSecond", Fixed::fromInt(30), zero,
                                          Fixed::fromInt(720), a.id);
                t.turnPerTick = perTick(turn / 360, rate);
                t.heading = number(path, "heading", zero, zero, Fixed::fromInt(360), a.id) / 360;
                t.rng = mix64(integer(path, "seed", 1, 2147483647u, a.id) ^ 0x544f524e41444full);
                if (const Json& b = path["bounds"]; !b.isNull()) {
                    if (!b.isArray() || b.size() != 4) fail(a.id, "bounds must be [minX, minZ, maxX, maxZ]");
                    for (std::size_t i = 0; i < 4; ++i)
                        if (!b[i].isNumber()) fail(a.id, "bounds contains a non-number");
                    t.boundsMin = {b[0].fx(), zero, b[1].fx()};
                    t.boundsMax = {b[2].fx(), zero, b[3].fx()};
                    if (!(t.boundsMax.x > t.boundsMin.x && t.boundsMax.z > t.boundsMin.z))
                        fail(a.id, "bounds must have max > min");
                }
            } else if (mode == "waypoints") {
                t.mode = TornadoPathMode::Waypoints;
                t.speedPerTick = perTick(speed, rate);
                t.loop = flag(path, "loop", false, a.id);
                const Json& pts = path["points"];
                if (!pts.isArray() || pts.size() == 0 || pts.size() > 256)
                    fail(a.id, "waypoints need 1..256 points");
                for (const Json& p : pts.items()) t.waypoints.push_back(point(p, a.id, "waypoint"));
            } else if (mode == "drift") {
                t.mode = TornadoPathMode::Drift;
                if (const Json& v = path["velocity"]; !v.isNull()) {
                    const Vec3 vel = point(v, a.id, "velocity");
                    const Fixed cap = Fixed::fromInt(100);
                    if (fxAbs(vel.x) > cap || fxAbs(vel.y) > cap || fxAbs(vel.z) > cap)
                        fail(a.id, "velocity components must be within +-100 m/s");
                    s.driftPerTick = {perTick(vel.x, rate), perTick(vel.y, rate), perTick(vel.z, rate)};
                }
            } else {
                fail(a.id, "path.mode must be drift, wander or waypoints");
            }
        }
        t.damagePerTick = perTick(number(o, "damagePerSecond", zero, zero, Fixed::fromInt(100000), a.id), rate);
        t.damageRadiusMeters = number(o, "damageRadiusMeters", zero, zero, Fixed::fromInt(20000), a.id);
        if (const Json& f = o["fire"]; !f.isNull()) {
            if (!f.isObject()) fail(a.id, "fire must be an object");
            t.fireSuppression = fxMin(one, perTick(number(f, "suppression", zero, zero, Fixed::fromInt(20), a.id), rate));
            t.fireFanning = fxMin(one, perTick(number(f, "fanning", zero, zero, Fixed::fromInt(20), a.id), rate));
        }
        if (const Json& d = o["debris"]; !d.isNull()) {
            if (d.type() == Json::Type::Bool) {
                if (d.boolean()) { t.maxDebris = 32; t.debrisPerTick = perTick(Fixed::fromInt(6), rate); }
            } else {
                if (!d.isObject()) fail(a.id, "debris must be an object or boolean");
                t.maxDebris = std::uint32_t(integer(d, "max", 32, 512, a.id));
                t.debrisPerTick = perTick(number(d, "perSecond", Fixed::fromInt(6), zero, Fixed::fromInt(1000), a.id), rate);
                t.debrisMassKg = number(d, "massKg", t.debrisMassKg, Fixed::fromRatio(1, 100), Fixed::fromInt(1000), a.id);
                t.debrisAreaSqMeters = number(d, "areaSqMeters", t.debrisAreaSqMeters, Fixed::fromRatio(1, 1000),
                                              Fixed::fromInt(100), a.id);
                t.debrisLifeTicks = std::uint32_t(std::clamp<std::uint64_t>(
                    ticks(d, "lifeTicks", "lifeSeconds", std::uint64_t(rate) * 20u, rate, a.id), 1u, 1000000u));
            }
        }
        prepareTornado(s, t);
        out.push_back(std::move(a));
    }
    std::sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.id < y.id; });
    for (std::size_t i = 1; i < out.size(); ++i)
        if (out[i - 1].id == out[i].id)
            throw std::invalid_argument("PHYSIS duplicate tornado id: " + out[i].id);
    return out;
}

inline void spawnTornadoes(World& world, const std::vector<AuthoredTornado>& list) {
    for (const auto& a : list) (void)spawnTornado(world, a.storm, a.track);
}

} // namespace aiverse::physis
