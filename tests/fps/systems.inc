void systems() {
    auto rules=Json::parse(R"({"fps":{"tickRate":20,"mode":"deathmatch","weapons":[{"damage":25,"magazine":30,"intervalTicks":1},{"damage":20,"price":500}],"mechanics":{"enabled":true,"healTicks":3,"reviveTicks":3,"grenadeFuse":3,"smokeTicks":20,"flashTicks":10,"freezeTicks":2,"plantTicks":3,"defuseTicks":3,"bombTicks":10,"roundsToWin":2}}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2},{"team":2}],"spawns":[[0,0.01,0],[0,0.01,10],[2,0.01,10]],"colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}],"loot":[{"pos":[1,0.01,0],"type":6}],"vehicles":[{"pos":[-3,0.01,0]}],"ladders":[{"min":[-1,0,-1],"max":[1,5,1]}]})");
    auto sim=simulation(rules,map);
    auto step=[&](int slot,uint32_t buttons=fps::Buttons(0),fps::Action action=fps::Action::None,uint32_t arg=0,Fixed pitch=Fixed{},int32_t forward=0) {
        fps::Input i;i.sequence=sim->tick()+1;i.viewTick=sim->tick();i.buttons=buttons;i.action=action;i.argument=arg;i.pitch=pitch;i.forward=forward;
        auto c=fps::command(uint8_t(slot),i);c.tick=sim->tick();sim->inject(c);sim->step();
    };
    // Pick up and install an attachment; drop/pickup keeps its ammo and attachment bits.
    step(1,0,fps::Action::Interact);CHECK(lootOf(*sim).empty());CHECK(character(*sim,1).bag.back().type==6);
    step(1,0,fps::Action::Attach,6);CHECK(character(*sim,1).attachments[0]==1);
    character(*sim,1).ammo=7;step(1,0,fps::Action::Drop);CHECK(!(character(*sim,1).owned&1));
    step(1,0,fps::Action::Interact);CHECK(character(*sim,1).ammo==7);CHECK(character(*sim,1).attachments[0]==1);
    // Healing is delayed, consumes one kit, and moving cancels without consuming a kit.
    character(*sim,1).health=20;step(1,0,fps::Action::Heal);CHECK(character(*sim,1).health==20);
    for(int i=0;i<3;++i)step(1);
    CHECK(character(*sim,1).health==80);
    step(1,0,fps::Action::Heal);step(1,0,fps::Action::None,0,{},1000);CHECK(character(*sim,1).activeAction==fps::Action::None);
    // Vehicle admission/exit is exclusive and the authoritative position follows driving.
    character(*sim,1).pos={-3_fx,Fixed::fromRatio(1,100),{}};step(1,0,fps::Action::Interact);CHECK(character(*sim,1).vehicle!=0);
    auto before=vehiclesOf(*sim)[0].pos;step(1,0,fps::Action::None,0,{},1000);CHECK(vehiclesOf(*sim)[0].pos.z>before.z);
    step(1,0,fps::Action::Interact);CHECK(!character(*sim,1).vehicle);
    // Ladder movement and prone height are server-state, not camera-only tricks.
    character(*sim,1).pos={{},Fixed::fromRatio(1,100),{}};for(const Entity e:fpsModule(*sim).vehicleEntities(*sim))sim->world().destroy(e);step(1,0,fps::Action::Interact);CHECK(character(*sim,1).climbing);
    before=character(*sim,1).pos;step(1,0,fps::Action::None,0,{},1000);CHECK(character(*sim,1).pos.y>before.y);
    step(1,0,fps::Action::Interact);step(1,fps::Prone);CHECK(character(*sim,1).prone);
    // Grenades create bounded persistent objects; smoke becomes a timed cloud.
    sim=simulation(rules,map);step(1,0,fps::Action::Smoke);CHECK(projectilesOf(*sim).size()==1);
    for(int i=0;i<4;++i)step(1);
    CHECK(projectilesOf(*sim)[0].kind==4);
    step(1,0,fps::Action::Flash);auto& flash=projectileAt(*sim,projectilesOf(*sim).size()-1);flash.pos=character(*sim,2).pos+Vec3{{},1_fx,1_fx};flash.expires=sim->tick();step(1);CHECK(character(*sim,2).blindUntil>sim->tick());
    step(1,0,fps::Action::Frag);auto& frag=projectileAt(*sim,projectilesOf(*sim).size()-1);frag.pos=character(*sim,2).pos+Vec3{{},1_fx,{}};frag.expires=sim->tick();step(1);CHECK(character(*sim,2).health<100);
    // Body armor and head hit multipliers use the hit position.
    sim=simulation(rules,map);character(*sim,2).armor=50;step(1,fps::Fire,fps::Action::None,0,Fixed::fromRatio(-1,100));CHECK(character(*sim,2).armor<50);CHECK(character(*sim,2).health>75);
    sim=simulation(rules,map);character(*sim,2).armor=50;step(1,fps::Fire);CHECK(character(*sim,2).health==50);CHECK(character(*sim,2).armor==50);
    // Ballistic bullets travel on later ticks; rewind is only used for hitscan.
    auto ballistic=rules;ballistic.member("fps").member("weapons").at(0).set("bulletSpeed",Json::number(int64_t(200)));
    sim=simulation(ballistic,map);step(1,fps::Fire);CHECK(character(*sim,2).health==100);CHECK(!projectilesOf(*sim).empty());step(1);CHECK(character(*sim,2).health<100);
    // Penetration is opt-in per collider; solid cover remains authoritative.
    auto cover=map;cover.member("colliders").push(Json::parse(R"({"id":2,"min":[-1,0,4],"max":[1,3,4.2],"penetration":1})"));
    sim=simulation(rules,cover);step(1,fps::Fire);CHECK(character(*sim,2).health==100);
    auto piercing=rules;piercing.member("fps").member("weapons").at(0).set("penetration",Json::number(int64_t(2)));sim=simulation(piercing,cover);step(1,fps::Fire);CHECK(character(*sim,2).health<100);
    // Knockdown needs a living teammate; revival is cancellable and team-checked.
    auto knock=rules;knock.member("fps").member("mechanics").set("knockdown",Json::parse("true"));sim=simulation(knock,map);character(*sim,2).health=20;step(1,fps::Fire);CHECK(character(*sim,2).downed);
    step(1);step(3,0,fps::Action::Interact);for(int i=0;i<3;++i)step(3);CHECK(!character(*sim,2).downed);CHECK(character(*sim,2).health==40);
    // Every extended field and dynamic object survives save and deterministic continuation.
    auto save=sim->serialize();auto restored=simulation(knock,map);CHECK(restored->deserialize(save));CHECK(restored->checksum()==sim->checksum());
    for(int i=0;i<20;++i){sim->step();restored->step();CHECK(restored->checksum()==sim->checksum());}
    // Bomb rules: buy zone/price, freeze, plant timer, defuse, round score and reset.
    auto bombRules=rules;bombRules.member("fps").set("mode",Json::string("bomb"));auto bombMap=map;bombMap.set("bombSites",Json::parse(R"([{"min":[-2,0,-2],"max":[2,3,2]}])"));
    sim=simulation(bombRules,bombMap);step(1,0,fps::Action::Buy,1);CHECK(character(*sim,1).money==300);CHECK(character(*sim,1).owned==3);
    step(1,0,fps::Action::Buy,1);CHECK(character(*sim,1).money==300);
    step(1,0,fps::Action::Plant);for(int i=0;i<3;++i)step(1);CHECK(state(*sim).match.planted);
    character(*sim,2).pos=character(*sim,1).pos+Vec3{1_fx,{}, {}};step(2,0,fps::Action::Interact);for(int i=0;i<3;++i)step(2);CHECK(state(*sim).match.scoreB==1);
    for(int i=0;i<65;++i)step(1);
    CHECK(state(*sim).round==2);
    CHECK(!state(*sim).match.planted);
    // Vault checks overhead clearance and requires a short obstacle.
    auto vaultMap=map;vaultMap.member("colliders").push(Json::parse(R"({"id":2,"min":[-1,0,0.7],"max":[1,1,1.2]})"));sim=simulation(rules,vaultMap);step(1);step(1,fps::Buttons(0),fps::Action::Vault);CHECK(character(*sim,1).pos.z>1_fx);
}
