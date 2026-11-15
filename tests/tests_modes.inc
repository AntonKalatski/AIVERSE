// ------------------------------------------------------------------ match modes (RtsModes.cpp)
namespace modes_t {
const char* kTypes = R"("types": {
    "village": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "garrison": {"max": 30, "unit": "shroom", "interval": 100}},
    "tower": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "garrison": {"max": 30, "unit": "shroom", "interval": 100}},
    "shroom": {"move": {"speed": 3}, "entersGarrison": true}})";
std::string rules(const std::string& extra) { return std::string("{") + extra + (extra.empty() ? "" : ",") + kTypes + "}"; }
std::string rows(int w, int h) {
    std::string s;
    for (int y = 0; y < h; ++y) s += std::string(y ? "," : "") + "\"" + std::string(size_t(w), '.') + "\"";
    return s;
}
// players: list of teams; entities: JSON list body
std::string map(const std::vector<int>& teams, const std::string& entities, const std::string& extra = "") {
    std::string pl;
    for (size_t i = 0; i < teams.size(); ++i) pl += std::string(i ? "," : "") + R"({"name":"P)" + std::to_string(i + 1) + R"(","team":)" + std::to_string(teams[i]) + "}";
    return R"({"size":[40,20],"players":[)" + pl + R"(],"terrain":{"rows":[)" + rows(40, 20) + R"(]},"entities":[)" + entities + "]" + extra + "}";
}
std::string ent(const char* type, int owner, int x, int z, const char* extra = "") {
    return std::string(R"({"type":")") + type + R"(","owner":)" + std::to_string(owner) + R"(,"pos":[)" + std::to_string(x) + "," + std::to_string(z) + "],\"garrison\":5" + extra + "}";
}
Entity at(Simulation& sim, int x, int z) {
    Entity found;
    sim.world().each<game::Unit, Transform>([&](Entity e, game::Unit&, Transform& t) {
        if (dist2DTest(t.pos, Vec3{Fixed::fromInt(x), Fixed{}, Fixed::fromInt(z)}) < Fixed::fromInt(2)) found = e;
    });
    return found;
}
bool alive(Simulation& sim, uint8_t p) { auto* pi = sim.player(p); return pi && pi->alive; }
} // namespace modes_t

TEST(modes_domination_ffa_hold_timer) {
    using namespace modes_t;
    auto sim = makeSimJson(rules(R"("modes": {"domination": {"hold": 1.5}})"),
        map({1, 2, 3}, ent("tower", 1, 10, 5, R"(,"modePoint":"domination")") + "," + ent("tower", 1, 20, 5, R"(,"modePoint":"domination")") + "," +
                       ent("village", 2, 30, 15) + "," + ent("village", 3, 5, 15) + "," + ent("village", 1, 15, 15)));
    auto& gs = mod(*sim).state(*sim);
    CHECK(gs.modes.points.size() == 2);
    run(*sim, 1.0);
    CHECK(alive(*sim, 2) && alive(*sim, 3));                  // timers still running (set on the first owner seen)
    run(*sim, 1.0);
    CHECK(!alive(*sim, 2) && !alive(*sim, 3) && alive(*sim, 1));   // FFA: every side holding no point is out
    CHECK(sim->result() == 1 && gs.modes.outcome[2] == 2);
}

TEST(modes_domination_coop_needs_all_points) {
    using namespace modes_t;
    const std::string r = rules(R"("modes": {"domination": {"hold": 1}})");
    // teams 1 (p1, p2) and 2 (p3, p4): team 1 holds both points with two players
    auto sim = makeSimJson(r, map({1, 1, 2, 2}, ent("tower", 1, 10, 5, R"(,"modePoint":"domination")") + "," + ent("tower", 2, 20, 5, R"(,"modePoint":"domination")") + "," +
                                                ent("village", 3, 30, 15) + "," + ent("village", 4, 5, 15)));
    run(*sim, 1.5);
    CHECK(!alive(*sim, 3) && !alive(*sim, 4) && sim->result() == 1);
    // one point each: nobody is out (holdAll)
    auto sim2 = makeSimJson(r, map({1, 1, 2, 2}, ent("tower", 1, 10, 5, R"(,"modePoint":"domination")") + "," + ent("tower", 3, 20, 5, R"(,"modePoint":"domination")") + "," +
                                                 ent("village", 4, 5, 15) + "," + ent("village", 2, 30, 15)));
    run(*sim2, 3);
    CHECK(alive(*sim2, 1) && alive(*sim2, 3) && sim2->result() == 0);
    // a neutral domination point blocks the check
    auto sim3 = makeSimJson(r, map({1, 2}, ent("tower", 1, 10, 5, R"(,"modePoint":"domination")") + "," + ent("tower", 0, 20, 5, R"(,"modePoint":"domination")") + "," +
                                           ent("village", 2, 30, 15)));
    run(*sim3, 3);
    CHECK(alive(*sim3, 2) && sim3->result() == 0);
}

