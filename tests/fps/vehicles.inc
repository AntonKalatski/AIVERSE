// fps.vehicleTypes: typed vehicles over vehicle-controllers (controller, seats, collision, passengers firing, saves).
void typedVehicles(const std::string& root) {
    auto types=Json::parseFile(root+"/../../libraries/mechanics/vehicles-sports/vehicles.json");
    auto rules=Json::parse(R"({"fps":{"tickRate":20,"mode":"deathmatch","mechanics":{"enabled":true}}})");
    Json only=Json::object();Json list=Json::array();list.push(types["vehicles"][0]);only.set("vehicles",list);
    rules.member("fps").set("vehicleTypes",only);
    auto map=Json::parse(R"({"players":[{"team":1},{"team":1},{"team":2}],"spawns":[[-1.5,0.01,5],[1.5,0.01,5],[30,0.01,-30]],
      "colliders":[{"id":1,"min":[-200,-2,-200],"max":[200,0,200]},{"id":2,"min":[-20,0,80],"max":[20,4,82]}],
      "vehicles":[{"pos":[0,0.01,5],"type":1},{"pos":[40,0.01,-40]}]})");
    auto sim=simulation(rules,map),twin=simulation(rules,map);
    CHECK(sim->world().countExisting<fps::VehicleDrive>()==1&&vehiclesOf(*sim).size()==2);
    uint32_t seq=0;
    auto send=[&](uint8_t slot,fps::Input i){i.sequence=++seq;i.viewTick=sim->tick();auto c=fps::command(slot,i);c.tick=sim->tick();sim->inject(c);twin->inject(c);};
    auto step=[&](int n){for(int i=0;i<n;++i){sim->step();twin->step();CHECK(sim->checksum()==twin->checksum());}};
    fps::Input enter;enter.action=fps::Action::Interact;
    send(1,enter);step(1);CHECK(character(*sim,1).vehicle==vehiclesOf(*sim)[0].id&&vehiclesOf(*sim)[0].driver==1);
    send(2,enter);step(1);CHECK(character(*sim,2).vehicle==vehiclesOf(*sim)[0].id);
    auto start=vehiclesOf(*sim)[0].pos;
    for(int i=0;i<80;++i){fps::Input d;d.forward=1000;d.buttons=fps::Fire;send(1,d);fps::Input p;p.buttons=fps::Fire;p.yaw=Fixed::fromRatio(1,4);send(2,p);step(1);
        if(i==40){auto bytes=sim->serialize();auto copy=simulation(rules,map);CHECK(copy->deserialize(bytes)&&copy->checksum()==sim->checksum());}}
    CHECK(vehiclesOf(*sim)[0].pos.z>start.z+10_fx);                       // drove forward (+Z at yaw 0)
    CHECK(character(*sim,1).shots==0&&character(*sim,2).shots>0);               // the driver steers, the passenger shoots
    CHECK((character(*sim,2).pos-vehiclesOf(*sim)[0].pos).length()<3_fx);       // the passenger rides along
    for(int i=0;i<400;++i){fps::Input d;d.forward=1000;send(1,d);step(1);}
    CHECK(vehiclesOf(*sim)[0].pos.z<80_fx);                               // the wall stops the car
    for(int i=0;i<120;++i){fps::Input d;d.forward=-1000;send(1,d);step(1);}
    for(int i=0;i<400;++i){auto vz=vehiclesOf(*sim)[0].velocity.z;if(fxAbs(vz)<Fixed::fromRatio(1,2))break;fps::Input d;d.forward=vz>Fixed{}?-1000:1000;send(1,d);step(1);}   // brake to a stop: exits need a slow car
    send(1,enter);step(1);CHECK(character(*sim,1).vehicle==0&&(character(*sim,1).pos-vehiclesOf(*sim)[0].pos).length()<4_fx);
    CHECK(vehiclesOf(*sim)[0].driver==0||vehiclesOf(*sim)[0].driver==2);
}
