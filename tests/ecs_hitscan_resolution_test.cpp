// A real schedulable ECS hitscan resolution system in two unrelated hosts.
// Covers invalid and blocked fire intents, deterministic event ordering,
// one-shot consumption and deep/COW snapshot replay.
#include <aiverselib/genre/EcsHitscanResolutionSystems.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace aiverse;
namespace lg=aiverse::lib::genre;

static void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
struct ShooterStyle {};
struct StrategyStyle {};

struct HitTarget {
    Entity entity=kNullEntity;
    world::Bounds box{};
};
struct TargetStore {std::vector<HitTarget> targets;};
struct HitLog {
    std::vector<uint32_t> shots;
    std::vector<uint64_t> sources,targets;
};

template<class Style> class Host final:public GameModule {
public:
    std::string name()const override{return "ecs-hitscan-resolution";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        auto& w=sim.world();
        w.setResource<TargetStore>();
        w.setResource<HitLog>();
        const Entity victim=w.create();
        w.add<Style>(victim);
        w.resource<TargetStore>()->targets.push_back(
            {victim,{{-1_fx,0_fx,3_fx},{1_fx,2_fx,4_fx}}});
        for(uint32_t id=1;id<=5;++id) {
            const Entity shooter=w.create();
            lg::HitscanIntentComponent intent;
            intent.origin={0_fx,1_fx,0_fx};
            intent.delta={0_fx,0_fx,10_fx};
            intent.shotId=id;
            intent.requested=id!=5;
            if(id==2)intent.maximumFraction=Fixed::fromRatio(1,5); // wall
            if(id==3)intent.delta={}; // invalid request
            w.add<lg::HitscanIntentComponent>(shooter,intent);
        }
        lg::addHitscanResolutionSystem(sim,"hitscan.resolve",
            [](Simulation& current,Entity,const lg::HitscanIntentComponent&)
                -> const std::vector<HitTarget>& {
                return current.world().resourceConst<TargetStore>()->targets;
            },
            [](Simulation&,Entity,const HitTarget&) {return true;},
            [](Simulation&,const HitTarget& target) {return target.box;},
            [](Simulation&,const HitTarget& target) {return target.entity;},
            [](Simulation& current,const lg::HitscanResolvedEvent& hit) {
                auto* log=current.world().resource<HitLog>();
                require(log!=nullptr,"missing hit log");
                require(hit.fraction==Fixed::fromRatio(3,10),
                        "wrong closest hit fraction");
                require(hit.point.z==3_fx,"wrong instant impact position");
                log->shots.push_back(hit.shotId);
                log->sources.push_back(hit.shooter.packed());
                log->targets.push_back(hit.target.packed());
            });
    }
    void applyCommand(Simulation&,const Command&)override {}
    void hashState(Simulation& sim,Hasher& h)override {
        const World& w=sim.world();
        w.eachExistingConst<lg::HitscanIntentComponent>(
            [&](Entity e,const lg::HitscanIntentComponent& intent) {
                if(!w.alive(e))return;
                h.u64(e.packed());
                h.u32(intent.shotId);
                h.u32(intent.requested?1:0);
                h.fx(intent.maximumFraction);
            });
        const auto* log=w.resourceConst<HitLog>();
        require(log!=nullptr,"missing hit log for checksum");
        for(size_t i=0;i<log->shots.size();++i) {
            h.u32(log->shots[i]);
            h.u64(log->sources[i]);
            h.u64(log->targets[i]);
        }
    }
    int result(Simulation&)override{return 0;}
};

template<class Style> uint64_t exercise(bool cow) {
    Simulation sim(SimConfig{73,20,0,true},std::make_unique<Host<Style>>());
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    SimSnapshot before;
    sim.saveState(before);
    auto run=[&]() {
        sim.step();
        const auto* log=sim.world().resourceConst<HitLog>();
        require(log&&log->shots==std::vector<uint32_t>({1,4}),
                "resolved hits must be in shooter dense order");
        require(log->sources.size()==2&&log->sources[0]!=log->sources[1],
                "different shooter intents were not isolated");
        require(log->targets.size()==2&&log->targets[0]==log->targets[1],
                "target identity mismatch");
        const auto& w=sim.world();
        w.eachExistingConst<lg::HitscanIntentComponent>(
            [&](Entity,const lg::HitscanIntentComponent& intent) {
                require(!intent.requested,
                        "one-shot fire intent was not consumed");
            });
        sim.step();
        require(sim.world().resourceConst<HitLog>()->shots.size()==2,
                "consumed intent fired a second time");
        return sim.checksum();
    };
    const auto expected=run();
    sim.loadState(before);
    require(run()==expected,"hitscan snapshot/rollback mismatch");
    return expected;
}

int main() {
    const auto baseline=exercise<ShooterStyle>(false);
    require(exercise<StrategyStyle>(false)==baseline,
            "strategy host hitscan mismatch");
    require(exercise<ShooterStyle>(true)==baseline,
            "shooter COW replay mismatch");
    require(exercise<StrategyStyle>(true)==baseline,
            "strategy COW replay mismatch");
    std::cout<<"PASS generic ECS hitscan resolution ordering and rollback\n";
}