TEST(modes_king_of_the_hill_countdown) {
    using namespace modes_t;
    auto sim = makeSimJson(rules(R"("modes": {"kingOfTheHill": {"score": 4, "rate": 2, "type": "tower", "garrison": 9}})"),
        map({1, 2}, ent("village", 1, 10, 5, R"(,"modePoint":"kingOfTheHill")") + "," + ent("village", 2, 30, 15)));
    auto& m = mod(*sim);
    auto& gs = m.state(*sim);
    Entity hill = at(*sim, 10, 5);
    CHECK(sim->world().get<game::Unit>(hill).type == uint16_t(m.rules().findType("tower")));   // became the mode type
    CHECK(sim->world().get<game::Garrison>(hill).count == 9);
    run(*sim, 1.0);
    CHECK(fxAbs(gs.modes.kohScore[1] - Fixed::fromInt(2)) < Fixed::fromRatio(1, 5) && gs.modes.kohScore[2] == Fixed::fromInt(4));
    CHECK(sim->result() == 0);
    run(*sim, 1.2);
    CHECK(sim->result() == 1 && !alive(*sim, 2) && gs.modes.kohScore[1].raw == 0);
}

TEST(modes_time_limit_speed_steps_and_points) {
    using namespace modes_t;
    const std::string r = rules(R"("modes": {"timeLimit": {"seconds": 3, "speed": [{"remaining": 2, "speed": 1.5}, {"remaining": 1, "speed": 2}]}})");
    auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15)));
    auto& m = mod(*sim);
    CHECK(m.speedScale(*sim) == 1.0);
    run(*sim, 1.1);
    CHECK(m.speedScale(*sim) == 1.5);
    run(*sim, 1.0);
    CHECK(m.speedScale(*sim) == 2.0 && sim->result() == 0);
    run(*sim, 1.0);
    CHECK(sim->result() == 1 && !alive(*sim, 2));             // most points wins when the time is up
    // equal points: a draw
    auto sim2 = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 2, 30, 15)));
    run(*sim2, 3.2);
    CHECK(sim2->result() == -1);
    // points comparison: 2 vs 1 with coefficient 1.5 -> only team 1 exceeds the best (2 * 1.5 > 2, 1 * 1.5 < 2)
    auto sim3 = makeSimJson(rules(R"("modes": {"timeLimit": {"seconds": 1, "win": "compare", "coefficient": 1.5}})"),
                            map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15)));
    run(*sim3, 1.2);
    CHECK(sim3->result() == 1);
    // simulation advance(): the game speed runs more ticks per real second
    auto sim4 = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 2, 30, 15)));
    run(*sim4, 2.05);
    const uint32_t t0 = sim4->tick();
    sim4->advance(0.5, 100);
    CHECK(sim4->tick() - t0 >= 19 && sim4->tick() - t0 <= 20);   // speed 2: 0.5 s real = 1 s of game
}

TEST(modes_sudden_death_phases) {
    using namespace modes_t;
    const std::string r = rules(R"("resources": {"mana": {"start": 0, "regen": 1}},
        "statuses": {"sd_boost": {"incomeMult": {"mana": 2}}},
        "modes": {"suddenDeath": {"at": 1, "preview": 1, "previewSpeed": 0.5, "end": 2, "counter": 0.5, "status": "sd_boost"}})");
    auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15)));
    auto& m = mod(*sim);
    auto& ms = m.state(*sim).modes;
    run(*sim, 0.6);
    CHECK(ms.sdPhase == 0 && ms.sdCounter);
    run(*sim, 0.5);
    CHECK(ms.sdPhase == 1 && m.speedScale(*sim) == 0.5);      // slowed preview
    run(*sim, 0.3);                                           // 0.3 s of game = 0.6 s real: still in the preview
    CHECK(ms.sdPhase == 1);
    run(*sim, 0.15);
    CHECK(ms.sdPhase == 2 && m.speedScale(*sim) == 1.0);
    const int boost = m.rules().findStatus("sd_boost");
    bool has = false;
    for (auto& in : m.state(*sim).players[2].statuses) has = has || in.def == boost;
    CHECK(has);
    const Fixed mana0 = stockOf(*sim, 1, "mana");
    run(*sim, 1.0);
    CHECK(stockOf(*sim, 1, "mana") - mana0 > Fixed::fromRatio(19, 10));   // factor 2 on income
    CHECK(sim->result() == 0);
    run(*sim, 1.1);
    CHECK(ms.sdPhase == 3 && sim->result() == 1 && m.state(*sim).modes.outcome[2] == 2);
}

