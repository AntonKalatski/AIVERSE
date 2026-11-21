void projectilePrograms() {
    auto base=Json::parse(R"({"fps":{"tickRate":20,"mode":"deathmatch","weapon":{"damage":10,"intervalTicks":1000,"bulletSpeed":200,"bulletGravity":0,"flight":{}},"motor":{"gravity":1}}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2},{"team":2},{"team":2}],"spawns":[[0,0.01,0],[0,0.01,4],[0,0.01,8],[0,0.01,12]],"colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}]})");
    auto make=[&](const char* flight,Json m){auto r=base;r.member("fps").member("weapon").set("flight",Json::parse(flight));return simulation(r,m);};
    auto fire=[](Simulation& sim){fps::Input in;in.buttons=fps::Fire;in.sequence=1;auto cmd=fps::command(1,in);cmd.tick=sim.tick();sim.inject(cmd);sim.step();};
    auto step=[](Simulation& sim,int n){while(n--)sim.step();};
    auto sim=make(R"({"modifiers":[{"kind":"flyThrough","count":2,"falloff":0.5}]})",map);
    fire(*sim);step(*sim,4);CHECK(character(*sim,2).health==90);CHECK(character(*sim,3).health==95);CHECK(character(*sim,4).health==98);
    CHECK(character(*sim,1).ammo==29);CHECK(projectilesOf(*sim).empty());
    sim=make(R"({"modifiers":[{"kind":"split","count":2,"angle":0},{"kind":"damage","factor":0.5},{"kind":"flyThrough","count":1}]})",map);
    fire(*sim);step(*sim,8);CHECK(character(*sim,2).health==90);CHECK(character(*sim,3).health==90);CHECK(character(*sim,4).health==90);
    auto corner=map;corner.set("spawns",Json::parse("[[0,0.01,0],[0,0.01,4],[3,0.01,4],[6,0.01,4]]"));
    sim=make(R"({"gravity":2,"modifiers":[{"kind":"ricochet","count":2,"radius":5},{"kind":"damage","factor":0.5}]})",corner);
    fire(*sim);step(*sim,1);CHECK(!projectilesOf(*sim).empty());CHECK(projectilesOf(*sim).front().program.verticalSpeed<Fixed{});step(*sim,7);CHECK(character(*sim,2).health==90);CHECK(character(*sim,3).health==95);CHECK(character(*sim,4).health==95);
    auto cover=map;cover.member("colliders").push(Json::parse(R"({"id":2,"min":[-1,0,6],"max":[1,5,6.01]})"));
    sim=make(R"({"modifiers":[{"kind":"flyThrough","count":3}]})",cover);fire(*sim);step(*sim,4);
    CHECK(character(*sim,2).health==90);CHECK(character(*sim,3).health==100);
    // Ballistic branches retain vertical velocity; state restores after a timed split.
    auto ballistic=base;ballistic.member("fps").member("weapon").set("bulletSpeed",Json::number(int64_t(10)));
    ballistic.member("fps").member("weapon").set("flight",Json::parse(R"({"gravity":4,"verticalSpeed":4,"splitBudget":2,"modifiers":[{"kind":"split","trigger":"time","after":0.1,"angle":45},{"kind":"flyThrough","count":2}]})"));
    sim=simulation(ballistic,map);fire(*sim);step(*sim,3);CHECK(projectilesOf(*sim).size()==3);
    auto saved=sim->serialize();auto copy=simulation(ballistic,map);CHECK(copy->deserialize(saved));CHECK(copy->checksum()==sim->checksum());
    for(const auto& p:projectilesOf(*sim)){CHECK(p.pos.y>1_fx);CHECK(p.program.verticalSpeed>3_fx);CHECK(p.program.splitBudget==0);}
    net::authority::Snapshot active;active.localSlot=1;active.characters=fpsModule(*sim).snapshots(*sim);active.projectiles=projectilesOf(*sim);
    net::authority::Snapshot view;CHECK(net::authority::decodeSnapshot(net::authority::encodeSnapshot(active),view));
    CHECK(view.projectiles.size()==3);CHECK(view.projectiles.front().pos==active.projectiles.front().pos);CHECK(!view.projectiles.front().program.enabled);
    for(int i=0;i<80;++i){sim->step();copy->step();CHECK(sim->checksum()==copy->checksum());}
    // Fully vertical swept shot hits an actor above, never a target merely aligned in XZ below.
    sim=make("{}",map);fire(*sim);auto& shot=projectileAt(*sim,0);shot.pos={0_fx,5_fx,4_fx};shot.program.direction={0_fx,1_fx,0_fx};
    character(*sim,3).pos={0_fx,8_fx,4_fx};step(*sim,1);
    CHECK(character(*sim,2).health==100);CHECK(character(*sim,3).health==90);
    // Network presentation keeps the existing wire format; private history is server-only.
    net::authority::Snapshot snapshot;snapshot.localSlot=1;snapshot.characters=fpsModule(*sim).snapshots(*sim);snapshot.projectiles=projectilesOf(*sim);
    net::authority::Snapshot decoded;CHECK(net::authority::decodeSnapshot(net::authority::encodeSnapshot(snapshot),decoded));
    bool rejected=false;try{auto bad=base;bad.member("fps").member("weapon").set("bulletSpeed",Json::number(int64_t(0)));fps::readConfig(bad);}catch(const std::exception&){rejected=true;}CHECK(rejected);
}


