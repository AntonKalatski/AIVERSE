void melee() {
    auto rules=Json::parse(R"({"fps":{"tickRate":20,"mode":"deathmatch","melee":{"enabled":true,"damage":50}}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2}],"spawns":[[0,0.01,0],[0,0.01,1.5]],"colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}]})");
    // Each tick both players send input: attacker (slot 1) and defender (slot 2, faces the attacker at yaw 0.5).
    struct Tick {uint32_t attacker=0,defender=0;Fixed attackerYaw{},defenderYaw=Fixed::fromRatio(1,2);};
    auto step=[](Simulation& sim,const Tick& t){
        for(uint8_t slot:{uint8_t(1),uint8_t(2)}){
            actor::Input i;i.sequence=sim.tick()+1;i.viewTick=sim.tick();
            i.buttons=slot==1?t.attacker:t.defender;i.yaw=slot==1?t.attackerYaw:t.defenderYaw;
            auto c=actor::command(slot,i);c.tick=sim.tick();sim.inject(c);
        }
        sim.step();
    };
    auto run=[&](Simulation& sim,Tick t,int ticks){for(int i=0;i<ticks;++i)step(sim,t);};
    // Rules without the block stay unchanged: the strike button does nothing.
    auto plain=Json::parse(R"({"fps":{"tickRate":20,"mode":"deathmatch"}})");
    auto sim=simulation(plain,map);run(*sim,{actor::Melee},20);CHECK(character(*sim,2).health==100);
    CHECK(!actor::readConfig(plain).melee.enabled);
    // A strike from the front of an unguarded target lands once per swing and staggers it.
    sim=simulation(rules,map);run(*sim,{actor::Melee},4);
    CHECK(character(*sim,2).health==50);CHECK(character(*sim,2).staggerUntil>sim->tick());
    // One swing hits each target at most once, and a held button chains the next swing.
    run(*sim,{actor::Melee},3);CHECK(character(*sim,2).health==50);
    run(*sim,{actor::Melee},8);CHECK(character(*sim,2).health==0);
    // Holding guard while facing the attacker blocks after the parry window has passed.
    sim=simulation(rules,map);run(*sim,{0,actor::Block},5);run(*sim,{actor::Melee,actor::Block},6);
    CHECK(character(*sim,2).health==100);CHECK(character(*sim,1).staggerUntil==0);
    // Guard does not cover the back.
    sim=simulation(rules,map);run(*sim,{0,actor::Block,{},Fixed{}},5);run(*sim,{actor::Melee,actor::Block,{},Fixed{}},6);
    CHECK(character(*sim,2).health==50);
    // Pressing guard as the swing starts parries: no damage and the attacker is staggered and cannot strike.
    sim=simulation(rules,map);step(*sim,{actor::Melee});run(*sim,{actor::Melee,actor::Block},3);
    CHECK(character(*sim,2).health==100);auto stagger=character(*sim,1).staggerUntil;CHECK(stagger>sim->tick());
    run(*sim,{actor::Melee,0},int(stagger-sim->tick()));CHECK(character(*sim,2).health==100);
    // Blocking and stagger also keep the gun from firing.
    sim=simulation(rules,map);run(*sim,{actor::Fire|actor::Block},3);CHECK(character(*sim,1).shots==0);
    // Lock-on turns the swing toward the nearest enemy even when the view looks away.
    sim=simulation(rules,map);run(*sim,{actor::Melee|actor::LockOn,0,Fixed::fromRatio(1,4)},4);
    CHECK(character(*sim,1).lockTarget==2);CHECK(character(*sim,2).health==50);
    sim=simulation(rules,map);run(*sim,{actor::Melee,0,Fixed::fromRatio(1,4)},6);CHECK(character(*sim,2).health==100);
    // Mid-swing state is hashed, saved (version 7) and restored; rollback replays identically.
    sim=simulation(rules,map);run(*sim,{actor::Melee,actor::Block},2);
    CHECK(character(*sim,1).melee.attack==1);
    auto saved=sim->serialize();auto restored=simulation(rules,map);CHECK(restored->deserialize(saved));CHECK(restored->checksum()==sim->checksum());
    CHECK(character(*restored,1).melee==character(*sim,1).melee);
    for(int i=0;i<10;++i){step(*sim,{actor::Melee});step(*restored,{actor::Melee});CHECK(sim->checksum()==restored->checksum());}
    auto twin=simulation(rules,map);run(*twin,{actor::Melee,actor::Block},2);auto hash=twin->checksum();character(*twin,1).staggerUntil+=1;CHECK(twin->checksum()!=hash);
    // Out-of-range settings are rejected with a readable error.
    auto bad=rules;bad.member("fps").member("melee").set("range",Json::number(int64_t(50)));
    bool threw=false;try{actor::readConfig(bad);}catch(const JsonError&){threw=true;}CHECK(threw);
}
