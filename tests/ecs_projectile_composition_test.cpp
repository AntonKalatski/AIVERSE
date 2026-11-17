// Same flight component/system + collision policy in two independent genre
// modules, with canonical impact events, COW snapshot and replay parity.
// No RtsModule, fps::Module or genre-specific Actor dependency.
#include <aiverselib/genre/EcsProjectileSystems.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>
using namespace aiverse;
namespace lg=aiverse::lib::genre;

struct ShooterWeaponSource {int32_t magazine=20;};
struct StrategyTurretSource {int32_t range=200;};
struct ImpactLog {
    std::vector<uint32_t> shots;
    std::vector<uint32_t> walls;
};

template<class Source>
class Host final : public GameModule {
    world::SpatialWorld collision_;
public:
    std::string name() const override {return "cross-genre-projectiles";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        World& w=sim.world();
        // The same collision provider is used by either genre.
        assert(collision_.add({42,{{-2_fx,0_fx,5_fx},{2_fx,3_fx,6_fx}}}));
        w.setResource<ImpactLog>();
        Entity owner=w.create();
        w.add<Source>(owner);
        for(int i=0;i<4;++i) {
            Entity projectile=w.create();
            lg::ProjectileFlightComponent data;
            data.config.gravity=Fixed{};
            data.config.lifetime=60;
            data.config.maxBounces=0;
            data.config.radius=Fixed::fromRatio(1,20);
            data.state.position={Fixed::fromInt(i*2-3),1_fx,Fixed{}};
            data.state.velocity={Fixed{},Fixed{},20_fx};
            data.owner=owner;
            data.shotId=uint32_t(100+i);
            w.add<lg::ProjectileFlightComponent>(projectile,data);
        }
        lg::addProjectileFlightSystem(sim,"universal.projectile.flight",
            [this](Simulation&) -> const world::SpatialWorld& {return collision_;},
            [](Simulation& current,const lg::ProjectileImpactEvent& event) {
                auto* log=current.world().resource<ImpactLog>();
                assert(log);
                log->shots.push_back(event.shotId);
                log->walls.push_back(event.contact.collider);
            });
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation& sim,Hasher& h) override {
        const World& w=sim.world();
        w.eachExistingConst<lg::ProjectileFlightComponent>(
            [&](Entity e,const lg::ProjectileFlightComponent& v) {
                if(!w.alive(e))return;
                h.u64(e.packed());
                h.u32(v.shotId);
                h.i64(v.state.position.x.raw);
                h.i64(v.state.position.y.raw);
                h.i64(v.state.position.z.raw);
                h.i64(v.state.velocity.z.raw);
                h.u32(v.state.age);
            });
        const auto* log=w.resourceConst<ImpactLog>();
        assert(log);
        for(size_t i=0;i<log->shots.size();++i) {
            h.u32(log->shots[i]);
            h.u32(log->walls[i]);
        }
    }
    int result(Simulation&) override{return 0;}
};

template<class Source>
uint64_t test(bool cow) {
    Simulation sim(SimConfig{71,20,0,true},std::make_unique<Host<Source>>());
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    for(int i=0;i<3;++i)sim.step();
    SimSnapshot checkpoint;
    sim.saveState(checkpoint);
    for(int i=0;i<7;++i)sim.step();
    const auto expected=sim.checksum();
    const auto* log=sim.world().resourceConst<ImpactLog>();
    assert(log && log->shots.size()==2);
    assert(log->shots[0]==101 && log->shots[1]==102);
    assert(log->walls[0]==42 && log->walls[1]==42);
    sim.loadState(checkpoint);
    for(int i=0;i<7;++i)sim.step();
    assert(sim.checksum()==expected);
    return expected;
}
int main(){
    const uint64_t shooter=test<ShooterWeaponSource>(false);
    assert(test<StrategyTurretSource>(false)==shooter);
    assert(test<ShooterWeaponSource>(true)==shooter);
    assert(test<StrategyTurretSource>(true)==shooter);
    std::cout<<"PASS shared projectile ECS system in shooter and strategy modules, impact order and COW replay\n";
}