void projectileProgramStageCase(bool cow) {
    // Production FPS programmed bullets are a separate ordered Simulation
    // stage (fps.projectilePrograms) over the Projectile entities: hits,
    // save, rollback replay and a native component write.
    const auto rules=Json::parse(R"({
        "fps":{"tickRate":20,"mode":"deathmatch",
               "weapon":{"damage":10,"intervalTicks":1000,
                         "bulletSpeed":200,"bulletGravity":0,
                         "flight":{"modifiers":[
                             {"kind":"flyThrough","count":2,"falloff":0.5}
                         ]}},
               "motor":{"gravity":1}}
    })");
    const auto map=Json::parse(R"({
        "players":[{"team":1},{"team":2},{"team":2},{"team":2}],
        "spawns":[[0,0.01,0],[0,0.01,4],[0,0.01,8],[0,0.01,12]],
        "colliders":[{"id":1,"min":[-100,-2,-100],
                                 "max":[100,0,100]}]
    })");
    auto sim=simulation(rules,map);
    if(cow)sim->world().enableCowSnapshots(true);
    auto input=fps::Input{};
    input.buttons=fps::Fire;
    input.sequence=1;
    input.viewTick=0;
    auto command=fps::command(1,input);
    command.tick=0;
    sim->inject(command);
    sim->step(); // shot created after projectile program phase
    CHECK(projectilesOf(*sim).size()==1);
    CHECK(sim->world().countExisting<fps::Projectile>()==1);
    SimSnapshot beforeProgram;
    sim->saveState(beforeProgram);
    std::vector<uint64_t> hashes;
    for(int i=0;i<12;++i){sim->step();hashes.push_back(sim->checksum());}
    CHECK(projectilesOf(*sim).empty());
    CHECK(character(*sim,2).health==90);
    CHECK(character(*sim,3).health==95);
    CHECK(character(*sim,4).health==98);
    const uint64_t expected=sim->checksum();
    auto restored=simulation(rules,map);
    CHECK(restored->deserialize(sim->serialize()));
    CHECK(restored->checksum()==expected);
    // SaveState must also rewind the pending projectile and repeat every hit.
    sim->loadState(beforeProgram);
    CHECK(projectilesOf(*sim).size()==1);
    for(int i=0;i<12;++i){sim->step();CHECK(sim->checksum()==hashes[size_t(i)]);}
    CHECK(sim->checksum()==expected);
    // The Projectile component is the state: a native write changes the
    // following stage (deep and COW snapshots alike).
    sim->loadState(beforeProgram);
    CHECK(projectilesOf(*sim).size()==1);
    projectileAt(*sim,0).damage=12;
    for(int i=0;i<12;++i)sim->step();
    CHECK(character(*sim,2).health==88);
}

void projectileProgramStage() {
    projectileProgramStageCase(false); // deep snapshots
    projectileProgramStageCase(true);  // COW snapshots
}


