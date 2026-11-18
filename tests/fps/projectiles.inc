void projectilePrograms() {
    auto base=Json::parse(R"({"fps":{"tickRate":20,"mode":"deathmatch","weapon":{"damage":10,"intervalTicks":1000,"bulletSpeed":200,"bulletGravity":0,"flight":{}},"motor":{"gravity":1}}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2},{"team":2},{"team":2}],"spawns":[[0,0.01,0],[0,0.01,4],[0,0.01,8],[0,0.01,12]],"colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}]})");
    auto make=[&](const char* flight,Json m){auto r=base;r.member("fps").member("weapon").set("flight",Json::parse(flight));return simulation(r,m);};
    auto fire=[](Simulation& sim){fps::Input in;in.buttons=fps::Fire;in.sequence=1;auto cmd=fps::command(1,in);cmd.tick=sim.tick();sim.inject(cmd);sim.step();};
    auto step=[](Simulation& sim,int n){while(n--)sim.step();};
    auto sim=make(R"({"modifiers":[{"kind":"flyThrough","count":2,"falloff":0.5}]})",map);
    fire(*sim);step(*sim,4);CHECK(state(*sim).actors[1].health==90);CHECK(state(*sim).actors[2].health==95);CHECK(state(*sim).actors[3].health==98);
    CHECK(state(*sim).actors[0].ammo==29);CHECK(state(*sim).projectiles.empty());
    sim=make(R"({"modifiers":[{"kind":"split","count":2,"angle":0},{"kind":"damage","factor":0.5},{"kind":"flyThrough","count":1}]})",map);
    fire(*sim);step(*sim,8);CHECK(state(*sim).actors[1].health==90);CHECK(state(*sim).actors[2].health==90);CHECK(state(*sim).actors[3].health==90);
    auto corner=map;corner.set("spawns",Json::parse("[[0,0.01,0],[0,0.01,4],[3,0.01,4],[6,0.01,4]]"));
    sim=make(R"({"gravity":2,"modifiers":[{"kind":"ricochet","count":2,"radius":5},{"kind":"damage","factor":0.5}]})",corner);
    fire(*sim);step(*sim,1);CHECK(!state(*sim).projectiles.empty());CHECK(state(*sim).projectiles.front().program.verticalSpeed<Fixed{});step(*sim,7);CHECK(state(*sim).actors[1].health==90);CHECK(state(*sim).actors[2].health==95);CHECK(state(*sim).actors[3].health==95);
    auto cover=map;cover.member("colliders").push(Json::parse(R"({"id":2,"min":[-1,0,6],"max":[1,5,6.01]})"));
    sim=make(R"({"modifiers":[{"kind":"flyThrough","count":3}]})",cover);fire(*sim);step(*sim,4);
    CHECK(state(*sim).actors[1].health==90);CHECK(state(*sim).actors[2].health==100);
    // Ballistic branches retain vertical velocity; state restores after a timed split.
    auto ballistic=base;ballistic.member("fps").member("weapon").set("bulletSpeed",Json::number(int64_t(10)));
    ballistic.member("fps").member("weapon").set("flight",Json::parse(R"({"gravity":4,"verticalSpeed":4,"splitBudget":2,"modifiers":[{"kind":"split","trigger":"time","after":0.1,"angle":45},{"kind":"flyThrough","count":2}]})"));
    sim=simulation(ballistic,map);fire(*sim);step(*sim,3);CHECK(state(*sim).projectiles.size()==3);
    auto saved=sim->serialize();auto copy=simulation(ballistic,map);CHECK(copy->deserialize(saved));CHECK(copy->checksum()==sim->checksum());
    for(const auto& p:state(*sim).projectiles){CHECK(p.pos.y>1_fx);CHECK(p.program.verticalSpeed>3_fx);CHECK(p.program.splitBudget==0);}
    net::authority::Snapshot active;active.localSlot=1;active.actors=state(*sim).actors;active.projectiles=state(*sim).projectiles;
    net::authority::Snapshot view;CHECK(net::authority::decodeSnapshot(net::authority::encodeSnapshot(active),view));
    CHECK(view.projectiles.size()==3);CHECK(view.projectiles.front().pos==active.projectiles.front().pos);CHECK(!view.projectiles.front().program.enabled);
    for(int i=0;i<80;++i){sim->step();copy->step();CHECK(sim->checksum()==copy->checksum());}
    // Fully vertical swept shot hits an actor above, never a target merely aligned in XZ below.
    sim=make("{}",map);fire(*sim);auto& shot=state(*sim).projectiles.front();shot.pos={0_fx,5_fx,4_fx};shot.program.direction={0_fx,1_fx,0_fx};
    state(*sim).actors[2].pos={0_fx,8_fx,4_fx};step(*sim,1);
    CHECK(state(*sim).actors[1].health==100);CHECK(state(*sim).actors[2].health==90);
    // Network presentation keeps the existing wire format; private history is server-only.
    net::authority::Snapshot snapshot;snapshot.localSlot=1;snapshot.actors=state(*sim).actors;snapshot.projectiles=state(*sim).projectiles;
    net::authority::Snapshot decoded;CHECK(net::authority::decodeSnapshot(net::authority::encodeSnapshot(snapshot),decoded));
    bool rejected=false;try{auto bad=base;bad.member("fps").member("weapon").set("bulletSpeed",Json::number(int64_t(0)));fps::readConfig(bad);}catch(const std::exception&){rejected=true;}CHECK(rejected);
}


