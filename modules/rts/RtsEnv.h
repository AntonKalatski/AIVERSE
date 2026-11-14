// Map environment, docs/game-format.md § Map environment: weather (rain puddles, snow, blizzard, lightning),
// directional wind, map zones, trample fields over grid fields (grass, snow), abilities emitted along splines
// (moving hazards), fog options (no explored memory, start reveal).
//
// Data: rules "environment" (defaults) + map "environment" (merged key by key). Everything is optional:
// a game without the block has no Environment state and keeps its checksums and saves.
// State: the EnvState world resource (rollback copies it, saves append it, hashState hashes it).
// Determinism: Fixed math only, sim.rng() in a fixed order, units visited in ECS order.
// Game-specific actors on top of it (bosses, scripted directors) are native modules: they read def() / state().
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"

namespace aiverse { class Simulation; class Hasher; class Archive; class Json; }

namespace aiverse::game {

class RtsModule;

// ---------------------------------------------------------------- shared geometry
// Catmull-Rom through the control points (10 nodes per segment), walked by distance.
struct EnvSpline {
    std::string id;
    std::vector<Vec3> points, nodes;
    std::vector<Fixed> cum;      // distance at each node
    Fixed length{};
    bool loop = false;
    // auto skill: emits `skill` at the first point and moves its areas along the spline
    int16_t skill = -1;          // player ability index (rules playerAbilities); -1 = path only
    Fixed velocity = Fixed::fromInt(100), emitPeriod = Fixed::fromInt(10), firstEmission = Fixed::fromInt(-1);
    bool enabled = true;
    void build(const std::vector<Vec3>& pts, bool loop, int smooth = 10);
    Vec3 at(Fixed d) const;      // by distance from the start, clamped to [0, length]
};

enum class EnvZoneKind : uint8_t { Water = 0, Road = 1, Other = 3 };
struct EnvZone {
    std::string id, tag;         // tag: the "kind" string as written (game modules find their own zones by it)
    EnvZoneKind kind = EnvZoneKind::Other;
    std::vector<Vec3> poly;      // polygon (x, z); a rect / circle is converted to a polygon / radius
    Vec3 center{}; Fixed radius{};   // radius > 0: circle zone
    Fixed slowdown = Fixed::fromInt(1);   // speed / slowdown inside
    bool contains(Vec3 p) const;
};

// ---------------------------------------------------------------- definitions
struct EnvWeatherDef {
    bool rain = false, snow = false, lightning = false, blizzard = false, winter = false;
    Fixed puddlesFill = Fixed::fromInt(20), puddlesDry = Fixed::fromInt(20), rainWait = Fixed::fromInt(60), noRainWait = Fixed::fromInt(30);
    Fixed snowRestore = Fixed::fromRatio(5, 1000), snowWait = Fixed::fromInt(30), snowFillChance = Fixed::fromRatio(6, 10);
    Fixed snowFillMin = Fixed::fromInt(40), snowFillMax = Fixed::fromInt(60);
    Fixed blizzardWait = Fixed::fromInt(30), blizzardFill = Fixed::fromInt(40), blizzardKillSpeed = Fixed::fromInt(5);
    Fixed blizzardSpeed = Fixed::fromRatio(1, 2), blizzardResistMin = Fixed::fromInt(1), blizzardResistMax = Fixed::fromInt(2);
    Fixed lightningKillTime = Fixed::fromInt(2), lightningRange = Fixed::fromInt(80), lightningTime = Fixed::fromInt(30), lightningWait = Fixed::fromInt(30);
    int lightningKillChance = 70, lightningStructureKill = 10;
    std::string burnId;
    Fixed rainSpread = Fixed::fromRatio(1, 2);   // rain scales the fire spread of trample killFields
    std::string blizzardVar = "blizzard";      // trigger var set to 1 while the blizzard blows (bots, scripts)
    int16_t burnField = -1; int32_t burnValue = 100; Fixed burnRadius{};   // lightning sets a grid field (fire / snow)
};
struct EnvWindDef {
    bool enabled = false;
    Fixed idleMin = Fixed::fromInt(5), idleMax = Fixed::fromInt(10), blowMin = Fixed::fromInt(5), blowMax = Fixed::fromInt(10);
    Fixed forceMin = Fixed::fromInt(40), forceMax = Fixed::fromInt(80);
};
struct EnvTrampleDef {          // units wear a grid field (fixed 0..1) down / up: grass, snow
    std::string fieldId, killId;  // grid field ids (rules "fields"), resolved at setup
    int16_t field = -1;
    Fixed perSecond{};          // added under every moving ground unit (winter: +0.1 trampled, grass: -scaleSpeed)
    std::vector<std::pair<Fixed, Fixed>> speed;   // winter: [health, mul] (last row with value <= health wins)
    Fixed lerpSpeed = Fixed::fromInt(-1);         // grass: speed lerp(1, lerpSpeed, value) while value > 0
    Fixed restore{};            // per second toward 0 while the weather's snow phase restores (winter)
    bool snowRestore = false;
    int16_t killField = -1; int32_t killAbove = 1;  // units on cells of killField >= killAbove die (burning grass)
    int16_t hideStatus = -1; Fixed hideAbove{};     // status while standing in value > hideAbove (grass hides units)
    bool skipRoads = true;                          // road zones: no speed table
};
struct EnvFogDef { bool memory = true; Fixed startSize{}, startTime{}; };   // explored memory, start reveal

struct EnvDef {
    bool active = false;
    Fixed scale = Fixed::fromInt(1);        // authored distance units per world unit (distances / speeds divide by it)
    EnvWeatherDef weather;
    EnvWindDef wind;
    std::vector<EnvZone> zones;
    std::vector<EnvSpline> splines;
    std::vector<EnvTrampleDef> trample;
    EnvFogDef fog;
};

// kEvEnv value = kind << 16 | data (presentation: weather / wind sounds and effects)
enum EnvEventKind : int64_t {
    kEnvWeather = 1,      // data: weather bits | phase << 8 (rain 1, snow 2, blizzard 4, lightning 8)
    kEnvLightning = 2,    // pos: strike, a: unit (if any)
    kEnvWind = 3,         // data: direction 0..7 (blow start) or 0xFF (calm)
};

// ---------------------------------------------------------------- state
struct EnvAuto { Fixed timer{}; bool late = false; struct Moving { Entity area = kNullEntity; Fixed dist{}; }; std::vector<Moving> moving; };
struct EnvUnit { uint32_t gen = 0; Fixed blizzard = Fixed::fromInt(1), resist = Fixed::fromInt(1); bool init = false; };

struct EnvState {
    // weather
    uint8_t puddlesState = 0, snowState = 0, blizzardState = 0, lightningState = 0;
    Fixed puddles{}, rainTimer{}, snowTimer{}, snowFill{}, blizzardTimer{}, lightningTimer{}, lightningKill{};
    Vec3 lightningPos{};
    // wind
    Vec3 windForce{}; Fixed windBlow{}, windIdle{}; int windDir = 0, windVisual = -1;
    std::vector<EnvAuto> autos;           // per spline with a skill
    std::vector<EnvUnit> units;           // per entity index: blizzard health
    std::vector<uint64_t> weatherKilled;  // units the weather killed this tick (lightning, blizzard); cleared every tick
    std::vector<std::vector<int32_t>> fogSeen;   // per player: cells visible after the last vision pass (fog without memory)
};

// ---------------------------------------------------------------- runtime
class Environment {
public:
    bool active() const { return def_.active; }
    const EnvDef& def() const { return def_; }
    // setup after setupMap (map entities exist); rules / map "environment" blocks
    void setup(Simulation& sim, RtsModule& m, const Json& rules, const Json& map);
    void tick(Simulation& sim);              // the "environment" system (before death)
    void fogTick(Simulation& sim);           // after vision: fog without explored memory, start reveal
    bool hasFog() const { return !def_.fog.memory || def_.fog.startTime.raw > 0; }
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    EnvState* state(Simulation& sim) const;

