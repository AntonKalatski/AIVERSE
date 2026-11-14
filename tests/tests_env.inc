// Map environment (RtsEnv.h, docs/game-format.md § Map environment): weather, wind, zones, trample fields, auto
// skills on splines, fog options, saves and determinism; gameplay hooks of native modules (docs/native-modules.md).
#include "modules/rts/RtsEnv.h"

namespace {
const char* kEnvRules = R"J({
  "lanes": {"left": 0, "right": 0, "spacing": 0.4},
  "resources": {"morale": {"start": 0}},
  "playerAbilities": [
    {"id": "boom", "target": "point", "effects": [{"type": "damage", "amount": 1000, "radius": 2, "affects": "any"}]},
    {"id": "bless", "target": "point", "effects": [{"type": "garrisonAdd", "amount": 5, "radius": 3, "affects": "any"}]}
  ],
  "types": {
    "village": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "capturable": true, "garrison": {"max": 60, "unit": "shroom", "interval": 1}},
    "shroom": {"move": {"speed": 2}, "entersGarrison": true, "health": {"max": 10}},
    "marker": {"move": {"speed": 0}, "invulnerable": true, "countsForVictory": false}
  }
})J";

std::string envMap(const std::string& env, int w = 40, int h = 20, const std::string& extraEntities = "") {
    return flatMap(w, h, R"(,"entities":[{"type":"village","owner":1,"pos":[5,10],"garrison":40,"id":"home"},
        {"type":"village","owner":2,"pos":[35,10],"garrison":40,"id":"enemy"},
        {"type":"village","owner":0,"pos":[20,3],"garrison":10,"id":"north"},
        {"type":"village","owner":0,"pos":[20,17],"garrison":10,"id":"south"})" + extraEntities + R"(],
        "environment":)" + env);
}
void envSendCmd(Simulation& sim, uint8_t p, Entity from, Entity to, int pct) {
    Command c; c.player = p; c.type = game::kCmdSend; c.units = {from}; c.target = to; c.param = pct; sim.submit(c);
}
int envEvents(Simulation& sim, int64_t kind, int64_t data = -1) {
    int n = 0;
    for (auto& ev : sim.events())
        if (ev.type == game::kEvEnv && (ev.value >> 16) == kind && (data < 0 || (ev.value & 0xFFFF) == data)) ++n;
    return n;
}
Entity envPoint(Simulation& sim, int i) {   // the i-th village of envMap (nearest garrison to its map position)
    static const int at[4][2] = {{5, 10}, {35, 10}, {20, 3}, {20, 17}};
    const Vec3 p{Fixed::fromInt(at[i][0]), Fixed{}, Fixed::fromInt(at[i][1])};
    Entity best; Fixed bd{};
    sim.world().each<game::Garrison, Transform>([&](Entity e, game::Garrison&, Transform& t) {
        const Fixed d = (t.pos - p).xz().length();
        if (!best.valid() || d < bd) { best = e; bd = d; }
    });
    return best;
}
} // namespace

TEST(env_absent_keeps_the_game_unchanged) {
    auto sim = makeSimJson(kEnvRules, flatMap(20, 10, R"(,"entities":[{"type":"village","owner":1,"pos":[5,5]}])"));
    CHECK(mod(*sim).environment() == nullptr);
    CHECK(sim->world().resource<game::EnvState>() == nullptr);
    run(*sim, 1);
}