void ecsProjectileProgramStageParityCase(bool cow) {
    // Production FPS programmed bullets are now a separate ordered
    // Simulation stage. Prove ecsAuthority's canonical runtime pools and the
    // default legacy State mode still have identical tick-visible effects.
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
    auto ecsRules=rules;
    ecsRules.member("fps").set("ecsAuthority",Json::fromBool(true));
    auto legacy=simulation(rules,map),ecs=simulation(ecsRules,map);
    if(cow) {
        legacy->world().enableCowSnapshots(true);
        ecs->world().enableCowSnapshots(true);
    }
    auto input=fps::Input{};
    input.buttons=fps::Fire;
    input.sequence=1;
    input.viewTick=0;
    auto command=fps::command(1,input);
    command.tick=0;
    legacy->inject(command);
    ecs->inject(command);
    auto checkBoth=[&]() {
        CHECK(legacy->checksum()==ecs->checksum());
        const auto& a=state(*legacy);
        const auto& b=state(*ecs);
        CHECK(a.actors==b.actors);
        CHECK(a.projectiles.size()==b.projectiles.size());
        const auto* index=ecs->world().resourceConst<fps::DynamicEntityIndex>();
        CHECK(index!=nullptr);
        size_t liveRuntimes=0;
        ecs->world().eachExistingConst<fps::ProjectileRuntime>(
            [&](Entity e,const fps::ProjectileRuntime&) {
                if(ecs->world().alive(e))++liveRuntimes;
            });
        CHECK(liveRuntimes==b.projectiles.size());
        for(size_t i=0;i<a.projectiles.size();++i) {
            const uint64_t key=(uint64_t(1)<<32)|b.projectiles[i].id;
            const auto entry=index->byKey.find(key);
            CHECK(entry!=index->byKey.end());
            CHECK(ecs->world().alive(entry->second));
            const auto* runtime=ecs->world().tryGetConst<fps::ProjectileRuntime>(entry->second);
            CHECK(runtime!=nullptr);
            CHECK(runtime->order==i);
            CHECK(runtime->value.id==b.projectiles[i].id);
            CHECK(runtime->value.pos==b.projectiles[i].pos);
            CHECK(runtime->value.damage==b.projectiles[i].damage);
            CHECK(a.projectiles[i].id==b.projectiles[i].id);
            CHECK(a.projectiles[i].pos==b.projectiles[i].pos);
            CHECK(a.projectiles[i].remaining==b.projectiles[i].remaining);
            CHECK(a.projectiles[i].program.age==b.projectiles[i].program.age);
        }
    };
    legacy->step();ecs->step(); // shot created after projectile program phase
    checkBoth();
    CHECK(!state(*legacy).projectiles.empty());
    SimSnapshot beforeProgramLegacy,beforeProgramEcs;
    legacy->saveState(beforeProgramLegacy);
    ecs->saveState(beforeProgramEcs);
    for(int i=0;i<12;++i) {
        legacy->step();ecs->step();
        checkBoth();
    }
    CHECK(state(*legacy).projectiles.empty());
    CHECK(state(*legacy).actors[1].health==90);
    CHECK(state(*legacy).actors[2].health==95);
    CHECK(state(*legacy).actors[3].health==98);
    const uint64_t expected=legacy->checksum();
    const auto legacySave=legacy->serialize();
    const auto ecsSave=ecs->serialize();
    auto fromLegacy=simulation(ecsRules,map);
    auto fromEcs=simulation(rules,map);
    CHECK(fromLegacy->deserialize(legacySave));
    CHECK(fromEcs->deserialize(ecsSave));
    CHECK(fromLegacy->checksum()==expected);
    CHECK(fromEcs->checksum()==expected);
    // SaveState must also rewind the pending projectile and repeat every hit.
    legacy->loadState(beforeProgramLegacy);
    ecs->loadState(beforeProgramEcs);
    checkBoth();
    for(int i=0;i<12;++i) {
        legacy->step();ecs->step();
        checkBoth();
    }
    CHECK(legacy->checksum()==expected);

    // The ECS component is authoritative: an external native write must
    // override a deliberately stale legacy projection before the next stage.
    // Repeat in deep and COW modes to catch accidental workspace writes.
    legacy->loadState(beforeProgramLegacy);
    ecs->loadState(beforeProgramEcs);
    const auto& pending=state(*ecs).projectiles;
    CHECK(pending.size()==1);
    const uint64_t key=(uint64_t(1)<<32)|pending.front().id;
    const auto* index=ecs->world().resourceConst<fps::DynamicEntityIndex>();
    CHECK(index && index->byKey.contains(key));
    auto* canonical=ecs->world().tryGet<fps::ProjectileRuntime>(index->byKey.at(key));
    CHECK(canonical!=nullptr);
    canonical->value.damage=12;
    state(*legacy).projectiles.front().damage=12;
    state(*ecs).projectiles.front().damage=777; // must not become canonical
    for(int i=0;i<12;++i) {
        legacy->step();ecs->step();
        checkBoth();
    }
    CHECK(state(*legacy).actors[1].health==88);
}

