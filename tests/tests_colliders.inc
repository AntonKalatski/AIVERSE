// Static colliders (type "collider": true, RtsColliders.h): map entities that are circles in a grid, not units.
// Walkers go round them (ORCA static obstacles) and never step into one, heroes and cars are stopped by them,
// saves keep working.

namespace {
const char* kColliderRules = R"J({
  "victory": "noUnits",
  "resources": {"gold": {"start": 0}},
  "types": {
    "walker": {"health": {"max": 50}, "move": {"speed": 2, "radius": 0.3}, "sight": 4},
    "post":   {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.4, "mass": 10000}, "collider": true},
    "rock":   {"health": {"max": 1000}, "move": {"speed": 0, "radius": 0.4, "mass": 10000}, "sight": 1}
  }
})J";
const char* kColliderDriving = R"J({
  "victory": "noUnits",
  "resources": {"gold": {"start": 0}},
  "topDown": {"heroes": ["guy"], "move": {"accel": 40, "decel": 60}, "autoFire": false},
  "driving": {
    "metersPerCell": 4, "enterRange": 1, "exitSpeed": 3, "controls": "tank",
    "crash": {"minSpeed": 4, "damage": 5, "restitution": 0.2},
    "vehicles": {"sedan": {"length": 1.1, "width": 0.5, "wheeled": {"massMilli": 1200000}}}
  },
  "types": {
    "guy":   {"health": {"max": 100}, "move": {"speed": 4, "radius": 0.3}, "sight": 10},
    "sedan": {"health": {"max": 400}, "move": {"speed": 0, "radius": 0.5}, "sight": 2},
    "post":  {"health": {"max": 1000}, "invulnerable": true, "move": {"speed": 0, "radius": 0.4, "mass": 10000}, "collider": true},
    "dummy": {"health": {"max": 100000}, "move": {"speed": 0, "radius": 0.4}, "sight": 2}
  }
})J";
int unitsOfType(Simulation& sim, const char* id) {
    int n = 0;
    auto& types = mod(sim).rules().types;
    sim.world().each<game::Unit>([&](Entity, game::Unit& u) { if (types[u.type].id == id) ++n; });
    return n;
}
} // namespace