    void speed(Simulation& sim, Entity e, Fixed& add, Fixed& mul) const;   // wind, puddles, zones, blizzard, trample (RtsModule::stat)
    Fixed windMultiplier(Simulation& sim, Vec3 dir) const;                 // distance per second the wind adds along `dir`
    bool killedByWeather(Simulation& sim, Entity e) const;                 // this tick (death hooks: such units leave nothing)
    // casts a player ability at a point for `player` (0: neutral) without its cost; `areas` collects the areas it starts
    void cast(Simulation& sim, uint8_t player, int skill, Vec3 point, Entity target = kNullEntity, std::vector<Entity>* areas = nullptr);

private:
    void weather(Simulation& sim, EnvState& st);
    void wind(Simulation& sim, EnvState& st);
    void trample(Simulation& sim, EnvState& st);
    void units(Simulation& sim, EnvState& st);
    void autos(Simulation& sim, EnvState& st);
    Fixed range(Simulation& sim, Fixed lo, Fixed hi) const;           // uniform [lo, hi] from sim.rng()
    void kill(Simulation& sim, EnvState& st, Entity e, bool weather);
    std::vector<Entity> units_(Simulation& sim) const;               // live mobile ground units in ECS order

    RtsModule* m_ = nullptr;
    EnvDef def_;
};

} // namespace aiverse::game
