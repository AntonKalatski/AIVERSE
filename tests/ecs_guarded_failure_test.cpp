// c++ -std=c++20 -O2 -pthread -I. tests/ecs_guarded_failure_test.cpp engine/sim/Simulation.cpp engine/core/Fixed.cpp engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp -o /tmp/aiverse-ecs-failure
// /tmp/aiverse-ecs-failure
#include "engine/experiments/EcsSimulationBridge.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace aiverse;
using namespace aiverse::ecs_experiment;
struct ValueA{int64_t x=0;};
struct ValueB{int64_t x=0;};
class FailingModule final : public GameModule {
    unsigned workers_;
public:
    explicit FailingModule(unsigned workers):workers_(workers){}
    std::string name() const override{return "ecs-failure";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        auto& world=sim.world();
        for(uint32_t i=0;i<100;++i){
            Entity e=world.create();
            world.add<ValueA>(e,ValueA{int64_t(i)});
            world.add<ValueB>(e,ValueB{int64_t(i*2)});
        }
        std::vector<GuardedSystem> systems;
        systems.push_back({{"write-a",{}, {componentId<ValueA>()},{},{},false,false},
            [](GuardedWorld& w){w.eachWrite<ValueA>([](Entity,ValueA& v){v.x+=100;});}});
        systems.push_back({{"throw-b",{}, {componentId<ValueB>()},{},{},false,false},
            [](GuardedWorld& w){
                w.eachWrite<ValueB>([](Entity,ValueB& v){v.x+=1000;});
                throw std::runtime_error("injected failure");
            }});
        addGuardedGroup(sim,"transactional-failure",std::move(systems),
            workers_,GuardedGroupOptions{true});
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation& sim,Hasher& hash) override {
        sim.world().each<ValueA,ValueB>([&](Entity e,ValueA& a,ValueB& b) {
            hash.u64(e.packed());hash.i64(a.x);hash.i64(b.x);
        });
    }
    int result(Simulation&) override{return 0;}
};
int main(){
    for(unsigned workerCount:{0u,3u,7u}){
        Simulation sim({1,20,2,true},std::make_unique<FailingModule>(workerCount));
        sim.setup(Json::object(),Json::object());
        const auto initial=sim.checksum();
        bool threw=false;
        try{sim.step();}catch(const std::runtime_error&){threw=true;}
        assert(threw && sim.tick()==0);
        assert(sim.checksum()==initial);
        for(int i=0;i<100;++i){
            Entity e{uint32_t(i),0};
            assert(sim.world().get<ValueA>(e).x==i);
            assert(sim.world().get<ValueB>(e).x==i*2);
        }
    }
    std::cout<<"PASS opt-in World transactional restoration on guarded worker failure\n";
}