TEST(modes_sudden_death_rule_sets_and_partial_tie) {
    using namespace modes_t;
    const std::string r = rules(R"("statuses": {"a": {}, "b": {}},
        "modes": {"suddenDeath": {"at": 0.5, "preview": 0, "end": 0.5, "ruleSets": [[{"status": "a", "when": [{"from": 0.2, "to": 0.4}]}], [{"status": "b"}]]}})");
    // three FFA players: 1 and 2 tie on two points, 3 has one -> 1 and 2 win, 3 draws: the match result is a draw
    auto sim = makeSimJson(r, map({1, 2, 3}, ent("village", 1, 5, 5) + "," + ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15) + "," +
                                             ent("village", 2, 30, 5) + "," + ent("village", 3, 20, 10)));
    auto& ms = mod(*sim).state(*sim).modes;
    CHECK(ms.sdRuleSet == 0 || ms.sdRuleSet == 1);
    run(*sim, 1.2);
    CHECK(sim->result() == -1 && ms.outcome[1] == 1 && ms.outcome[2] == 1 && ms.outcome[3] == 3);
}

TEST(modes_zones_shrink_and_neutralize) {
    using namespace modes_t;
    const std::string r = rules(R"("modes": {"zones": {"neutralType": "tower",
        "start": [{"rect": [0, 0, 20, 20], "stay": 1, "move": 2, "next": 0}, {"rect": [20, 0, 20, 20], "stay": 100}],
        "steps": [{"rect": [0, 0, 10, 20], "stay": 100}]}})");
    auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 1, 13, 5) + "," + ent("village", 2, 30, 15)));
    auto& m = mod(*sim);
    game::ModeRect z;
    CHECK(m.modeZone(*sim, 1, z) && z.w == Fixed::fromInt(20));
    CHECK(m.modeZone(*sim, 2, z) && z.x == Fixed::fromInt(20));
    run(*sim, 2.0);                                           // halfway through the move: width 15
    CHECK(m.modeZone(*sim, 1, z) && fxAbs(z.w - Fixed::fromInt(15)) < Fixed::fromRatio(1, 10));
    CHECK(sim->world().get<Owner>(at(*sim, 13, 5)).player == 1);
    run(*sim, 1.2);                                           // the village at x = 13 is outside the zone now
    Entity v = at(*sim, 13, 5);
    CHECK(sim->world().get<Owner>(v).player == 0 && sim->world().get<game::Unit>(v).type == uint16_t(m.rules().findType("tower")));
    CHECK(sim->world().get<Owner>(at(*sim, 5, 5)).player == 1 && sim->world().get<Owner>(at(*sim, 30, 15)).player == 2);
}

