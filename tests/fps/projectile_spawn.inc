// Regression for actual FPS weapon/grenade creation into ProjectileRuntime.
// No genre-specific test-only spawn hook: exercise production input Commands.
void ecsProjectileSpawnAuthorityParityCase(bool cow) {
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
    auto ecsRules=rules;
    ecsRules.member("fps").set("ecsAuthority",Json::fromBool(true));
    auto legacy=simulation(rules,map),ecs=simulation(ecsRules,map);
    if(cow) {
        legacy->world().enableCowSnapshots(true);
        ecs->world().enableCowSnapshots(true);
    }
    auto compare=[&]() {
        CHECK(legacy->checksum()==ecs->checksum());
        const auto& a=state(*legacy);
        const auto& b=state(*ecs);
        CHECK(a.actors==b.actors);
        CHECK(a.projectiles.size()==b.projectiles.size());
        CHECK(a.nextObject==b.nextObject);
        const auto* index=ecs->world().resourceConst<fps::DynamicEntityIndex>();
        CHECK(index!=nullptr);
        size_t liveCount=0;
        ecs->world().eachExistingConst<fps::ProjectileRuntime>(
            [&](Entity e,const fps::ProjectileRuntime&) {
                if(ecs->world().alive(e))++liveCount;
            });
        CHECK(liveCount==b.projectiles.size());
        for(size_t i=0;i<b.projectiles.size();++i) {
            const auto& left=a.projectiles[i];
            const auto& right=b.projectiles[i];
            CHECK(left.id==right.id);
            CHECK(left.kind==right.kind);
            CHECK(left.owner==right.owner);
            CHECK(left.pos==right.pos);
            CHECK(left.velocity==right.velocity);
            CHECK(left.damage==right.damage);
            CHECK(left.expires==right.expires);
            const uint64_t key=(uint64_t(1)<<32)|right.id;
            CHECK(index->byKey.contains(key));
            const auto entity=index->byKey.at(key);
            CHECK(ecs->world().alive(entity));
            const auto* runtime=ecs->world().tryGetConst<fps::ProjectileRuntime>(entity);
            CHECK(runtime && runtime->order==i);
            CHECK(runtime->value.id==right.id);
            CHECK(runtime->value.pos==right.pos);
            CHECK(runtime->value.kind==right.kind);
            CHECK(runtime->value.damage==right.damage);
        }
    };
    auto tick=[&](uint32_t t,uint32_t buttons,fps::Action action) {
        fps::Input input;
        input.sequence=t+1;
        input.viewTick=t;
        input.buttons=buttons;
        input.action=action;
        auto cmd=fps::command(1,input);
        cmd.tick=t;
        legacy->inject(cmd);
        ecs->inject(cmd);
        legacy->step();
        ecs->step();
        compare();
    };
    // First accepted shot is created in the actor stage, after the flight
    // stage, so it must not advance until the next tick.
    tick(0,fps::Fire,fps::Action::None);
    CHECK(state(*ecs).projectiles.size()==1);
    const uint32_t weaponShotId=state(*ecs).projectiles[0].id;
    CHECK(state(*ecs).projectiles[0].kind==0);
    // The new one-way ECS mirror must NEVER copy a stale compatibility
    // State value back over the runtime at the end-of-tick barrier.
    const auto* index=ecs->world().resourceConst<fps::DynamicEntityIndex>();
    const uint64_t key=(uint64_t(1)<<32)|weaponShotId;
    CHECK(index && index->byKey.contains(key));
    auto* canonical=ecs->world().tryGet<fps::ProjectileRuntime>(
        index->byKey.at(key));
    CHECK(canonical && canonical->value.kind==0);
    canonical->value.damage=17;
    state(*legacy).projectiles[0].damage=17;
    state(*ecs).projectiles[0].damage=777; // intentionally stale
    static_cast<fps::Module&>(ecs->module()).syncDynamicEntities(*ecs,true);
    CHECK(state(*ecs).projectiles[0].damage==17);
    compare();
    // All three throwable actions use the same production spawn adapter.
    tick(1,0,fps::Action::Frag);
    tick(2,0,fps::Action::Smoke);
    tick(3,0,fps::Action::Flash);
    bool frag=false,smoke=false,flash=false;
    for(const auto& shot:state(*ecs).projectiles) {
        if(shot.kind==1)frag=true;
        if(shot.kind==2)smoke=true;
        if(shot.kind==3)flash=true;
    }
    CHECK(frag && smoke && flash);
    CHECK(state(*ecs).nextObject>weaponShotId+3);
    SimSnapshot checkpointLegacy,checkpointEcs;
    legacy->saveState(checkpointLegacy);
    ecs->saveState(checkpointEcs);
    const auto legacyBytes=legacy->serialize();
    const auto ecsBytes=ecs->serialize();
    auto fromLegacy=simulation(ecsRules,map);
    auto fromEcs=simulation(rules,map);
    CHECK(fromLegacy->deserialize(legacyBytes));
    CHECK(fromEcs->deserialize(ecsBytes));
    CHECK(fromLegacy->checksum()==legacy->checksum());
    CHECK(fromEcs->checksum()==ecs->checksum());
    for(uint32_t t=4;t<14;++t)
        tick(t,t==4?fps::Fire:0,fps::Action::None);
    const auto finalChecksum=ecs->checksum();
    legacy->loadState(checkpointLegacy);
    ecs->loadState(checkpointEcs);
    compare();
    for(uint32_t t=4;t<14;++t)
        tick(t,t==4?fps::Fire:0,fps::Action::None);
    CHECK(ecs->checksum()==finalChecksum);
}
void ecsProjectileSpawnAuthorityParity() {
    ecsProjectileSpawnAuthorityParityCase(false);
    ecsProjectileSpawnAuthorityParityCase(true);
}


