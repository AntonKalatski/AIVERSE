// fps.gear: tiered helmets / vests / backpacks (gear-tiers package) as loot, absorption, drops, saves.
void gear() {
    auto rules=Json::parse(R"({"fps":{"tickRate":20,"mode":"deathmatch","mechanics":{"enabled":true},
      "gear":[{"id":1,"slot":0,"tier":2,"durability":150,"reduction":400,"zones":1},{"id":2,"slot":1,"tier":2,"durability":100,"reduction":500,"zones":2},
              {"id":3,"slot":1,"tier":3,"durability":250,"reduction":550,"zones":2},{"id":4,"slot":2,"tier":1,"durability":1,"wear":0,"capacity":4}]}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2}],"spawns":[[0,0.01,0],[0,0.01,30]],"colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}],
      "loot":[{"pos":[0,0.01,1],"type":202},{"pos":[0,0.01,1.5],"type":203},{"pos":[0,0.01,2],"type":204}]})");
    auto bad=Json::parse(R"({"fps":{"tickRate":20,"gear":[{"id":1}]}})");
    try{fps::readConfig(bad);CHECK(false);}catch(const JsonError& e){CHECK(std::string(e.what()).find("fps.gear: requires mechanics.enabled")!=std::string::npos);}
    auto sim=simulation(rules,map);auto& s=state(*sim);CHECK(s.gear.size()==2&&s.loot.size()==3&&s.loot[0].ammo==100);
    auto interact=[&](uint32_t seq){fps::Input i;i.sequence=seq;i.viewTick=sim->tick();i.action=fps::Action::Interact;auto c=fps::command(1,i);c.tick=sim->tick();sim->inject(c);sim->step();sim->step();};
    interact(1);CHECK(s.gear[0].slots.at(1).def==2&&s.loot.size()==2);   // vest L2 picked up
    interact(2);CHECK(s.gear[0].slots.at(1).def==3);                      // vest L3 swaps: L2 lies where L3 was
    bool l2=false;for(const auto& l:s.loot)l2|=l.type==202&&l.ammo==100;CHECK(l2);
    auto& module=static_cast<fps::Module&>(sim->module());
    // body hit of 40 through a 55 % vest: 22 absorbed
    auto& victim=s.actors[0];lib::systems::GearLoadout copy=s.gear[0];auto hit=lib::systems::absorbHit(module.config().gear,copy,2,40);CHECK(hit&&hit->damage==18);
    auto bytes=sim->serialize();auto restored=simulation(rules,map);CHECK(restored->deserialize(bytes)&&restored->checksum()==sim->checksum()&&state(*restored).gear==s.gear);
    // dying drops the worn gear as loot with its durability
    auto lootBefore=s.loot.size();victim.armor=0;
    for(int i=0;i<200&&victim.health>0;++i){fps::Input in;in.sequence=100+uint32_t(i);in.viewTick=sim->tick();in.yaw=Fixed::fromRatio(1,2);in.buttons=fps::Fire;auto c=fps::command(2,in);c.tick=sim->tick();sim->inject(c);sim->step();}
    bool dropped=false;for(const auto& l:s.loot)dropped|=l.type==203&&l.ammo>0;
    CHECK(victim.health<=0&&dropped&&s.gear[0].slots.empty()&&s.loot.size()>lootBefore);
}