TEST(modes_timed_rules_and_levels) {
    using namespace modes_t;
    const std::string r = rules(R"("resources": {"morale": {"start": 0, "min": 0, "levels": [
            {"at": 0}, {"at": 100, "mods": [{"stat": "damage", "mul": 2}], "decay": {"every": 0.5, "amount": 10, "after": 1}}]}},
        "statuses": {"rush": {"mods": [{"stat": "speed", "mul": 2}]}},
        "timedRules": [{"status": "rush", "when": [{"from": 1, "to": 2}]}, {"status": "rush", "resource": "morale", "when": [{"levelBehind": 1}]}])");
    auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 2, 30, 15) +
                                          R"(,{"type":"shroom","owner":1,"pos":[6,9]},{"type":"shroom","owner":2,"pos":[30,9]})"));
    auto& m = mod(*sim);
    auto& gs = m.state(*sim);
    const int rush = m.rules().findStatus("rush");
    auto has = [&](uint8_t p) { for (auto& in : gs.players[p].statuses) if (in.def == rush && in.until > sim->tick()) return true; return false; };
    Entity s1 = at(*sim, 6, 9);
    run(*sim, 0.5);
    CHECK(!has(1) && m.stat(*sim, s1, game::Stat::Damage) == m.stat(*sim, at(*sim, 30, 9), game::Stat::Damage));
    m.addResource(*sim, 1, uint16_t(m.rules().findResource("morale")), Fixed::fromInt(120));
    run(*sim, 0.1);
    CHECK(m.modeLevel(*sim, 1, 0) == 1 && m.stat(*sim, s1, game::Stat::Damage) == m.stat(*sim, at(*sim, 30, 9), game::Stat::Damage) * Fixed::fromInt(2));
    CHECK(has(2) && !has(1));                                  // player 2 is one level behind
    run(*sim, 0.8);
    CHECK(has(1));                                            // time slot [1, 2]
    CHECK(stockOf(*sim, 1, "morale") == Fixed::fromInt(120)); // quiet for less than `after`
    run(*sim, 1.0);
    CHECK(stockOf(*sim, 1, "morale") < Fixed::fromInt(120));  // decays after the quiet period
    run(*sim, 1.5);
    CHECK(!has(1) && m.modeLevel(*sim, 1, 0) == 0 && !has(2));   // out of the slot, back on level 0, nobody behind
    CHECK(m.matchStat(*sim, 1, game::kMsLevelUps) == 1 && m.matchStat(*sim, 1, game::kMsLevelDowns) == 1 && m.matchStat(*sim, 1, game::kMsMaxLevel) == 1);
}

TEST(modes_presets_stats_save_and_errors) {
    using namespace modes_t;
    const std::string r = rules(R"("lanes": {"left": 2, "right": 1, "spacing": 0.4}, "matchModes": {"koh": {"modes": {"kingOfTheHill": {"score": 30}}, "victory": "noEntities"}, "plain": {}})");
    auto sim = makeSimJson(r, map({1, 2}, ent("village", 1, 10, 5, R"(,"modePoint":"kingOfTheHill")") + "," + ent("village", 2, 30, 15) + "," +
                                          std::string(R"({"type":"village","owner":0,"pos":[20,5],"garrison":0})"), R"(,"matchMode":"koh")"));
    auto& m = mod(*sim);
    CHECK(m.modes().koh && m.modes().stats && m.state(*sim).victory.mode == game::VictoryMode::NoEntities);
    auto simB = makeSimJson(r, map({1, 2}, ent("village", 1, 10, 5) + "," + ent("village", 2, 30, 15), R"(,"matchMode":"plain")"));
    CHECK(!mod(*simB).modes().koh);
    // statistics: a capture of a neutral village
    Command c; c.player = 1; c.type = game::kCmdSend; c.units = {at(*sim, 10, 5)}; c.target = at(*sim, 20, 5); c.param = 100;
    sim->submit(c);
    run(*sim, 1.0);
    // save / load mid-match: the copy goes on identically
    const std::string save = sim->serialize();
    auto copy = makeSimJson(r, map({1, 2}, ent("village", 1, 10, 5, R"(,"modePoint":"kingOfTheHill")") + "," + ent("village", 2, 30, 15) + "," +
                                           std::string(R"({"type":"village","owner":0,"pos":[20,5],"garrison":0})"), R"(,"matchMode":"koh")"));
    CHECK(copy->deserialize(save));
    CHECK(copy->checksum() == sim->checksum());
    run(*sim, 4.0); run(*copy, 4.0);
    CHECK(copy->checksum() == sim->checksum());
    CHECK(m.matchStat(*sim, 1, game::kMsCapturedNeutral) == 1 && m.matchStat(*sim, 1, game::kMsPoints) == 2);
    CHECK(sim->result() == 0 && m.state(*sim).modes.kohScore[1] < Fixed::fromInt(26));
    // readable errors
    auto err = [&](const std::string& extra) {
        try { makeSimJson(rules(extra), map({1, 2}, ent("village", 1, 5, 5))); } catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    CHECK(err(R"("modes": {"suddenDeath": {"status": "nope"}})").find("unknown status 'nope'") != std::string::npos);
    CHECK(err(R"("modes": {"zones": {"start": []}})").find("modes.zones") != std::string::npos);
    CHECK(err(R"("timedRules": [{"status": "x"}])").find("timedRules[0]") != std::string::npos);
    CHECK(err(R"("modes": {"domination": {"rule": "x"}})").find("rule must be") != std::string::npos);
}

TEST(modes_deterministic_match) {
    using namespace modes_t;
    const std::string r = rules(R"("modes": {"kingOfTheHill": {"score": 30}, "timeLimit": {"seconds": 20, "speed": [{"remaining": 10, "speed": 2}]},
        "suddenDeath": {"at": 5, "preview": 1, "end": 30}, "zones": {"start": [{"rect": [0, 0, 40, 20], "stay": 3, "move": 4, "next": 0}], "steps": [{"rect": [5, 2, 30, 16], "stay": 99}]}})");
    const std::string mp = map({1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 2, 34, 15) + "," + ent("village", 0, 20, 10, R"(,"modePoint":"kingOfTheHill")"));
    auto a = makeSimJson(r, mp, 7), b = makeSimJson(r, mp, 7);
    for (int t = 0; t < 20 * 25; ++t) {
        if (t % 40 == 3)
            for (auto* s : {a.get(), b.get()}) {
                Command c; c.player = uint8_t(1 + (t / 40) % 2); c.type = game::kCmdSend;
                c.units = {at(*s, c.player == 1 ? 5 : 34, c.player == 1 ? 5 : 15)}; c.target = at(*s, 20, 10); c.param = 60;
                s->submit(c);
            }
        a->step(); b->step();
    }
    CHECK(a->checksum() == b->checksum() && a->result() != 0);
}

