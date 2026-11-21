// Regression for actual FPS weapon/grenade creation into Projectile entities.
// No genre-specific test-only spawn hook: exercise production input Commands.
void projectileSpawnsCase(bool cow) {
    const auto rules=Json::parse(R"({
        "fps":{"tickRate":20,"mode":"deathmatch",
               "weapons":[{"damage":10,"magazine":12,"intervalTicks":1,
                            "bulletSpeed":200,"bulletGravity":0}],
               "mechanics":{"enabled":true,"grenadeFuse":3,
                            "smokeTicks":10,"flashTicks":5},
               "motor":{"gravity":1}}
    })");
    const auto map=Json::parse(R"({
        "players":[{"team":1},{"team":2}],
        "spawns":[[0,0.01,0],[0,0.01,10]],
        "colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}]
    })");
    auto sim=simulation(rules,map);
    if(cow)sim->world().enableCowSnapshots(true);
    auto ordered=[&]() {
        const auto shots=projectilesOf(*sim);
        CHECK(shots.size()==sim->world().countExisting<fps::Projectile>());
        for(size_t i=1;i<shots.size();++i)CHECK(shots[i-1].id<shots[i].id);
        for(const auto& shot:shots)CHECK(shot.id<state(*sim).nextObject);
    };
    auto tick=[&](uint32_t t,uint32_t buttons,fps::Action action) {
        fps::Input input;
        input.sequence=t+1;
        input.viewTick=t;
        input.buttons=buttons;
        input.action=action;
        auto cmd=fps::command(1,input);
        cmd.tick=t;
        sim->inject(cmd);
        sim->step();
        ordered();
    };
    // First accepted shot is created in the character stage, after the flight
    // stage, so it must not advance until the next tick.
    tick(0,fps::Fire,fps::Action::None);
    CHECK(projectilesOf(*sim).size()==1);
    const uint32_t weaponShotId=projectilesOf(*sim)[0].id;
    CHECK(projectilesOf(*sim)[0].kind==0);
    // The entity component is the only copy: a native write is what the next stage reads.
    auto& live=projectileAt(*sim,0);
    CHECK(live.id==weaponShotId&&live.kind==0);
    live.damage=17;
    CHECK(projectilesOf(*sim)[0].damage==17);
    // All three throwable actions use the same production spawn adapter.
    tick(1,0,fps::Action::Frag);
    tick(2,0,fps::Action::Smoke);
    tick(3,0,fps::Action::Flash);
    bool frag=false,smoke=false,flash=false;
    for(const auto& shot:projectilesOf(*sim)) {
        if(shot.kind==1)frag=true;
        if(shot.kind==2)smoke=true;
        if(shot.kind==3)flash=true;
    }
    CHECK(frag && smoke && flash);
    CHECK(state(*sim).nextObject>weaponShotId+3);
    SimSnapshot checkpoint;
    sim->saveState(checkpoint);
    auto restored=simulation(rules,map);
    CHECK(restored->deserialize(sim->serialize()));
    CHECK(restored->checksum()==sim->checksum());
    std::vector<uint64_t> hashes;
    for(uint32_t t=4;t<14;++t){
        tick(t,t==4?fps::Fire:0,fps::Action::None);
        hashes.push_back(sim->checksum());
    }
    const auto finalChecksum=sim->checksum();
    sim->loadState(checkpoint);
    ordered();
    for(uint32_t t=4;t<14;++t){
        tick(t,t==4?fps::Fire:0,fps::Action::None);
        CHECK(sim->checksum()==hashes[t-4]);
    }
    CHECK(sim->checksum()==finalChecksum);
}
void projectileSpawns() {
    projectileSpawnsCase(false);
    projectileSpawnsCase(true);
}


// Bomb-round cleanup must destroy the projectile entities immediately,
// and a subsequent spawn must not reuse a stale projectile id.
void projectileRoundResetCase(bool cow) {
    const auto rules=Json::parse(R"({
        "fps":{"tickRate":20,"mode":"bomb",
               "weapons":[{"damage":10,"magazine":12,"intervalTicks":1}],
               "mechanics":{"enabled":true,"freezeTicks":1,
                            "grenadeFuse":20,"smokeTicks":10},
               "motor":{"gravity":1}}
    })");
    const auto map=Json::parse(R"({
        "players":[{"team":1},{"team":2}],
        "spawns":[[0,0.01,0],[0,0.01,10]],
        "bombSites":[{"min":[-2,0,-2],"max":[2,3,2]}],
        "colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}]
    })");
    auto sim=simulation(rules,map);
    if(cow)sim->world().enableCowSnapshots(true);
    auto tick=[&](uint32_t t,fps::Action action) {
        fps::Input input;input.sequence=t+1;input.viewTick=t;
        input.action=action;
        auto cmd=fps::command(1,input);cmd.tick=t;
        sim->inject(cmd);
        sim->step();
        CHECK(projectilesOf(*sim).size()==sim->world().countExisting<fps::Projectile>());
    };
    tick(0,fps::Action::None); // freeze phase
    tick(1,fps::Action::Frag); // create a real grenade entity
    CHECK(projectilesOf(*sim).size()==1);
    const uint32_t previousId=projectilesOf(*sim)[0].id;
    const auto lootBefore=lootOf(*sim).size();
    const auto vehiclesBefore=fpsModule(*sim).vehicleEntities(*sim);

    // Force a round conclusion. The normal tickSystems() reset must call
    // clearProjectiles() and destroy the projectile entities while keeping
    // the existing vehicle entities.
    state(*sim).match.end=sim->tick();
    SimSnapshot before;
    sim->saveState(before);
    tick(2,fps::Action::None);
    CHECK(projectilesOf(*sim).empty());
    CHECK(sim->world().countExisting<fps::Projectile>()==0);
    CHECK(lootOf(*sim).size()==lootBefore);
    CHECK(fpsModule(*sim).vehicleEntities(*sim)==vehiclesBefore);
    const uint64_t resetChecksum=sim->checksum();
    auto restored=simulation(rules,map);
    CHECK(restored->deserialize(sim->serialize()));
    CHECK(restored->checksum()==resetChecksum);

    sim->loadState(before);
    tick(2,fps::Action::None);
    CHECK(sim->checksum()==resetChecksum);
    tick(3,fps::Action::Frag); // new object after entity destruction/recycle
    CHECK(projectilesOf(*sim).size()==1);
    CHECK(projectilesOf(*sim)[0].id>previousId);
}
void projectileRoundReset() {
    projectileRoundResetCase(false);
    projectileRoundResetCase(true);
}