void ecsProjectileProgramStageParity() {
    ecsProjectileProgramStageParityCase(false); // deep snapshots
    ecsProjectileProgramStageParityCase(true);  // COW snapshots
}


void ecsProjectileBranchAuthorityParityCase(bool cow) {
    // Branching shots may retire a source and create multiple children in one
    // tick. Verify ECS Entity recycling, stable projectile order and v9 saves.
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
    auto ecsRules=rules;
    ecsRules.member("fps").set("ecsAuthority",Json::fromBool(true));
    auto legacy=simulation(rules,map),ecs=simulation(ecsRules,map);
    if(cow) {
        legacy->world().enableCowSnapshots(true);
        ecs->world().enableCowSnapshots(true);
    }
    fps::Input input;input.buttons=fps::Fire;input.sequence=1;
    // Insert a nonprogrammed cloud before the first shot. Only the
    // authoritative ECS Runtime exists initially (no State vector edit),
    // while the legacy baseline receives the equivalent source object.
    // This checks mixed projectile ownership and ECS hydration/reindex.
    fps::Projectile passive;
    passive.id=9001;passive.owner=1;passive.kind=4;
    passive.expires=1000;passive.pos={10_fx,5_fx,10_fx};
    state(*legacy).projectiles.push_back(passive);
    const Entity passiveEntity=ecs->world().create();
    ecs->world().add<fps::ProjectileRuntime>(
        passiveEntity,fps::ProjectileRuntime{passive,0});

    const auto command=fps::command(1,input);
    legacy->inject(command);
    ecs->inject(command);
    auto compare=[&]() {
        CHECK(legacy->checksum()==ecs->checksum());
        CHECK(state(*legacy).actors==state(*ecs).actors);
        const auto& left=state(*legacy).projectiles;
        const auto& right=state(*ecs).projectiles;
        CHECK(left.size()==right.size());
        CHECK(state(*legacy).nextObject==state(*ecs).nextObject);
        size_t runtimeCount=0;
        ecs->world().eachExistingConst<fps::ProjectileRuntime>(
            [&](Entity entity,const fps::ProjectileRuntime& runtime) {
                if(ecs->world().alive(entity)) {
                    ++runtimeCount;
                    CHECK(runtime.order<right.size());
                    CHECK(runtime.value.id==right[runtime.order].id);
                    CHECK(runtime.value.pos==right[runtime.order].pos);
                }
            });
        CHECK(runtimeCount==right.size());
        const auto* index=ecs->world().resourceConst<fps::DynamicEntityIndex>();
        CHECK(index!=nullptr);
        for(size_t i=0;i<right.size();++i) {
            CHECK(left[i].id==right[i].id);
            CHECK(left[i].pos==right[i].pos);
            CHECK(left[i].program.age==right[i].program.age);
            const auto key=(uint64_t(1)<<32)|right[i].id;
            CHECK(index->byKey.contains(key));
            const auto entity=index->byKey.at(key);
            CHECK(ecs->world().alive(entity));
            const auto* value=ecs->world().tryGetConst<fps::ProjectileRuntime>(entity);
            CHECK(value && value->order==i);
        }
    };
    legacy->step();ecs->step();
    compare();
    SimSnapshot preLegacy,preEcs;
    legacy->saveState(preLegacy);ecs->saveState(preEcs);
    uint32_t maxAllocated=state(*legacy).nextObject;
    for(int tick=0;tick<8;++tick) {
        legacy->step();ecs->step();compare();
        maxAllocated=std::max(maxAllocated,state(*legacy).nextObject);
    }
    CHECK(maxAllocated>3); // the original bullet created branch children
    CHECK(state(*legacy).projectiles.size()==1);
    CHECK(state(*legacy).projectiles.front().id==passive.id);
    CHECK(state(*ecs).projectiles.size()==1);
    CHECK(state(*ecs).projectiles.front().id==passive.id);
    for(size_t slot=1;slot<=3;++slot)
        CHECK(state(*legacy).actors[slot].health==90);
    const auto checksum=legacy->checksum();
    const auto legacyBytes=legacy->serialize(),ecsBytes=ecs->serialize();
    auto fromLegacy=simulation(ecsRules,map),fromEcs=simulation(rules,map);
    CHECK(fromLegacy->deserialize(legacyBytes));
    CHECK(fromEcs->deserialize(ecsBytes));
    CHECK(fromLegacy->checksum()==checksum);
    CHECK(fromEcs->checksum()==checksum);
    // Replay branch allocation with identical ECS generations and order.
    legacy->loadState(preLegacy);ecs->loadState(preEcs);
    compare();
    for(int tick=0;tick<8;++tick) {
        legacy->step();ecs->step();compare();
    }
    CHECK(ecs->checksum()==checksum);
}
void ecsProjectileBranchAuthorityParity() {
    ecsProjectileBranchAuthorityParityCase(false);
    ecsProjectileBranchAuthorityParityCase(true);
}
