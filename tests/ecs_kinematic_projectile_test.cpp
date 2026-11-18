// Kinematic projectile system: fixed-tick travel and replay in unrelated hosts.
#include <aiverselib/genre/EcsKinematicProjectileSystems.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace aiverse;
namespace lg=aiverse::lib::genre;
static void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
struct StrategyTag {};
struct ShooterTag {};
struct ArrivalLog {std::vector<uint32_t> ids;std::vector<int64_t> x;};
template<class Tag> class Host final:public GameModule {
public:
    std::string name()const override{return "kinematic-projectile-host";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        auto& world=sim.world();
        world.setResource<ArrivalLog>();
        const Entity owner=world.create();
        world.add<Tag>(owner);
        for(uint32_t i=0;i<3;++i) {
            const Entity e=world.create();
            lg::KinematicProjectileComponent shot;
            shot.owner=owner;
            shot.shotId=100+i;
            shot.origin={Fixed{},Fixed{},Fixed{}};
            shot.destination={Fixed::fromInt(int64_t(3+i)),Fixed{},Fixed{}};
            shot.duration=2+i;
            world.add<lg::KinematicProjectileComponent>(e,shot);
        }
        lg::addKinematicProjectileSystem(sim,"kinematic",
            [](Simulation& current,const lg::KinematicArrivalEvent& event) {
                auto* log=current.world().resource<ArrivalLog>();
                check(log!=nullptr,"missing arrival log");
                log->ids.push_back(event.shotId);
                log->x.push_back(event.position.x.raw);
            });
    }
    void applyCommand(Simulation&,const Command&)override {}
    void hashState(Simulation& sim,Hasher& h)override {
        const auto& world=sim.world();
        world.eachExistingConst<lg::KinematicProjectileComponent>(
            [&](Entity e,const lg::KinematicProjectileComponent& shot) {
                if(!world.alive(e))return;
                h.u64(e.packed());h.u32(shot.shotId);
                h.u32(shot.elapsed);h.u32(shot.duration);
                h.i64(shot.position.x.raw);h.u32(shot.alive?1:0);
            });
        const auto* log=world.resourceConst<ArrivalLog>();
        check(log!=nullptr,"missing arrival log");
        for(size_t i=0;i<log->ids.size();++i) {
            h.u32(log->ids[i]);h.i64(log->x[i]);
        }
    }
    int result(Simulation&)override{return 0;}
};
template<class Tag> uint64_t exercise(bool cow) {
    Simulation sim(SimConfig{51,20,0,true},std::make_unique<Host<Tag>>());
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    sim.step();
    SimSnapshot checkpoint;
    sim.saveState(checkpoint);
    for(int i=0;i<4;++i)sim.step();
    const auto* log=sim.world().resourceConst<ArrivalLog>();
    check(log&&log->ids.size()==3,"incorrect arrival count");
    for(size_t i=0;i<3;++i) {
        check(log->ids[i]==100+i,"arrival order mismatch");
        check(log->x[i]==Fixed::fromInt(int64_t(3+i)).raw,"arrival position mismatch");
    }
    check(sim.world().countExisting<lg::KinematicProjectileComponent>()==0,
          "completed projectiles not despawned");
    const auto expected=sim.checksum();
    sim.loadState(checkpoint);
    for(int i=0;i<4;++i)sim.step();
    check(sim.checksum()==expected,"snapshot replay mismatch");
    return expected;
}
int main() {
    const auto baseline=exercise<StrategyTag>(false);
    check(exercise<ShooterTag>(false)==baseline,"cross-host mismatch");
    check(exercise<StrategyTag>(true)==baseline,"COW strategy mismatch");
    check(exercise<ShooterTag>(true)==baseline,"COW shooter mismatch");
    std::cout<<"PASS kinematic ECS projectiles and rollback across hosts\n";
}
