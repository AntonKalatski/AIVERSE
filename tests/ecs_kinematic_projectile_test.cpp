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
struct MovingTarget {Vec3 position{};};
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
        // The endpoint is re-resolved every tick while pursuing a moving unit.
        const Entity target=world.create();
        world.add<MovingTarget>(target,MovingTarget{{2_fx,Fixed{},Fixed{}}});
        const Entity homing=world.create();
        lg::KinematicProjectileComponent chase;
        chase.owner=owner;
        chase.target=target;
        chase.shotId=200;
        chase.destination={2_fx,Fixed{},Fixed{}};
        chase.duration=10;
        chase.distancePerTick=1_fx;
        chase.mode=lg::KinematicMotionMode::Pursuit;
        world.add<lg::KinematicProjectileComponent>(homing,chase);
        // Timeout must despawn without pretending to hit a distant target.
        const Entity timeout=world.create();
        lg::KinematicProjectileComponent expiring=chase;
        expiring.shotId=201;
        expiring.target=kNullEntity;
        expiring.destination={100_fx,Fixed{},Fixed{}};
        expiring.duration=2;
        world.add<lg::KinematicProjectileComponent>(timeout,expiring);
        sim.addSystem("move-target",[](Simulation& current) {
            current.world().each<MovingTarget>([](Entity,MovingTarget& t) {
                t.position.x+=Fixed::fromRatio(1,4);
            });
        });
        lg::addKinematicProjectileSystem(sim,"kinematic",
            [](Simulation& current,const lg::KinematicArrivalEvent& event) {
                auto* log=current.world().resource<ArrivalLog>();
                check(log!=nullptr,"missing arrival log");
                log->ids.push_back(event.shotId);
                log->x.push_back(event.position.x.raw);
            },
            [](Simulation& current,const lg::KinematicProjectileComponent& shot) {
                if(!shot.target.valid())return shot.destination;
                const auto* target=current.world().tryGetConst<MovingTarget>(shot.target);
                return target ? target->position : shot.destination;
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
        world.eachExistingConst<MovingTarget>([&](Entity e,const MovingTarget& t) {
            h.u64(e.packed());h.i64(t.position.x.raw);
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
    check(log&&log->ids.size()==4,"incorrect arrival count");
    // At tick 3, the second timed projectile arrives before the pursuit
    // projectile in the ECS component pool's canonical dense order.
    const uint32_t order[]={100,101,200,102};
    const Fixed expectedX[]={3_fx,4_fx,Fixed::fromRatio(11,4),5_fx};
    for(size_t i=0;i<4;++i) {
        check(log->ids[i]==order[i],"arrival order mismatch");
        check(log->x[i]==expectedX[i].raw,"arrival position mismatch");
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
    // Validation failure must leave the component byte-level state alone.
    lg::KinematicProjectileComponent invalid;
    invalid.mode=lg::KinematicMotionMode::Pursuit;
    invalid.duration=8;
    invalid.distancePerTick=Fixed{};
    check(!lg::stepKinematicProjectile(invalid),"accepted a zero-speed pursuit");
    check(invalid.elapsed==0 && invalid.alive && !invalid.arrived,
          "invalid pursuit unexpectedly mutated runtime state");
    const auto baseline=exercise<StrategyTag>(false);
    check(exercise<ShooterTag>(false)==baseline,"cross-host mismatch");
    check(exercise<StrategyTag>(true)==baseline,"COW strategy mismatch");
    check(exercise<ShooterTag>(true)==baseline,"COW shooter mismatch");
    std::cout<<"PASS kinematic ECS projectiles and rollback across hosts\n";
}
