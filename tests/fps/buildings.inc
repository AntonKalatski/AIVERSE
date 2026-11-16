// Map `buildings` (building-gen): generated collision, door entry and stair climbing with the real FPS motor.
void buildings() {
    auto rules=Json::parse(R"({"fps":{"tickRate":30,"mode":"deathmatch","mechanics":{"enabled":true}}})");
    auto cfg=fps::readConfig(rules);lib::systems::BuildingDef def;def.floors=3;
    def.agentRadius=cfg.motor.radius;def.agentHeight=cfg.motor.height;def.maxStepRise=fxMin(def.maxStepRise,cfg.motor.stepHeight);
    Vec3 origin{20_fx,{},20_fx};auto layout=*lib::systems::generateBuilding(def,5,origin,0);
    auto at=[](Vec3 p){Json a=Json::array();a.push(Json::number(p.x));a.push(Json::number(p.y+Fixed::fromRatio(1,100)));a.push(Json::number(p.z));return a;};
    auto bottom=layout.stairs[0].first,top=layout.stairs[0].second;bottom.z-=Fixed::fromRatio(1,20);   // flight 0 climbs +Z; keep off the first tread
    Json map=Json::parse(R"({"players":[{"team":1},{"team":2}],"colliders":[{"id":1,"min":[-200,-2,-200],"max":[200,0,200]}],
      "buildings":[{"pos":[20,0,20],"seed":5,"building":{"floors":3},"loot":[1,2,9]}]})");
    Json spawns=Json::array();spawns.push(at(layout.doorOutside));spawns.push(at(bottom));map.set("spawns",spawns);
    auto sim=simulation(rules,map);auto& s=state(*sim);auto& module=static_cast<fps::Module&>(sim->module());
    CHECK(module.collision().colliders().size()==1+layout.boxes.size()&&s.loot.size()==3);
    for(const auto& l:s.loot)CHECK(l.pos.x>origin.x&&l.pos.z>origin.z&&l.pos.x<origin.x+def.width&&l.pos.z<origin.z+def.depth);
    auto yawTo=[](Vec3 from,Vec3 to){auto d=to-from;auto y=fxAtan2Turns(d.x,d.z);return y<Fixed{}?y+1_fx:y;};
    uint32_t seq=0;
    for(int i=0;i<150;++i){
        auto& a=s.actors[0];auto& b=s.actors[1];
        fps::Input in;in.sequence=++seq;in.viewTick=sim->tick();in.forward=1000;in.yaw=yawTo(a.pos,layout.door+(layout.door-layout.doorOutside));
        auto c=fps::command(1,in);c.tick=sim->tick();sim->inject(c);
        fps::Input up;up.sequence=seq;up.viewTick=sim->tick();up.forward=1000;up.yaw=yawTo(b.pos,top);if(b.pos.y>=top.y-Fixed::fromRatio(1,10))up.forward=0;
        auto d=fps::command(2,up);d.tick=sim->tick();sim->inject(d);sim->step();
    }
    const auto& a=s.actors[0];CHECK(a.pos.x>origin.x&&a.pos.x<origin.x+def.width&&a.pos.z>origin.z&&a.pos.z<origin.z+def.depth);   // walked in
    CHECK(s.actors[1].pos.y>=def.floorHeight);                                                                                      // climbed a floor
}