TEST(modes_king_of_the_hill_trigger_vars) {
    using namespace modes_t;
    auto sim = makeSimJson(rules(R"("modes": {"kingOfTheHill": {"score": 10}})"),
        map({1, 2}, ent("village", 1, 10, 5, R"(,"modePoint":"kingOfTheHill")") + "," + ent("village", 2, 30, 15)));
    run(*sim, 1.0);
    auto& vars = mod(*sim).state(*sim).trig.vars;
    CHECK(vars.count("koth_1") && vars.count("koth_2") && vars.count("koth_max"));
    CHECK(vars["koth_2"] == Fixed::fromInt(10) && vars["koth_1"] < Fixed::fromInt(10) && vars["koth_max"] == Fixed::fromInt(10));
}

TEST(modes_team_conquest_rule_sets_and_hud_vars) {
    using namespace modes_t;
    // team conquest: player 2 has nothing, but its teammate 1 still has a village -> both stay
    const std::string r = rules(R"("victory": "noEntities", "statuses": {"rush": {}}, "ruleSets": {"league3": [{"status": "rush"}]},
        "modes": {"conquest": {"team": true}, "timeLimit": {"seconds": 50}, "suddenDeath": {"at": 10, "preview": 2, "end": 5}})");
    auto sim = makeSimJson(r, map({1, 1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 3, 30, 15), R"(,"ruleSets":["league3"])"));
    run(*sim, 1.0);
    CHECK(alive(*sim, 1) && alive(*sim, 2) && alive(*sim, 3));
    auto& m = mod(*sim);
    const int rush = m.rules().findStatus("rush");
    bool has = false;
    for (auto& in : m.state(*sim).players[3].statuses) has = has || in.def == rush;
    CHECK(has);
    auto& V = m.state(*sim).trig.vars;
    CHECK(fxAbs(V["mode_time_left"] - Fixed::fromInt(49)) < Fixed::fromRatio(1, 10));
    CHECK(V["mode_sd_phase"].raw == 0 && fxAbs(V["mode_sd_left"] - Fixed::fromInt(9)) < Fixed::fromRatio(1, 10));
    // without team conquest player 2 is out
    auto sim2 = makeSimJson(rules(R"("victory": "noEntities")"), map({1, 1, 2}, ent("village", 1, 5, 5) + "," + ent("village", 3, 30, 15)));
    run(*sim2, 1.0);
    CHECK(alive(*sim2, 1) && !alive(*sim2, 2));
    // unknown rule set: a readable error
    bool threw = false;
    try { makeSimJson(r, map({1, 2}, ent("village", 1, 5, 5), R"(,"ruleSets":["nope"])")); } catch (const std::exception& e) { threw = std::string(e.what()).find("nope") != std::string::npos; }
    CHECK(threw);
}