TEST(env_weather_and_wind_cycles) {
    auto sim = makeSimJson(kEnvRules, envMap(R"({"weather": ["rain", "lightning"],
        "weatherParams": {"puddlesFillSpeed": 2, "puddlesDrySpeed": 2, "rainWaitTime": 1, "noRainWaitTime": 1,
                          "lightningKillTime": 0.5, "lightningKillFactor": 100, "lightningTime": 30, "lightningPointKillValue": 3},
        "wind": {"minIdleDuration": 0.5, "maxIdleDuration": 0.5, "minBlowDuration": 1, "maxBlowDuration": 1, "minForce": 1, "maxForce": 1},
        "zones": [{"id": "pond", "kind": "water", "rect": [10, 8, 20, 4], "slowdown": 2}]})"), 3);
    auto& m = mod(*sim);
    auto* env = m.environment();
    CHECK(env != nullptr);
    auto* st = sim->world().resource<game::EnvState>();
    bool puddleFull = false, puddleDry = false, blew = false, struck = false;
    Fixed windSeen{};
    for (int t = 0; t < 20 * 8; ++t) {
        sim->step();
        if (st->puddlesState == 1) puddleFull = true;
        if (st->puddlesState == 3) puddleDry = true;
        if (envEvents(*sim, game::kEnvWind) && st->windBlow.raw > 0) blew = true;
        if (st->windBlow.raw > 0) windSeen = fxMax(windSeen, fxAbs(env->windMultiplier(*sim, Vec3{Fixed::fromInt(1), Fixed{}, Fixed{}})) + fxAbs(env->windMultiplier(*sim, Vec3{Fixed{}, Fixed{}, Fixed::fromInt(1)})));
        if (envEvents(*sim, game::kEnvLightning)) struck = true;
    }
    CHECK(puddleFull && puddleDry);   // fill -> wait -> dry -> wait
    CHECK(blew && windSeen > Fixed::fromRatio(1, 2));
    CHECK(st->windDir >= 1);
    CHECK(struck);                    // lightning every 0.5 s with a 100% chance
    // the same seed gives the same weather (sim RNG only)
    auto b = makeSimJson(kEnvRules, envMap(R"({"weather": ["rain", "lightning"], "weatherParams": {"puddlesFillSpeed": 2, "puddlesDrySpeed": 2, "rainWaitTime": 1, "noRainWaitTime": 1, "lightningKillTime": 0.5, "lightningKillFactor": 100, "lightningTime": 30, "lightningPointKillValue": 3}, "wind": {"minIdleDuration": 0.5, "maxIdleDuration": 0.5, "minBlowDuration": 1, "maxBlowDuration": 1, "minForce": 1, "maxForce": 1}, "zones": [{"id": "pond", "kind": "water", "rect": [10, 8, 20, 4], "slowdown": 2}]})"), 3);
    for (int t = 0; t < 20 * 8; ++t) b->step();
    CHECK(b->checksum() == sim->checksum());
}

TEST(env_water_zone_slows_and_blizzard_kills_walkers) {
    auto sim = makeSimJson(kEnvRules, envMap(R"({"weather": ["blizzard"],
        "weatherParams": {"blizzardFillTime": 100, "blizzardUnitKillSpeed": 1, "blizzardUnitKillFactor": [1, 1]},
        "zones": [{"id": "pond", "kind": "water", "rect": [6, 6, 26, 8], "slowdown": 2}]})"));
    auto& m = mod(*sim);
    Entity home = envPoint(*sim, 0), enemy = envPoint(*sim, 1);
    run(*sim, 0.2);
    envSendCmd(*sim, 1, home, enemy, 50);
    Fixed slowSeen{}; int walkersMax = 0;
    for (int t = 0; t < 20 * 3; ++t) {
        sim->step();
        int walkers = 0;
        sim->world().each<game::LaneWalker, game::Unit>([&](Entity e, game::LaneWalker&, game::Unit&) {
            ++walkers;
            Vec3 p = sim->world().get<Transform>(e).pos;
            if (p.x > Fixed::fromRatio(65, 10) && p.x < Fixed::fromInt(30)) slowSeen = fxMax(slowSeen, Fixed::fromInt(2) - m.stat(*sim, e, game::Stat::Speed));
        });
        walkersMax = std::max(walkersMax, walkers);
    }
    CHECK(walkersMax > 0);
    CHECK(slowSeen >= Fixed::fromRatio(9, 10));   // speed 2 / water slowdown 2 (no rain: puddles stay full)
    run(*sim, 2);
    int walkers = 0;
    sim->world().each<game::LaneWalker>([&](Entity, game::LaneWalker&) { ++walkers; });
    CHECK(walkers == 0);                          // frozen by the blizzard within a second on the way
    CHECK(sim->world().get<game::Garrison>(enemy).count >= 40);
}

