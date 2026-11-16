// fps.royale: battle-royale package wiring (insertion, phased zone, supply drops, standings, saves, snapshots).
void royale() {
    auto rules=Json::parse(R"({"fps":{"tickRate":20,"mode":"battleRoyale","worldLimit":2000,"roundTicks":20000,
      "mechanics":{"enabled":true,"knockdown":true},
      "royale":{"bounds":{"min":[-200,-200],"max":[200,200]},
        "zone":{"radius":300,"startTick":600,"phases":[{"wait":20,"shrink":40,"radius":100,"damage":5,"interval":5},{"wait":10,"shrink":20,"radius":0,"damage":50,"interval":5}]},
        "flight":{"speed":4,"altitude":100,"margin":20,"crossRadius":0,"doorOpen":5},
        "drop":{"autoDeployHeight":30,"minDeployHeight":10},
        "supplyDrops":{"firstTick":10,"interval":100,"maxDrops":1,"fall":20,"altitude":50}}}})");
    auto map=Json::parse(R"({"players":[{"team":1,"bot":true},{"team":1,"bot":true},{"team":2,"bot":true},{"team":2,"bot":true}],
      "spawns":[[-50,0.01,-50],[-40,0.01,-50],[50,0.01,50],[40,0.01,50]],"colliders":[{"id":1,"min":[-1000,-2,-1000],"max":[1000,0,1000]}]})");
    auto bad=rules;bad.member("fps").set("mode",Json::string("deathmatch"));
    try{fps::readConfig(bad);CHECK(false);}catch(const JsonError& e){CHECK(std::string(e.what()).find("fps.royale: requires")!=std::string::npos);}
    auto sim=simulation(rules,map),twin=simulation(rules,map);
    auto& s=state(*sim);CHECK(s.royale.enabled&&s.royale.divers.size()==4&&s.actors[0].pos.y==100_fx);
    auto& module=static_cast<fps::Module&>(sim->module());
    auto view=module.royaleView(*sim,1);CHECK(view.enabled&&view.divePhase==0&&view.alive==4&&view.teamsIn==2&&view.target.radius==300_fx);
    // snapshot round trip carries the royale view
    net::authority::Snapshot snap;snap.tick=1;snap.localSlot=1;snap.actors={s.actors[0]};snap.royale=view;
    net::authority::Snapshot back;CHECK(net::authority::decodeSnapshot(net::authority::encodeSnapshot(snap),back)&&back.royale==view);
    size_t loot=s.loot.size();bool landedAny=false,crate=false;uint32_t restoreAt=40;
    for(uint32_t t=0;t<20000&&!sim->result();++t){
        sim->step();twin->step();CHECK(sim->checksum()==twin->checksum());
        for(const auto& d:s.royale.divers)landedAny|=d.phase==lib::genre::SkydivePhase::Landed;
        if(s.loot.size()>loot)crate=true;
        for(const auto& a:s.actors)if(!(s.royale.divers[a.slot-1].phase==lib::genre::SkydivePhase::Landed))CHECK(a.pos.y>=Fixed{});
        if(sim->tick()==restoreAt){
            auto bytes=sim->serialize();auto copy=simulation(rules,map);CHECK(copy->deserialize(bytes)&&copy->checksum()==sim->checksum());
            auto probe=simulation(rules,map);CHECK(probe->deserialize(bytes));
            for(int i=0;i<200;++i){probe->step();copy->step();CHECK(probe->checksum()==copy->checksum());}
        }
    }
    CHECK(landedAny&&crate&&sim->result()!=0);
    const auto& st=s.royale.standings;CHECK(st.finished&&(st.winner==sim->result()||(st.winner==-1&&sim->result()==-1)));
    for(const auto& a:s.actors){auto life=st.players.at(a.slot).life;CHECK((a.health<=0)==(life==lib::genre::RoyaleLife::Out));}
    if(st.winner>0)CHECK(st.teams.at(st.winner).placement==1);
    std::printf("royale: winner %d after %u ticks\n",sim->result(),sim->tick());
}