TEST(colliders_are_static_circles_not_units) {
    auto sim = makeSimJson(kColliderRules, topDownMap(R"({"type":"walker","owner":1,"pos":[2.5,8.5]},{"type":"post","owner":0,"pos":[6.5,8.5]},
        {"type":"post","owner":0,"pos":[6.5,3.5]},{"type":"rock","owner":0,"pos":[20.5,3.5]})", false));
    const auto& c = mod(*sim).state(*sim).colliders;
    CHECK(unitsOfType(*sim, "post") == 0 && unitsOfType(*sim, "rock") == 1 && unitsOfType(*sim, "walker") == 1);
    const Vec2 first{Fixed::fromRatio(13, 2), Fixed::fromRatio(17, 2)};
    CHECK(c.items.size() == 2 && c.items[0].p == first && c.items[0].r == Fixed::fromRatio(2, 5));
    int n = 0;
    c.query({Fixed::fromInt(6), Fixed::fromInt(8)}, Fixed::fromInt(1), game::kLayerGround, [&](size_t i, const auto&) { CHECK(i == 0); ++n; });
    CHECK(n == 1);
    // a collider needs a radius and no structure block
    bool threw = false;
    try { makeSimJson(R"({"types": {"u": {"health": {"max": 10}, "structure": {"footprint": [1, 1]}, "collider": true}}})", topDownMap("", false)); }
    catch (const std::exception& e) { threw = std::string(e.what()).find("collider") != std::string::npos; }
    CHECK(threw);
}

TEST(colliders_walkers_go_round_and_never_step_in) {
    auto sim = makeSimJson(kColliderRules, topDownMap(R"({"type":"walker","owner":1,"pos":[2.5,8.5]},{"type":"post","owner":0,"pos":[6.5,8.5]},
        {"type":"post","owner":0,"pos":[9.5,8.6]})", false));
    Entity w = findType(*sim, "walker", 1);
    Command c; c.player = 1; c.type = game::kCmdMove; c.units = {w}; c.point = {Fixed::fromRatio(29, 2), Fixed{}, Fixed::fromRatio(17, 2)};
    sim->submit(c);
    const auto& cs = mod(*sim).state(*sim).colliders;
    Fixed closest = Fixed::fromInt(100);
    for (int t = 0; t < 20 * 12; ++t) {
        sim->step();
        const Vec2 p = sim->world().get<Transform>(w).pos.xz();
        for (const auto& it : cs.items) closest = fxMin(closest, (p - it.p).length() - it.r - Fixed::fromRatio(3, 10));
    }
    const Vec3 end = sim->world().get<Transform>(w).pos;
    CHECK(closest > -Fixed::fromRatio(1, 50));   // never inside a post (a hair of the separation dead zone)
    const Vec2 goal{Fixed::fromRatio(29, 2), Fixed::fromRatio(17, 2)};
    CHECK((end.xz() - goal).length() < Fixed::fromInt(1));   // arrived (a move order stops within its arrival radius)
}

TEST(colliders_stop_heroes_and_cars) {
    // the hero on foot walks into a post and stays outside it
    auto sim = makeSimJson(kColliderDriving, topDownMap(R"({"type":"guy","owner":1,"pos":[4.5,7.4]},{"type":"post","owner":0,"pos":[7.5,7.4]})", false));
    Entity hero = mod(*sim).topDown()->heroOf(*sim, 1);
    steer(*sim, 1, kEast);
    run(*sim, 2.0);
    CHECK(tdPos(*sim, hero).x < Fixed::fromRatio(75, 10) - Fixed::fromRatio(69, 100) && tdPos(*sim, hero).x > Fixed::fromInt(6));
    // a car driven into one stops against it, as against any body that never moves
    auto car = makeSimJson(kColliderDriving, topDownMap(R"({"type":"guy","owner":1,"pos":[4.5,7.4]},{"type":"sedan","owner":0,"pos":[4.5,8.5]},
        {"type":"post","owner":0,"pos":[10.5,8.5]})", false));
    Entity sedan = firstOf(*car, "sedan");
    enterKey(*car, 1);
    run(*car, 0.3);
    steer(*car, 1, kNorth);
    run(*car, 6.0);
    CHECK(tdPos(*car, sedan).x < Fixed::fromRatio(105, 10) - Fixed::fromRatio(9, 10) && tdPos(*car, sedan).x > Fixed::fromInt(8));
    CHECK(car->world().get<game::Unit>(sedan).hp < Fixed::fromInt(400));
}

TEST(colliders_save_and_load) {
    auto sim = makeSimJson(kColliderRules, topDownMap(R"({"type":"walker","owner":1,"pos":[2.5,8.5]},{"type":"post","owner":0,"pos":[6.5,8.5]})", false));
    Entity w = findType(*sim, "walker", 1);
    Command c; c.player = 1; c.type = game::kCmdMove; c.units = {w}; c.point = {Fixed::fromRatio(29, 2), Fixed{}, Fixed::fromRatio(17, 2)};
    sim->submit(c);
    run(*sim, 1.5);
    const std::string save = sim->serialize();
    auto copy = makeSimJson(kColliderRules, topDownMap(R"({"type":"walker","owner":1,"pos":[2.5,8.5]},{"type":"post","owner":0,"pos":[6.5,8.5]})", false));
    std::string err;
    CHECK(copy->deserialize(save, &err) && copy->checksum() == sim->checksum());
    CHECK(mod(*copy).state(*copy).colliders.items.size() == 1);
    run(*sim, 3.0);
    run(*copy, 3.0);
    CHECK(copy->checksum() == sim->checksum());
}