TEST(env_auto_skill_fires_along_its_spline) {
    auto sim = makeSimJson(kEnvRules, envMap(R"({"splines": [{"id": "fire", "points": [[8, 2], [38, 2]], "skill": "bless", "velocity": 20, "emitPeriod": 1}]})"));
    int abilities = 0;
    for (int t = 0; t < 20 * 4; ++t) {
        sim->step();
        for (auto& ev : sim->events()) if (ev.type == game::kEvPlayerAbility && ev.player == 0) ++abilities;
    }
    CHECK(abilities >= 3);   // cast at the spline start every second, for the neutral player
}

TEST(env_save_load_continues_identically) {
    const std::string env = R"({"weather": ["rain", "lightning", "blizzard"], "weatherParams": {"lightningKillTime": 0.5, "lightningKillFactor": 50, "blizzardFillTime": 2, "blizzardWaitTime": 1},
        "wind": {"minIdleDuration": 0.3, "maxIdleDuration": 1, "minBlowDuration": 1, "maxBlowDuration": 2, "minForce": 0.5, "maxForce": 1},
        "splines": [{"id": "path", "points": [[6, 6], [34, 6], [34, 14]], "skill": "bless", "velocity": 10, "emitPeriod": 2}]})";
    const std::string map = envMap(env);
    auto a = makeSimJson(kEnvRules, map, 9);
    for (int t = 0; t < 20 * 6; ++t) {
        if (t % 30 == 0) envSendCmd(*a, 1, envPoint(*a, 0), envPoint(*a, 1), 40);
        if (t % 30 == 15) envSendCmd(*a, 2, envPoint(*a, 1), envPoint(*a, 0), 40);
        a->step();
    }
    const std::string bytes = a->serialize();
    CHECK(!bytes.empty());
    auto b = makeSimJson(kEnvRules, map, 9);
    std::string err;
    CHECK(b->deserialize(bytes, &err));
    CHECK(b->checksum() == a->checksum());
    for (int t = 0; t < 20 * 6; ++t) { a->step(); b->step(); }
    CHECK(b->checksum() == a->checksum());
    CHECK(a->serialize() == b->serialize());
}

TEST(env_rain_lowers_the_grass_fire_spread) {
    std::string rules = kEnvRules;
    rules.replace(rules.find("\"types\": {"), 10, R"("fields": {
        "grass": {"type": "fixed", "min": 0, "max": 1, "default": 1},
        "fire": {"type": "int", "min": 0, "max": 100, "kernels": [{"type": "spread", "period": 2, "above": 1, "set": 100, "fuel": {"field": "grass", "min": 0.1}}]}},
      "types": {)");
    auto burning = [&](bool rain) {
        const std::string env = std::string(R"({"weather": [)") + (rain ? "\"rain\"" : "") + R"(], "trample": [{"field": "grass", "killField": "fire"}]})";
        std::string map = envMap(env);
        map.insert(map.size() - 1, R"(,"gridFields": {"fire": {"fill": [{"rect": [20, 10, 1, 1], "value": 100}]}})");
        auto sim = makeSimJson(rules, map, 5);
        run(*sim, 1);
        auto* gf = mod(*sim).gridFields();
        int n = 0, fi = gf->find("fire");
        for (int z = 0; z < 20; ++z) for (int x = 0; x < 40; ++x) n += gf->get(*sim, fi, x, z) > 0;
        return n;
    };
    const int dry = burning(false), wet = burning(true);
    CHECK(dry > 40);
    CHECK(wet < dry);   // rain factor 0.5: half the neighbours catch per run
}

TEST(env_blizzard_sets_the_trigger_var) {
    auto sim = makeSimJson(kEnvRules, envMap(R"({"weather": ["blizzard"], "weatherParams": {"blizzardFillTime": 1, "blizzardWaitTime": 1}})"));
    auto& gs = mod(*sim).state(*sim);
    bool on = false, off = false;
    for (int t = 0; t < 20 * 3; ++t) {
        sim->step();
        auto it = gs.trig.vars.find("blizzard");
        if (it != gs.trig.vars.end()) { if (it->second.raw) on = true; else off = true; }
    }
    CHECK(on && off);                          // 1 while it blows, 0 while it waits
}

