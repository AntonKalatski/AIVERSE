// Map `buildings` (building-gen): generated collision, door entry and stair climbing with the real FPS motor.
void buildings() {
    auto rules=Json::parse(R"({"fps":{"tickRate":30,"mode":"deathmatch","mechanics":{"enabled":true}}})");
    auto cfg=fps::readConfig(rules);lib::systems::BuildingDef def;def.floors=3;
    def.agentRadius=cfg.motor.radius;def.agentHeight=cfg.motor.height;def.maxStepRise=fxMin(def.maxStepRise,cfg.motor.stepHeight);
    Vec3 origin{20_fx,{},20_fx};auto layout=*lib::systems::generateBuilding(def,5,origin-Vec3{{},def.slab,{}},0);layout.doorOutside.y={};
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
// Map colliders with "yaw": exact rotated boxes (12 triangles), not their bounding boxes.
void rotatedColliders() {
    auto rules=Json::parse(R"({"fps":{"tickRate":30,"mode":"deathmatch"}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2}],"spawns":[[-6,0.01,0],[30,0.01,30]],
      "colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]},{"id":2,"center":[0,1.5,0],"half":[20,1.5,0.25],"yaw":0.125}]})");
    auto sim=simulation(rules,map);auto& module=static_cast<fps::Module&>(sim->module());const auto& w=module.collision();
    CHECK(w.cast({-1_fx,1_fx,-6_fx},{2_fx,{},12_fx}).hit);                       // through the middle of the turned wall
    // The wall runs from (-14, 14) to (14, -14); (2, 2.5) is inside its axis-aligned bounds but 3 m away.
    CHECK(!w.cast({2_fx,1_fx,Fixed::fromRatio(3,2)},{{},{},2_fx}).hit);
    CHECK(w.cast({3_fx,1_fx,3_fx},{-6_fx,{},-6_fx}).hit);
    auto& s=state(*sim);uint32_t seq=0;
    for(int i=0;i<120;++i){fps::Input in;in.sequence=++seq;in.viewTick=sim->tick();in.forward=1000;in.yaw=Fixed::fromRatio(1,4);   // walk +X into the wall
        auto c=fps::command(1,in);c.tick=sim->tick();sim->inject(c);sim->step();}
    const auto& p=s.actors[0].pos;CHECK(p.x+p.z<Fixed{}&&p.x>-5_fx);   // slid along the turned wall, never through it (wall line x + z = 0)
}
// fps.bots: a library bot walks through the door of a generated house and picks up the loot inside.
void houseBots() {
    auto rules=Json::parse(R"({"fps":{"tickRate":30,"mode":"deathmatch","mechanics":{"enabled":true},"bots":{"preset":"normal"},
      "weapons":[{"nameKey":"fps.weapon.pistol"},{"nameKey":"fps.weapon.rifle"}]}})");
    auto cfg=fps::readConfig(rules);lib::systems::BuildingDef def;def.floors=1;
    def.agentRadius=cfg.motor.radius;def.agentHeight=cfg.motor.height;def.maxStepRise=fxMin(def.maxStepRise,cfg.motor.stepHeight);
    Vec3 origin{20_fx,{},20_fx};auto layout=*lib::systems::generateBuilding(def,8,origin-Vec3{{},def.slab,{}},0);layout.doorOutside.y={};
    auto at=[](Vec3 p){Json a=Json::array();a.push(Json::number(p.x));a.push(Json::number(p.y+Fixed::fromRatio(1,100)));a.push(Json::number(p.z));return a;};
    Json map=Json::parse(R"({"players":[{"team":1,"bot":true},{"team":2}],"colliders":[{"id":1,"min":[-200,-2,-200],"max":[200,0,200]}],
      "buildings":[{"pos":[20,0,20],"seed":8,"building":{"floors":1},"loot":[101]}]})");
    auto away=layout.doorOutside-layout.door;away.y={};Json spawns=Json::array();spawns.push(at(layout.doorOutside+away*3_fx));spawns.push(at({-150_fx,{},-150_fx}));map.set("spawns",spawns);
    auto sim=simulation(rules,map),twin=simulation(rules,map);auto& s=state(*sim);
    CHECK(s.botStates.size()==2&&s.loot.size()==1);
    s.actors[0].owned=1;state(*twin).actors[0].owned=1;   // carries only the pistol
    for(int i=0;i<900&&s.loot.size();++i){sim->step();twin->step();CHECK(sim->checksum()==twin->checksum());
        if(i==300){auto bytes=sim->serialize();auto copy=simulation(rules,map);CHECK(copy->deserialize(bytes)&&copy->checksum()==sim->checksum());}}
    CHECK(s.loot.empty()&&(s.actors[0].owned&2u));   // walked in and took the rifle
}
// Every room of generated houses (all floors, via the stairs) is reachable on the bots' navmesh from the door.
void houseNavigation() {
    auto rules=Json::parse(R"({"fps":{"tickRate":30,"mode":"deathmatch"}})");auto cfg=fps::readConfig(rules);
    for(uint64_t seed=1;seed<=6;++seed)for(uint32_t floors:{1u,2u,3u}){
        lib::systems::BuildingDef def;def.floors=floors;def.agentRadius=cfg.motor.radius;def.agentHeight=cfg.motor.height;def.maxStepRise=fxMin(def.maxStepRise,cfg.motor.stepHeight);
        Json b=Json::object();b.set("floors",Json::number(int64_t(floors)));Json house=Json::object();house.set("pos",Json::parse("[0,0,0]"));house.set("seed",Json::number(int64_t(seed)));house.set("building",b);
        Json list=Json::array();list.push(house);
        auto map=Json::parse(R"({"players":[{"team":1},{"team":2}],"spawns":[[-30,0.01,-30],[-35,0.01,-30]],"colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}]})");map.set("buildings",list);
        auto sim=simulation(rules,map);const auto& w=static_cast<fps::Module&>(sim->module()).collision();
        auto layout=*lib::systems::generateBuilding(def,seed,{{},-def.slab,{}},0);layout.doorOutside.y={};
        lib::bots::NavBuildConfig c;c.bounds={{-4_fx,-1_fx,-4_fx},{16_fx,def.floorHeight*Fixed::fromInt(floors)+2_fx,16_fx}};c.cellSize=Fixed::fromRatio(1,2);
        c.radius=cfg.motor.radius;c.height=cfg.motor.height;c.stepHeight=cfg.motor.stepHeight;auto mesh=lib::bots::buildNavMesh(w,c);CHECK(mesh.mesh);
        for(const auto& [h0,h1]:layout.stairHalls)for(auto b:{h0,h1}){if(b.y>Fixed::fromRatio(1,2))continue;bool ok=bool(lib::bots::navPath(*mesh.mesh,w,layout.doorOutside,b));
            if(!ok)std::printf("stairs unreachable seed %llu floors %u bottom %f %f %f poly %d\n",(unsigned long long)seed,floors,b.x.toDouble(),b.y.toDouble(),b.z.toDouble(),int(bool(lib::bots::navPolygonAt(*mesh.mesh,b))));CHECK(ok);}
        for(auto spot:layout.lootSpots){if(spot.y>Fixed::fromRatio(1,2))continue;   // upper floors: stairs are crossed outside the mesh (upstairsBot)
            bool ok=bool(lib::bots::navPath(*mesh.mesh,w,layout.doorOutside,spot));
            if(!ok)std::printf("unreachable seed %llu floors %u spot %f %f %f\n",(unsigned long long)seed,floors,spot.x.toDouble(),spot.y.toDouble(),spot.z.toDouble());CHECK(ok);}
    }
}
// A bot fetches loot from the top floor of a three-storey house (navmesh per floor + the stair flights).
void upstairsBot() {
    auto rules=Json::parse(R"({"fps":{"tickRate":30,"mode":"deathmatch","mechanics":{"enabled":true},"bots":{"preset":"normal"},
      "weapons":[{"nameKey":"fps.weapon.pistol"},{"nameKey":"fps.weapon.rifle"}]}})");
    auto cfg=fps::readConfig(rules);lib::systems::BuildingDef def;def.floors=3;
    def.agentRadius=cfg.motor.radius;def.agentHeight=cfg.motor.height;def.maxStepRise=fxMin(def.maxStepRise,cfg.motor.stepHeight);
    Vec3 origin{20_fx,{},20_fx};auto layout=*lib::systems::generateBuilding(def,4,origin-Vec3{{},def.slab,{}},0);layout.doorOutside.y={};
    auto at=[](Vec3 p){Json a=Json::array();a.push(Json::number(p.x));a.push(Json::number(p.y+Fixed::fromRatio(1,100)));a.push(Json::number(p.z));return a;};
    Json map=Json::parse(R"({"players":[{"team":1,"bot":true},{"team":2}],"colliders":[{"id":1,"min":[-200,-2,-200],"max":[200,0,200]}],
      "buildings":[{"pos":[20,0,20],"seed":4,"building":{"floors":3}}]})");
    Json loot=Json::array();Json item=Json::object();item.set("pos",at(layout.lootSpots.back()));item.set("type",Json::number(int64_t(101)));loot.push(item);map.set("loot",loot);
    auto away=layout.doorOutside-layout.door;away.y={};Json spawns=Json::array();spawns.push(at(layout.doorOutside+away*3_fx));spawns.push(at({-150_fx,{},-150_fx}));map.set("spawns",spawns);
    auto sim=simulation(rules,map);auto& s=state(*sim);s.actors[0].owned=1;CHECK(layout.lootSpots.back().y>def.floorHeight);
    for(int i=0;i<3000&&!s.loot.empty();++i){sim->step();}
    CHECK(s.loot.empty()&&(s.actors[0].owned&2u));
}
