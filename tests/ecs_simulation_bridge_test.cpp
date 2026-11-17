// Real Simulation::step() and SimSnapshot rollback regression for guarded groups.
// c++ -std=c++20 -O2 -pthread -I. tests/ecs_simulation_bridge_test.cpp engine/sim/Simulation.cpp engine/core/Fixed.cpp engine/data/Json.cpp -o /tmp/aiverse-ecs-sim
// /tmp/aiverse-ecs-sim
#include "engine/experiments/EcsSimulationBridge.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace aiverse;
using namespace aiverse::ecs_experiment;
struct Position { int64_t x=0; };
struct Velocity { int64_t v=1; };
struct Score { int64_t value=0; };
struct Counter { int64_t value=1; };

class ToyModule final : public GameModule {
    unsigned workerCount_;
public:
    explicit ToyModule(unsigned workerCount):workerCount_(workerCount){}
    std::string name() const override{return "ecs-experiment-toy";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        World& world=sim.world();
        for(uint32_t i=0;i<1024;++i) {
            Entity e=world.create();
            world.add<Position>(e,Position{int64_t(i)});
            world.add<Velocity>(e,Velocity{int64_t(i%3+1)});
            world.add<Score>(e);
        }
        world.setResource<Counter>(Counter{1});
        const AccessId p=componentId<Position>(),v=componentId<Velocity>();
        const AccessId score=componentId<Score>(),counter=componentId<Counter>();
        std::vector<GuardedSystem> systems;
        systems.push_back({{"move",{v},{p},{},{},false,false},
            [](GuardedWorld& w) {
                w.eachWrite<Position>([&](Entity e,Position& pos) {
                    if(const auto* vel=w.read<Velocity>(e))pos.x+=vel->v;
                });
            }});
        systems.push_back({{"counter",{}, {}, {}, {counter},false,false},
            [](GuardedWorld& w) {
                auto* c=w.writeResource<Counter>();
                assert(c);c->value+=1;
            }});
        systems.push_back({{"score",{p},{score},{counter},{},false,false},
            [](GuardedWorld& w) {
                const auto* counterValue=w.readResource<Counter>();
                assert(counterValue);
                w.eachWrite<Score>([&](Entity e,Score& out) {
                    const auto* pos=w.read<Position>(e);
                    assert(pos);
                    out.value=pos->x+counterValue->value;
                });
            }});
        addGuardedGroup(sim,"experimental-guarded-wave",std::move(systems),workerCount_);
    }
    void applyCommand(Simulation& sim,const Command& command) override {
        Counter* c=sim.world().resource<Counter>();
        assert(c);
        if(command.type==1)c->value+=command.param;
        else if(command.type==2)c->value*=command.param;
    }
    void hashState(Simulation& sim,Hasher& hash) override {
        auto& world=sim.world();
        world.each<Position,Velocity>([&](Entity e,Position& pos,Velocity& vel) {
            hash.u64(e.packed());hash.i64(pos.x);hash.i64(vel.v);
            hash.i64(world.get<Score>(e).value);
        });
        auto* counter=world.resource<Counter>();
        assert(counter);
        hash.i64(counter->value);
        const auto state=world.entityState();
        for(auto g:state.gens)hash.u32(g);
        for(auto id:state.free)hash.u32(id);
    }
    int result(Simulation&) override{return 0;}
};
static void submitOutOfOrder(Simulation& sim,uint32_t tick) {
    Command second;second.tick=tick;second.player=2;second.seq=1;
    second.type=2;second.param=2;
    Command first;first.tick=tick;first.player=1;first.seq=1;
    first.type=1;first.param=3;
    sim.inject(second);
    sim.inject(first);
}
static uint64_t simulate(unsigned workers,bool rollback) {
    Simulation sim(SimConfig{2026,20,2,true},std::make_unique<ToyModule>(workers));
    sim.setup(Json::object(),Json::object());
    // Commands arrive in reverse order; Simulation sorts by (player, seq).
    submitOutOfOrder(sim,0);
    for(int i=0;i<4;++i)sim.step();
    SimSnapshot snap;
    sim.saveState(snap);
    const auto snapHash=sim.checksum();
    for(int i=0;i<6;++i)sim.step();
    const auto expected=sim.checksum();
    if(rollback) {
        sim.loadState(snap);
        assert(sim.tick()==4 && sim.checksum()==snapHash);
        for(int i=0;i<6;++i)sim.step();
        assert(sim.checksum()==expected);
    }
    return sim.checksum();
}
static void testAccessRejection() {
    Simulation sim({},std::make_unique<ToyModule>(0));
    bool rejected=false;
    try {
        std::vector<GuardedSystem> bad{
            {{"bad",{}, {}, {}, {},true,false},[](GuardedWorld&){}}
        };
        addGuardedGroup(sim,"reject-structural",std::move(bad),0);
    } catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
}
int main() {
    testAccessRejection();
    const auto expected=simulate(0,false);
    for(unsigned workerCount:{0u,1u,3u,7u}) {
        assert(simulate(workerCount,false)==expected);
        assert(simulate(workerCount,true)==expected);
    }
    std::cout<<"PASS Simulation step, canonical commands, snapshot rollback, 0/1/3/7-worker checksums\n";
}