TEST(env_fog_ellipse_start_reveal_and_no_memory) {
    std::string rules = kEnvRules;
    rules.insert(1, R"("fogOfWar": {"mode": "full", "ellipse": 2},)");
    rules.replace(rules.find("\"shroom\": {\"move\": {\"speed\": 2}"), 31, R"("shroom": {"move": {"speed": 2}, "sight": 6)");
    auto sim = makeSimJson(rules, envMap(R"({"fog": {"memory": false, "startFowSize": 60, "startDuration": 1}})", 40, 20,
        R"(,{"type":"shroom","owner":1,"pos":[20.5,10.5]})"));
    auto& gs = mod(*sim).state(*sim);
    const auto& g = gs.grid;
    run(*sim, 0.3);
    CHECK(gs.vision[1][size_t(g.index(36, 10))] == 2);      // start reveal around the first building (x 5): far east still lit
    run(*sim, 1.5);
    CHECK(gs.vision[1][size_t(g.index(36, 10))] == 0);      // over: no explored memory, back to unexplored
    CHECK(gs.vision[1][size_t(g.index(25, 10))] == 2);      // ellipse: 5 cells along x ...
    CHECK(gs.vision[1][size_t(g.index(20, 14))] == 0);      // ... but not 4 along z (4 x 2 > 6)
}

// ---- gameplay hooks of native modules (RtsNative.h): only the declared ones are called
namespace {
class HookProbe final : public game::NativeModule {
public:
    void setup(game::RtsModule& m, Simulation&, game::NativeState& s, const Json& cfg) const override {
        s.values["mask"] = cfg.getInt("hooks", 0);
        s.values["entities"] = int64_t(m.mapData()["entities"].size());   // the setup map is readable
        const_cast<HookProbe*>(this)->mask_ = uint32_t(cfg.getInt("hooks", 0));
    }
    uint32_t hooks() const override { return mask_; }
    void unitSpeed(game::RtsModule&, Simulation& sim, const game::NativeState&, Entity e, Fixed&, Fixed& mul) const override {
        if (sim.world().get<Owner>(e).player == 1) mul = mul / 2;
    }
    bool allowSend(game::RtsModule&, Simulation& sim, const game::NativeState&, Entity, Entity to) const override {
        return sim.world().get<Owner>(to).player != 0;   // neutral buildings take nobody
    }
    bool damage(game::RtsModule&, Simulation& sim, game::NativeState& s, Entity target, Entity, Fixed& dmg) const override {
        if (sim.world().get<Owner>(target).player != 2) return false;
        ++s.values["hits"]; dmg = Fixed{};
        return true;
    }
    void unitDied(game::RtsModule&, Simulation&, game::NativeState& s, Entity, uint8_t, Vec3, bool silent) const override { ++s.values[silent ? "entered" : "died"]; }
    void assault(game::RtsModule&, Simulation&, game::NativeState& s, Entity, uint8_t, uint8_t, int a, int d) const override { s.values["lostA"] += a; s.values["lostD"] += d; }
    bool allowDefeat(game::RtsModule&, Simulation&, const game::NativeState&, uint8_t player) const override { return player != 1; }
    void sceneBodies(game::RtsModule& m, Simulation&, const game::NativeState&, std::vector<Body>& out) const override {
        out.push_back({uint16_t(m.rules().findType("marker")), Vec3{Fixed::fromInt(3), Fixed{}, Fixed::fromInt(3)}, Vec3{Fixed::fromInt(1), Fixed{}, Fixed{}}, true});
    }
private:
    uint32_t mask_ = 0;
};
std::unique_ptr<game::NativeModule> hookProbe() { return std::make_unique<HookProbe>(); }
std::unique_ptr<Simulation> hookSim(int mask) {
    Json rules = Json::parse(kEnvRules);
    rules.set("nativeModules", Json::parse(R"([{"id":"hooks","config":{"hooks":)" + std::to_string(mask) + "}}]"));
    std::vector<game::NativeDefinition> defs{{"hooks", 1, hookProbe}};
    auto sim = std::make_unique<Simulation>(SimConfig{}, std::make_unique<game::RtsModule>(std::move(defs)));
    sim->setup(rules, Json::parse(envMap("null", 40, 20, R"(,{"type":"shroom","owner":1,"pos":[8,5]},{"type":"shroom","owner":2,"pos":[30,5]})")));
    return sim;
}
int64_t hookValue(Simulation& sim, const char* key) {
    auto* st = &sim.world().resource<game::NativeStore>()->modules[0];
    auto it = st->values.find(key);
    return it == st->values.end() ? 0 : it->second;
}
Entity hookUnit(Simulation& sim, uint8_t owner) {
    Entity out;
    sim.world().each<game::Unit, Owner>([&](Entity e, game::Unit& u, Owner& o) { if (!out.valid() && o.player == owner && !mod(sim).rules().types[u.type].structure) out = e; });
    return out;
}
} // namespace

