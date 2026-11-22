// PHYSIS tornado: deterministic path (wander / waypoints), life envelope,
// wind field, fire rules, debris, carried objects, host damage through
// GameModule::environmentDamage, map install, save/load and checksums.
#include "engine/physis/PhysisAutoInstall.h"
#include "engine/physis/PhysisProjectConfig.h"
#include "engine/physis/PhysisTornadoAssets.h"
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS TORNADO FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

Settings grid(std::int32_t w = 16, std::int32_t h = 12, std::int32_t cell = 4) {
    Settings s;
    s.width = w; s.height = h; s.cellSizeMeters = cell; s.seed = 5;
    s.baseTemperature = Fixed::fromInt(24); s.baseHumidity = Fixed::fromRatio(3, 10);
    return s;
}

StormEvent vortex(Vec3 at) {
    StormEvent e;
    e.kind = StormKind::Tornado;
    e.center = at;
    e.eyeRadiusMeters = Fixed::fromInt(3);
    e.outerRadiusMeters = Fixed::fromInt(14);
    e.heightMeters = Fixed::fromInt(60);
    e.tangentialWind = Fixed::fromInt(45);
    e.updraft = Fixed::fromInt(18);
    e.radialInflow = Fixed::fromInt(5);
    return e;
}

TornadoTrack wander(std::uint64_t seed) {
    TornadoTrack t;
    t.mode = TornadoPathMode::Wander;
    t.speedPerTick = Fixed::fromRatio(1, 10);       // 2 m/s at 20 Hz
    t.turnPerTick = Fixed::fromRatio(1, 200);
    t.rng = seed;
    t.boundsMin = {Fixed::fromInt(4), Fixed{}, Fixed::fromInt(4)};
    t.boundsMax = {Fixed::fromInt(60), Fixed{}, Fixed::fromInt(44)};
    return t;
}

TornadoTickStats step(World& w) {
    if (w.resourceConst<DisasterState>()) advanceEnvironmentWithStormsAndDisasters(w);
    else advanceEnvironmentWithStorms(w);
    return advanceTornadoes(w);
}

Vec3 centerOf(const World& w) {
    Vec3 c{};
    w.eachExistingConst<StormEvent>([&](Entity, const StormEvent& e) { c = e.center; });
    return c;
}

void wanderPath() {
    World a, b, other;
    for (World* w : {&a, &b, &other}) { initialize(*w, grid()); initializeDisasters(*w); }
    (void)spawnTornado(a, vortex({Fixed::fromInt(32), Fixed{}, Fixed::fromInt(24)}), wander(7));
    (void)spawnTornado(b, vortex({Fixed::fromInt(32), Fixed{}, Fixed::fromInt(24)}), wander(7));
    (void)spawnTornado(other, vortex({Fixed::fromInt(32), Fixed{}, Fixed::fromInt(24)}), wander(8));
    bool differs = false, inside = true;
    World snapshot;
    for (int i = 0; i < 600; ++i) {
        if (i == 300) snapshot = a;
        step(a); step(b); step(other);
        const Vec3 ca = centerOf(a), cb = centerOf(b), co = centerOf(other);
        CHECK(ca == cb);
        if (!(ca == co)) differs = true;
        if (ca.x < Fixed::fromInt(4) || ca.x > Fixed::fromInt(60) ||
            ca.z < Fixed::fromInt(4) || ca.z > Fixed::fromInt(44)) inside = false;
    }
    CHECK(differs);                 // the seed matters
    CHECK(inside);                  // wander reflects off the bounds
    const Vec3 end = centerOf(a);
    CHECK(!(end == Vec3{Fixed::fromInt(32), Fixed{}, Fixed::fromInt(24)}));
    // Rewind / replay from a World copy: identical continuation.
    for (int i = 300; i < 600; ++i) step(snapshot);
    CHECK(centerOf(snapshot) == end);
}