void projectileBranchesCase(bool cow) {
    // Branching shots may retire a source and create multiple children in one
    // tick. Verify entity recycling, stable projectile order and saves.
    auto rules=Json::parse(R"({
        "fps":{"tickRate":20,"mode":"deathmatch",
               "weapon":{"damage":10,"intervalTicks":1000,
                         "bulletSpeed":200,"bulletGravity":0,
                         "flight":{"modifiers":[
                             {"kind":"split","count":2,"angle":0},
                             {"kind":"damage","factor":0.5},
                             {"kind":"flyThrough","count":1}
                         ]}},
               "motor":{"gravity":1}}
    })");
    auto map=Json::parse(R"({
        "players":[{"team":1},{"team":2},{"team":2},{"team":2}],
        "spawns":[[0,0.01,0],[0,0.01,4],[0,0.01,8],[0,0.01,12]],
        "colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}]
    })");
    auto sim=simulation(rules,map);
    if(cow)sim->world().enableCowSnapshots(true);
    fps::Input input;input.buttons=fps::Fire;input.sequence=1;
    // Insert a nonprogrammed cloud entity before the first shot: mixed
    // passive / programmed projectiles in one id-ordered pool.
    fps::Projectile passive;
    passive.owner=1;passive.kind=4;
    passive.expires=1000;passive.pos={10_fx,5_fx,10_fx};
    const Entity passiveEntity=spawnObject(*sim,passive);
    const uint32_t passiveId=sim->world().tryGetConst<fps::Projectile>(passiveEntity)->id;

    const auto command=fps::command(1,input);
    sim->inject(command);
    auto ordered=[&]() {
        const auto shots=projectilesOf(*sim);
        CHECK(shots.size()==size_t(sim->world().countExisting<fps::Projectile>()));
        for(size_t i=1;i<shots.size();++i)CHECK(shots[i-1].id<shots[i].id);
        for(const auto& shot:shots)CHECK(shot.id<state(*sim).nextObject);
    };
    sim->step();
    ordered();
    SimSnapshot pre;
    sim->saveState(pre);
    const uint32_t beforeBranchAllocation=state(*sim).nextObject;
    uint32_t maxAllocated=beforeBranchAllocation;
    std::vector<uint64_t> hashes;
    for(int tick=0;tick<8;++tick) {
        sim->step();ordered();hashes.push_back(sim->checksum());
        maxAllocated=std::max(maxAllocated,state(*sim).nextObject);
    }
    CHECK(maxAllocated>beforeBranchAllocation); // branch child IDs were allocated
    CHECK(projectilesOf(*sim).size()==1);
    CHECK(projectilesOf(*sim).front().id==passiveId);
    CHECK(sim->world().alive(passiveEntity));
    for(uint8_t slot=2;slot<=4;++slot)
        CHECK(character(*sim,slot).health==90);
    const auto checksum=sim->checksum();
    auto restored=simulation(rules,map);
    CHECK(restored->deserialize(sim->serialize()));
    CHECK(restored->checksum()==checksum);
    // Replay branch allocation with identical order.
    sim->loadState(pre);
    ordered();
    for(int tick=0;tick<8;++tick) {
        sim->step();ordered();CHECK(sim->checksum()==hashes[size_t(tick)]);
    }
    CHECK(sim->checksum()==checksum);
}
void projectileBranches() {
    projectileBranchesCase(false);
    projectileBranchesCase(true);
}


void ordinaryProjectilesCase(bool cow) {
    // Bullets, timed smoke and bouncing grenades are processed in the
    // separate fps.ordinaryProjectiles stage over natively created entities.
    const auto rules=Json::parse(R"({
        "fps":{"tickRate":20,"mode":"deathmatch",
               "weapon":{"damage":10,"intervalTicks":1000,
                         "bulletSpeed":40,"bulletGravity":0},
               "mechanics":{"enabled":true,"smokeTicks":10,
                            "grenadeFuse":3},
               "motor":{"gravity":1}}
    })");
    const auto map=Json::parse(R"({
        "players":[{"team":1},{"team":2}],
        "spawns":[[0,0.01,0],[0,0.01,4]],
        "colliders":[{"id":1,"min":[-100,-2,-100],
                                 "max":[100,0,100]}]
    })");
    auto sim=simulation(rules,map);
    if(cow)sim->world().enableCowSnapshots(true);
    fps::Projectile bullet;
    bullet.owner=1;bullet.kind=0;bullet.weapon=0;
    bullet.pos={0_fx,1_fx,0_fx};bullet.velocity={0_fx,0_fx,40_fx};
    bullet.remaining=100_fx;bullet.expires=100;bullet.damage=10;
    fps::Projectile smoke;
    smoke.owner=1;smoke.kind=4;
    smoke.pos={20_fx,5_fx,20_fx};smoke.expires=5;
    fps::Projectile grenade;
    grenade.owner=1;grenade.kind=2;
    grenade.pos={30_fx,4_fx,30_fx};grenade.expires=2;
    grenade.velocity={1_fx,2_fx,0_fx};
    spawnObject(*sim,bullet);
    const Entity smokeEntity=spawnObject(*sim,smoke);
    const uint32_t smokeId=sim->world().tryGetConst<fps::Projectile>(smokeEntity)->id;
    spawnObject(*sim,grenade);
    for(int i=0;i<2;++i)sim->step();
    CHECK(character(*sim,2).health==90); // direct bullet hit
    CHECK(!projectilesOf(*sim).empty());
    SimSnapshot before;
    sim->saveState(before);
    std::vector<uint64_t> hashes;
    for(int i=0;i<8;++i){sim->step();hashes.push_back(sim->checksum());}
    const auto expected=sim->checksum();
    auto restored=simulation(rules,map);
    CHECK(restored->deserialize(sim->serialize()));
    CHECK(restored->checksum()==expected);
    // Rollback of the real projectile stage, including entity removal and
    // deterministic smoke conversion/expiry.
    sim->loadState(before);
    for(int i=0;i<8;++i){sim->step();CHECK(sim->checksum()==hashes[size_t(i)]);}
    CHECK(sim->checksum()==expected);
    sim->loadState(before);
    // A native write to the smoke entity is the state at once.
    auto* live=sim->world().tryGet<fps::Projectile>(smokeEntity);
    CHECK(live&&live->id==smokeId&&live->kind==4);
    const auto unchanged=sim->checksum();
    live->expires+=3;
    CHECK(sim->checksum()!=unchanged);
    for(int i=0;i<8;++i)sim->step();
}
void ordinaryProjectiles() {
    ordinaryProjectilesCase(false);
    ordinaryProjectilesCase(true);
}
