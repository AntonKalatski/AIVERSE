// Real RollbackSession parity with incremental COW snapshots OFF and ON.
// Build via tests/run_ecs_experiment_suite.sh.
#include "engine/net/Rollback.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>
using namespace aiverse;
using namespace aiverse::net;
struct Position {int64_t x=0;};
struct Velocity {int64_t v=1;};
struct Score {int64_t points=0;};
class Toy : public GameModule {
public:
    std::string name() const override{return "cow-rollback";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        for(uint32_t i=0;i<1024;++i) {
            auto e=sim.world().create();
            sim.world().add<Position>(e,Position{int64_t(i)});
            sim.world().add<Velocity>(e,Velocity{int64_t(i%4+1)});
        }
        sim.world().setResource<Score>();
        sim.addSystem("update",[](Simulation& sim){
            auto& w=sim.world();
            w.each<Position,Velocity>([](Entity,Position& p,Velocity& v){p.x+=v.v;});
            w.resource<Score>()->points+=1;
        });
    }
    void applyCommand(Simulation& sim,const Command& cmd) override {
        sim.world().resource<Score>()->points+=cmd.param;
    }
    void hashState(Simulation& sim,Hasher& h) override {
        sim.world().each<Position,Velocity>([&](Entity e,Position& p,Velocity& v){
            h.u64(e.packed());h.i64(p.x);h.i64(v.v);
        });
        h.i64(sim.world().resource<Score>()->points);
    }
    int result(Simulation&) override{return 0;}
};
struct Result {uint64_t hash;uint32_t tick,rollbacks;};
static Result run(bool incremental) {
    Simulation sim({99,20,1,true},std::make_unique<Toy>());
    sim.setup(Json::object(),Json::object());
    SessionConfig cfg;
    cfg.rollbackWindow=8;
    cfg.inputDelay=1;
    cfg.incrementalWorldSnapshots=incremental;
    {
        RollbackSession session(sim,cfg,{1},
            [](uint32_t,const std::vector<Command>&){},
            [](uint32_t,uint64_t){});
        assert(sim.world().cowSnapshotsEnabled()==incremental);
        for(int i=0;i<5;++i)session.update(0.05,uint64_t(i*50));
        assert(sim.tick()==5);
        Command delayed;
        delayed.tick=2;delayed.player=2;delayed.seq=1;delayed.type=1;delayed.param=37;
        session.confirm(0,3,{delayed});
        // Update with zero seconds still applies correction and replay.
        session.update(0.0,250);
        assert(session.stats().rollbacks>=1);
        for(int i=5;i<8;++i)session.update(0.05,uint64_t(i*50));
        return {sim.checksum(),sim.tick(),session.stats().rollbacks};
    }
}
int main(){
    const auto deep=run(false),cow=run(true);
    assert(deep.hash==cow.hash);
    assert(deep.tick==cow.tick && deep.rollbacks==cow.rollbacks);
    std::cout<<"PASS RollbackSession deep-vs-incremental COW parity including late confirmations\n";
}