// ------------------------------------------------------------------ native module hooks (RtsNative.h NativeHook)
namespace hooks_t {
// A synthetic module: sends only from "village" types, neutral garrisons grow at half rate up to 7, player 2 grows
// at double rate, upgrades into "tower" cost nothing and take twice as long, upgrades of player 2 are refused.
class HookProbe final : public game::NativeModule {
public:
    void setup(game::RtsModule& g, Simulation&, game::NativeState& s, const Json&) const override {
        s.values["mapKey"] = g.matchMap().getInt("probeKey", -1);
    }
    uint32_t hooks() const override { return game::kNativeHookCommand | game::kNativeHookGarrison | game::kNativeHookUpgrade; }
    bool filterCommand(game::RtsModule& g, Simulation& sim, game::NativeState& s, Command& c) const override {
        if (c.type != game::kCmdSend || c.units.empty() || !sim.world().alive(c.units[0])) return true;
        ++s.values["sends"];
        if (g.rules().types[sim.world().get<game::Unit>(c.units[0]).type].id != "village") return false;
        if (c.param == 100) c.param = -2;   // changed: exactly two units
        return true;
    }
    void garrisonGrowth(game::RtsModule&, Simulation&, game::NativeState&, Entity, uint8_t owner, Fixed& rate, int& cap) const override {
        if (owner == 0) { rate = Fixed::fromRatio(1, 2); cap = 7; }
        else if (owner == 2) rate = rate * Fixed::fromInt(2);
    }
    bool upgradeOrder(game::RtsModule& g, Simulation&, game::NativeState&, Entity, uint8_t player, uint16_t& type, Fixed& cost, Fixed& time) const override {
        if (player == 2) return false;
        if (g.rules().types[type].id == "tower") { cost = Fixed{}; time = Fixed::fromInt(2); }
        return true;
    }
    void tick(game::RtsModule&, Simulation& sim, game::NativeState& s) const override {
        for (auto& ev : sim.events()) if (ev.type == game::kEvAssault) { ++s.values["assaults"]; s.values["defendersLost"] += ev.value; }
    }
};
std::unique_ptr<game::NativeModule> hookProbe() { return std::make_unique<HookProbe>(); }
std::unique_ptr<Simulation> sim(const std::string& entities, bool withModule = true) {
    const std::string r = std::string(R"({"lanes": {"left": 0, "right": 0}, "resources": {"mana": {"start": 0}},
      "playerAbilities": [{"id": "gift", "target": "structure", "cost": {"mana": 50}, "cooldown": 30, "effects": [{"type": "garrisonAdd", "amount": 5, "affects": "own"}]}],)") +
      (withModule ? R"("nativeModules": [{"id": "hookprobe"}],)" : "") + R"("types": {
        "village": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "garrison": {"max": 30, "unit": "shroom", "interval": 1}, "upgrades": ["tower"]},
        "tower": {"structure": {"footprint": [2, 2]}, "invulnerable": true, "garrison": {"max": 30, "unit": "shroom", "interval": 1}, "cost": {"garrison": 4, "time": 2}},
        "shroom": {"move": {"speed": 3}, "entersGarrison": true}}})";
    std::vector<game::NativeDefinition> defs{{"hookprobe", 1, hookProbe}};
    auto s = std::make_unique<Simulation>(SimConfig{}, std::make_unique<game::RtsModule>(std::move(defs)));
    s->setup(Json::parse(r), Json::parse(modes_t::map({1, 2}, entities, R"(,"probeKey":42)")));
    return s;
}
// the garrisoned building at (x, z): modes_t::at may return a unit that just walked out of it (no Garrison;
// reading one through get<> was undefined and differed between platforms)
int garrison(Simulation& s, int x, int z) {
    int count = -1;
    s.world().each<game::Garrison, Transform>([&](Entity, game::Garrison& g, Transform& t) {
        if (dist2DTest(t.pos, Vec3{Fixed::fromInt(x), Fixed{}, Fixed::fromInt(z)}) < Fixed::fromInt(2)) count = g.count;
    });
    return count;
}
} // namespace hooks_t