void waypointPath() {
    World w; initialize(w, grid());
    TornadoTrack t;
    t.mode = TornadoPathMode::Waypoints;
    t.speedPerTick = Fixed::fromInt(1);
    t.waypoints = {{Fixed::fromInt(20), Fixed{}, Fixed::fromInt(10)},
                   {Fixed::fromInt(20), Fixed{}, Fixed::fromInt(30)}};
    t.loop = true;
    StormEvent e = vortex({Fixed::fromInt(10), Fixed{}, Fixed::fromInt(10)});
    e.driftPerTick = {Fixed::fromInt(5), Fixed{}, Fixed{}};   // ignored for paths
    (void)spawnTornado(w, e, t);
    for (int i = 0; i < 10; ++i) step(w);
    CHECK(centerOf(w).x == Fixed::fromInt(20) && centerOf(w).z == Fixed::fromInt(10));
    for (int i = 0; i < 20; ++i) step(w);
    CHECK(centerOf(w).x == Fixed::fromInt(20) && centerOf(w).z == Fixed::fromInt(30));
    for (int i = 0; i < 20; ++i) step(w);                     // loops back
    CHECK(centerOf(w).z == Fixed::fromInt(10));
}

void envelopeAndWind() {
    World w; initialize(w, grid());
    StormEvent e = vortex({Fixed::fromInt(32), Fixed{}, Fixed::fromInt(24)});
    e.startTick = 10; e.durationTicks = 100;
    TornadoTrack t; t.rampTicks = 20;
    const Entity id = spawnTornado(w, e, t);
    const Vec3 ring{Fixed::fromInt(35), Fixed::fromInt(2), Fixed::fromInt(24)};
    Fixed peak{}, early{};
    for (int i = 0; i < 130; ++i) {
        step(w);
        const auto* tr = w.tryGetConst<TornadoTrack>(id);
        const auto tick = w.resourceConst<State>()->tick;
        if (tick == 12) early = tr->intensity;
        peak = fxMax(peak, tr->intensity);
        if (tick == 60) {
            const Vec3 v = windWithStormsAt(w, ring);
            CHECK(v.z > windAt(w, ring).z + Fixed::fromInt(30));   // swirl (+Z on the +X side, CCW)
            CHECK(windWithStormsAt(w, {Fixed::fromInt(32), Fixed::fromInt(2), Fixed::fromInt(24)}).y >
                  Fixed::fromInt(10));                              // updraft in the core
            const Vec3 far{Fixed::fromInt(60), Fixed::fromInt(2), Fixed::fromInt(5)};
            CHECK(windWithStormsAt(w, far) == windAt(w, far));      // outside the ring: untouched
        }
    }
    CHECK(early > Fixed{} && early < Fixed::fromRatio(1, 4));  // spin-up
    CHECK(peak == Fixed::fromInt(1));
    CHECK(w.tryGetConst<TornadoTrack>(id)->intensity == Fixed{}); // over
    CHECK(windWithStormsAt(w, ring) == windAt(w, ring));
}

void fireRules() {
    auto burning = [](bool withTornado) {
        World w; initialize(w, grid()); initializeDisasters(w);
        CHECK(ignite(w, 8, 6, Fixed::fromRatio(1, 5)));
        CHECK(ignite(w, 10, 6, Fixed::fromRatio(1, 5)));
        if (withTornado) {
            TornadoTrack t;
            t.fireSuppression = Fixed::fromRatio(1, 5);
            t.fireFanning = Fixed::fromRatio(1, 10);
            (void)spawnTornado(w, vortex({Fixed::fromInt(34), Fixed{}, Fixed::fromInt(26)}), t);
        }
        for (int i = 0; i < 4; ++i) step(w);
        HazardCell core{}, ring{};
        CHECK(hazardAt(w, 8, 6, core));    // cell centre (34, 26): in the core
        CHECK(hazardAt(w, 10, 6, ring));   // centre (42, 26): 8 m out, ring
        return std::make_pair(core.fire, ring.fire);
    };
    const auto calm = burning(false), storm = burning(true);
    CHECK(storm.first < calm.first);       // blown out in the core
    CHECK(storm.second > calm.second);     // fanned in the ring
    if (!(storm.second > calm.second)) std::printf("fire core %f/%f ring %f/%f\n", storm.first.toDouble(), calm.first.toDouble(), storm.second.toDouble(), calm.second.toDouble());
}