TEST(native_hooks_are_called_only_when_declared) {
    using namespace game;
    auto off = hookSim(0), on = hookSim(kHookSpeed | kHookSend | kHookDamage | kHookDeath | kHookAssault | kHookDefeat | kHookScene);
    CHECK(hookValue(*on, "entities") == 6);
    for (Simulation* s : {off.get(), on.get()}) {
        const bool h = s == on.get();
        RtsModule& m = mod(*s);
        Entity mine = hookUnit(*s, 1), theirs = hookUnit(*s, 2), home = envPoint(*s, 0), enemy = envPoint(*s, 1), north = envPoint(*s, 2);
        CHECK(m.stat(*s, mine, Stat::Speed) == Fixed::fromInt(h ? 1 : 2));          // kHookSpeed
        CHECK(m.stat(*s, theirs, Stat::Speed) == Fixed::fromInt(2));
        CHECK(m.canSend(*s, home, north) == !h && m.canSend(*s, home, enemy));      // kHookSend
        CHECK(m.defeatAllowed(*s, 1) == !h && m.defeatAllowed(*s, 2));              // kHookDefeat
        m.dealDamage(*s, theirs, Fixed::fromInt(4), 0, mine, 1, nullptr);           // kHookDamage
        CHECK(s->world().get<Unit>(theirs).hp == Fixed::fromInt(h ? 10 : 6));
        std::vector<NativeModule::Body> bodies;                                      // kHookScene
        m.native().sceneBodies(m, *s, bodies);
        CHECK(bodies.size() == (h ? 1u : 0u));
        envSendCmd(*s, 1, home, north, 100);                                        // refused by the send filter
        run(*s, 0.5);
        CHECK((s->world().get<Garrison>(home).count >= 40) == h);                   // nobody left the building
        envSendCmd(*s, 1, home, enemy, 50);
        s->world().get<Unit>(mine).hp = Fixed{};
        run(*s, 45);
        CHECK(hookValue(*s, "hits") == (h ? 1 : 0));
        CHECK((hookValue(*s, "died") >= 1) == h);                                    // kHookDeath
        CHECK((hookValue(*s, "lostA") >= 10 && hookValue(*s, "lostD") >= 1) == h);   // kHookAssault: 20 walkers hit the enemy village
    }
    // the hooks are part of the deterministic step: same seed, same checksum
    auto again = hookSim(kHookSpeed | kHookSend | kHookDamage | kHookDeath | kHookAssault | kHookDefeat | kHookScene);
    CHECK(again->checksum() != on->checksum());
    {
        RtsModule& m = mod(*again);
        m.dealDamage(*again, hookUnit(*again, 2), Fixed::fromInt(4), 0, hookUnit(*again, 1), 1, nullptr);
        envSendCmd(*again, 1, envPoint(*again, 0), envPoint(*again, 2), 100);
        run(*again, 0.5);
        envSendCmd(*again, 1, envPoint(*again, 0), envPoint(*again, 1), 50);
        again->world().get<Unit>(hookUnit(*again, 1)).hp = Fixed{};
        run(*again, 45);
    }
    CHECK(again->checksum() == on->checksum());
}