// Bomb-round cleanup must retire canonical Runtime/ID mappings immediately,
// and a subsequent spawn must not reuse a stale projectile key or view.
void ecsProjectileRoundResetParityCase(bool cow) {
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
    auto ecsRules=rules;
    ecsRules.member("fps").set("ecsAuthority",Json::fromBool(true));
    auto legacy=simulation(rules,map),ecs=simulation(ecsRules,map);
    if(cow) {
        legacy->world().enableCowSnapshots(true);
        ecs->world().enableCowSnapshots(true);
    }
    auto compare=[&]() {
        CHECK(legacy->checksum()==ecs->checksum());
        CHECK(state(*legacy).actors==state(*ecs).actors);
        const auto& a=state(*legacy).projectiles;
        const auto& b=state(*ecs).projectiles;
        CHECK(a.size()==b.size());
        const auto* index=ecs->world().resourceConst<fps::DynamicEntityIndex>();
        CHECK(index!=nullptr);
        size_t runtimeCount=0;
        ecs->world().eachExistingConst<fps::ProjectileRuntime>(
            [&](Entity e,const fps::ProjectileRuntime& r) {
                if(ecs->world().alive(e)) {
                    ++runtimeCount;
                    CHECK(r.order<b.size());
                    CHECK(r.value.id==b[r.order].id);
                }
            });
        CHECK(runtimeCount==b.size());
        for(size_t i=0;i<b.size();++i) {
            CHECK(a[i].id==b[i].id);
            const uint64_t key=(uint64_t(1)<<32)|b[i].id;
            CHECK(index->byKey.contains(key));
            CHECK(ecs->world().alive(index->byKey.at(key)));
        }
    };
    auto tick=[&](uint32_t t,fps::Action action) {
        fps::Input input;input.sequence=t+1;input.viewTick=t;
        input.action=action;
        auto cmd=fps::command(1,input);cmd.tick=t;
        legacy->inject(cmd);ecs->inject(cmd);
        legacy->step();ecs->step();compare();
    };
    tick(0,fps::Action::None); // freeze phase
    tick(1,fps::Action::Frag); // create a real Runtime grenade
    CHECK(state(*legacy).projectiles.size()==1);
    const uint32_t previousId=state(*legacy).projectiles[0].id;

    // Force a round conclusion from identical in-match State resources.
    // The normal tickSystems() reset must call clearProjectiles() and
    // destroy ECS entities while keeping existing loot/vehicle identities.
    state(*legacy).match.end=legacy->tick();
    state(*ecs).match.end=ecs->tick();
    SimSnapshot beforeLegacy,beforeEcs;
    legacy->saveState(beforeLegacy);ecs->saveState(beforeEcs);
    tick(2,fps::Action::None);
    CHECK(state(*ecs).projectiles.empty());
    CHECK(ecs->world().countExisting<fps::ProjectileRuntime>()==0);
    const auto* index=ecs->world().resourceConst<fps::DynamicEntityIndex>();
    CHECK(index!=nullptr);
    for(const auto& [key,entity]:index->byKey) {
        (void)entity;
        CHECK((key>>32)!=1); // no dead projectile ID survives reset
    }
    const uint64_t resetChecksum=ecs->checksum();
    auto legacyBytes=legacy->serialize(),ecsBytes=ecs->serialize();
    auto fromLegacy=simulation(ecsRules,map),fromEcs=simulation(rules,map);
    CHECK(fromLegacy->deserialize(legacyBytes));
    CHECK(fromEcs->deserialize(ecsBytes));
    CHECK(fromLegacy->checksum()==resetChecksum);
    CHECK(fromEcs->checksum()==resetChecksum);

    legacy->loadState(beforeLegacy);ecs->loadState(beforeEcs);
    tick(2,fps::Action::None);
    CHECK(ecs->checksum()==resetChecksum);
    tick(3,fps::Action::Frag); // new object after ECS destruction/recycle
    CHECK(state(*legacy).projectiles.size()==1);
    CHECK(state(*legacy).projectiles[0].id>previousId);
    compare();
}
void ecsProjectileRoundResetParity() {
    ecsProjectileRoundResetParityCase(false);
    ecsProjectileRoundResetParityCase(true);
}