void debrisAndCarry() {
    World w; initialize(w, grid());
    TornadoTrack t = wander(3);
    t.maxDebris = 24;
    t.debrisPerTick = Fixed::fromRatio(1, 2);
    (void)spawnTornado(w, vortex({Fixed::fromInt(32), Fixed{}, Fixed::fromInt(24)}), t);
    const Entity crate = w.create();
    w.add<Transform>(crate, Transform{{Fixed::fromInt(34), Fixed{}, Fixed::fromInt(24)}, {}, Fixed{}});
    NatureCarriable c; c.massKg = Fixed::fromInt(2); c.liftWindSpeed = Fixed::fromInt(15);
    w.add<NatureCarriable>(crate, c);
    const Entity rock = w.create();                // far away: stays put
    w.add<Transform>(rock, Transform{{Fixed::fromInt(2), Fixed{}, Fixed::fromInt(2)}, {}, Fixed{}});
    w.add<NatureCarriable>(rock, c);
    Fixed highest{};
    std::uint32_t most = 0;
    bool lifted = false;
    for (int i = 0; i < 200; ++i) {
        const auto s = step(w);
        most = std::max(most, s.debris);
        if (const auto* f = w.resourceConst<TornadoDebrisField>())
            for (const auto& p : f->pieces) highest = fxMax(highest, p.position.y);
        if (w.tryGetConst<NatureCarriable>(crate)->airborne) lifted = true;
    }
    CHECK(most > 0 && most <= 24);
    CHECK(highest > Fixed::fromInt(2));            // torn up and lifted
    CHECK(lifted);
    const Vec3 moved = w.tryGetConst<Transform>(crate)->pos;
    CHECK(!(moved == Vec3{Fixed::fromInt(34), Fixed{}, Fixed::fromInt(24)}));
    CHECK(w.tryGetConst<Transform>(rock)->pos == (Vec3{Fixed::fromInt(2), Fixed{}, Fixed::fromInt(2)}));
    CHECK(moved.y >= Fixed{});                     // never below its ground
    // Tornado force on a generic drag receiver (vegetation stress reads it).
    ForceReceiver r; r.position = centerOf(w) + Vec3{Fixed::fromInt(3), Fixed::fromInt(2), Fixed{}};
    World calm; initialize(calm, grid());
    const Fixed loaded = fxAbs(evaluateForces(w, r).aerodynamicForce.z) + fxAbs(evaluateForces(w, r).aerodynamicForce.x);
    const Fixed quiet = fxAbs(evaluateForces(calm, r).aerodynamicForce.z) + fxAbs(evaluateForces(calm, r).aerodynamicForce.x);
    CHECK(loaded > quiet + Fixed::fromInt(100));   // ~0.5*rho*Cd*A*v^2, v ~ 40 m/s
}

// Any genre: records environmental damage like a game would apply it.
class HostModule final : public GameModule {
public:
    std::string name() const override { return "host"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        World& w = sim.world();
        for (int i = 0; i < 6; ++i) {
            const Entity e = w.create();
            w.add<Transform>(e, Transform{{Fixed::fromInt(10 + 9 * i), Fixed{}, Fixed::fromInt(24)}, {}, Fixed{}});
            ids.push_back(e);
        }
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher& h) override {
        for (const auto& [k, v] : damage) { h.u64(k); h.u64(std::uint64_t(v.raw)); }
    }
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&, Archive& a) override { a(damage); return a.ok(); }
    void environmentDamage(Simulation&, Entity target, Fixed amount, Entity) override {
        damage[target.packed()] += amount;
    }
    std::map<std::uint64_t, Fixed> damage;
    std::vector<Entity> ids;
};

const char* kMap = R"({"physis":{
  "grid":{"originX":0,"originZ":0,"width":16,"height":12,"cellSizeMeters":4,"seed":7},
  "climate":{"baseTemperature":24,"baseHumidity":0.3},
  "enableDisasters":true,
  "tornadoes":[
    {"id":"twister","center":[10,24],"coreRadiusMeters":3,"outerRadiusMeters":14,
     "heightMeters":60,"tangentialWind":45,"updraft":18,"radialInflow":6,
     "startTick":5,"durationSeconds":20,"rampSeconds":2,
     "path":{"mode":"waypoints","speed":3,"points":[[60,24]]},
     "damagePerSecond":10,"damageRadiusMeters":6,
     "fire":{"suppression":0.5,"fanning":0.3},
     "debris":{"max":32,"perSecond":10,"massKg":0.4}}
  ]}})";