TEST(native_hooks_command_garrison_upgrade_cast) {
    using namespace modes_t;
    const std::string ents = std::string(R"({"type":"village","owner":1,"pos":[5,5],"garrison":10},{"type":"tower","owner":1,"pos":[5,12],"garrison":10},)") +
        R"({"type":"village","owner":0,"pos":[14,5],"garrison":3},{"type":"village","owner":2,"pos":[30,15],"garrison":10})";
    auto sim = hooks_t::sim(ents);
    auto& m = mod(*sim);
    auto& st = sim->world().resource<game::NativeStore>()->modules[0];
    CHECK(st.values.at("mapKey") == 42);                                  // matchMap(): the module's own map key
    // garrisonGrowth: 4 s -> player 1 +4, player 2 +8 (rate 2), neutral +2 (rate 0.5), and the neutral cap 7
    run(*sim, 4.05);
    CHECK(hooks_t::garrison(*sim, 5, 5) == 14 && hooks_t::garrison(*sim, 30, 15) == 18 && hooks_t::garrison(*sim, 14, 5) == 5);
    run(*sim, 8);
    CHECK(hooks_t::garrison(*sim, 14, 5) == 7);
    // filterCommand: a send from the tower is dropped; the village's 100 % becomes exactly two units
    Command c; c.player = 1; c.type = game::kCmdSend; c.units = {at(*sim, 5, 12)}; c.target = at(*sim, 14, 5); c.param = 100;
    const int tower0 = hooks_t::garrison(*sim, 5, 12);
    sim->submit(c);
    run(*sim, 0.2);
    CHECK(hooks_t::garrison(*sim, 5, 12) >= tower0 && st.values.at("sends") == 1);
    const int village0 = hooks_t::garrison(*sim, 5, 5);
    c.units = {at(*sim, 5, 5)};
    sim->submit(c);
    run(*sim, 0.2);
    CHECK(hooks_t::garrison(*sim, 5, 5) == village0 - 2);
    run(*sim, 6);
    CHECK(st.values.at("assaults") == 2 && st.values.at("defendersLost") == 2);   // kEvAssault per unit
    // upgradeOrder: free for player 1 (garrison cost 4 x 0), twice the time; refused for player 2
    const int before = hooks_t::garrison(*sim, 5, 5);
    Command u; u.player = 1; u.type = game::kCmdUpgrade; u.units = {at(*sim, 5, 5)}; u.param = m.rules().findType("tower");
    sim->submit(u);
    Command u2 = u; u2.player = 2; u2.units = {at(*sim, 30, 15)};
    sim->submit(u2);
    run(*sim, 0.2);
    CHECK(hooks_t::garrison(*sim, 5, 5) >= before);                       // nothing paid
    run(*sim, 3);
    CHECK(sim->world().get<game::Unit>(at(*sim, 5, 5)).type != uint16_t(m.rules().findType("tower")));   // 2 s x 2 not over yet
    run(*sim, 1.5);
    CHECK(sim->world().get<game::Unit>(at(*sim, 5, 5)).type == uint16_t(m.rules().findType("tower")));
    CHECK(sim->world().get<game::Unit>(at(*sim, 30, 15)).type == uint16_t(m.rules().findType("village")));
    // castPlayerAbilityFree: the payload without cost or cooldown
    const Entity own = at(*sim, 5, 12);
    const int g0 = hooks_t::garrison(*sim, 5, 12);
    const Vec3 op = sim->world().get<Transform>(own).pos;
    CHECK(m.castPlayerAbilityFree(*sim, 1, m.rules().findPlayerAbility("gift"), op, own));
    CHECK(m.castPlayerAbilityFree(*sim, 1, m.rules().findPlayerAbility("gift"), op, own));   // no cooldown, no mana needed
    CHECK(hooks_t::garrison(*sim, 5, 12) == g0 + 10 && stockOf(*sim, 1, "mana").raw == 0 && !m.castPlayerAbilityFree(*sim, 1, 99, op, own));
    // without the module nothing changes: neutral garrisons do not grow
    auto plain = hooks_t::sim(ents, false);
    run(*plain, 4.05);
    CHECK(hooks_t::garrison(*plain, 14, 5) == 3 && hooks_t::garrison(*plain, 30, 15) == 14);
    // deterministic with the hooks: two runs, same checksum
    auto a = hooks_t::sim(ents), b = hooks_t::sim(ents);
    for (int i = 0; i < 200; ++i) { a->step(); b->step(); }
    CHECK(a->checksum() == b->checksum());
}