std::unique_ptr<Simulation> make(const Json& map) {
    SimConfig cfg; cfg.seed = 3;
    auto sim = std::make_unique<Simulation>(cfg, std::make_unique<HostModule>());
    sim->setup(Json::object(), map);
    return sim;
}

void mapInstallDamageAndSaves() {
    const Json map = Json::parse(kMap);
    auto a = make(map), b = make(map);
    CHECK(a->world().resourceConst<TornadoDebrisField>() != nullptr);
    CHECK(a->world().countExisting<TornadoTrack>() == 1);
    CHECK(a->systemCount() == 2);                  // environment + tornadoes
    for (int i = 0; i < 200; ++i) { a->step(); b->step(); }
    CHECK(a->checksum() == b->checksum());
    const auto bytes = a->serialize();
    auto c = make(map);
    std::string error;
    CHECK(c->deserialize(bytes, &error));
    if (!error.empty()) std::printf("deserialize: %s\n", error.c_str());
    CHECK(c->checksum() == a->checksum());
    for (int i = 0; i < 300; ++i) { a->step(); b->step(); c->step(); }
    CHECK(a->checksum() == b->checksum());
    CHECK(c->checksum() == a->checksum());
    // Every host on the straight path (z = 24) was hit, damage grows with time.
    const auto& host = static_cast<const HostModule&>(a->module());
    std::size_t hit = 0;
    for (const auto& [k, v] : host.damage) if (v > Fixed{}) ++hit;
    CHECK(hit >= 5);
    // The tornado reached its last waypoint and expired after 20 s (+5 ticks).
    const Vec3 end = centerOf(a->world());
    CHECK(end.x == Fixed::fromInt(60));
    a->world().eachExistingConst<TornadoTrack>([&](Entity, const TornadoTrack& t) {
        CHECK(t.intensity == Fixed{});
    });
    // Same map without tornadoes keeps the old system list and schema.
    std::string plain = kMap;
    plain = plain.substr(0, plain.find(",\n  \"tornadoes\"")) + "}}";
    auto none = make(Json::parse(plain));
    CHECK(none->systemCount() == 1);
    CHECK(!none->world().resourceConst<TornadoDebrisField>());
}

void parseErrors() {
    const auto bad = [](const char* tornado) {
        const std::string text = std::string(R"({"tornadoes":[)") + tornado + "]}";
        try { (void)parseTornadoes(Json::parse(text), 20); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    CHECK(bad(R"({"center":[0,0]})"));                                   // no id
    CHECK(bad(R"({"id":"a","center":[0,0],"coreRadiusMeters":5,"outerRadiusMeters":4})"));
    CHECK(bad(R"({"id":"a","center":[0,0],"path":{"mode":"spiral"}})"));
    CHECK(bad(R"({"id":"a","center":[0,0],"startTick":1,"startSeconds":1})"));
    CHECK(bad(R"({"id":"a","center":[0,0]},{"id":"a","center":[1,1]})"));
    CHECK(bad(R"({"id":"a","center":[0,0],"path":{"mode":"waypoints","points":[]}})"));
    CHECK(!bad(R"({"id":"a","center":[0,0]})"));
    const auto ok = parseTornadoes(Json::parse(R"({"tornadoes":[{"id":"z","center":[1,2]},
        {"id":"b","center":[1,0,2],"durationSeconds":2,"path":{"mode":"wander","speed":4}}]})"), 20);
    CHECK(ok.size() == 2 && ok[0].id == "b");        // sorted by id
    CHECK(ok[0].storm.durationTicks == 40);
    CHECK(ok[0].track.speedPerTick == Fixed::fromRatio(1, 5));
    CHECK(ok[0].track.rampTicks == 60);              // default 3 s
}
} // namespace

int main() {
    wanderPath();
    waypointPath();
    envelopeAndWind();
    fireRules();
    debrisAndCarry();
    mapInstallDamageAndSaves();
    parseErrors();
    if (failures) { std::printf("PHYSIS TORNADO: %d failures\n", failures); return 1; }
    std::printf("PHYSIS TORNADO: OK\n");
    return 0;
}
