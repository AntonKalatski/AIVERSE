// Proof that RTS-like artillery and FPS-like ballistics run the same ECS
// flight, collision, damage queue and resolution systems, with identical
// deterministic replay and COW rollback. No genre module headers or linkage.
#include <aiverselib/genre/EcsDamageSystems.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

using namespace aiverse;
namespace lg=aiverse::lib::genre;

struct ShooterTag{};
struct ArtilleryTag{};
struct ImpactReport {uint32_t count=0, lastShot=0;int32_t totalDamage=0;};

template<class Tag>
class ProjectileHost final:public GameModule {
    world::SpatialWorld collision_{4};
public:
    Entity target=kNullEntity,shot=kNullEntity;
    std::string name()const override{return "universal-projectile";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        World& w=sim.world();
        const world::Collider wall{42,{{3_fx,-1_fx,-1_fx},{4_fx,1_fx,1_fx}}};
        assert(collision_.add(wall));
        target=w.create();
        shot=w.create();
        w.add<Tag>(target);
        w.add<lg::HitPointsComponent>(target,lg::HitPointsComponent{100,100});
        lg::ProjectileFlightComponent projectile;
        projectile.config.gravity=Fixed{};
        projectile.config.radius=Fixed{};
        projectile.config.maxBounces=0;
        projectile.config.lifetime=100;
        projectile.state.position={Fixed{},Fixed{},Fixed{}};
        projectile.state.velocity={20_fx,Fixed{},Fixed{}};
        projectile.owner=target;
        projectile.shotId=19;
        w.add<lg::ProjectileFlightComponent>(shot,projectile);
        w.add<lg::ProjectileDamageComponent>(shot,lg::ProjectileDamageComponent{35});
        lg::ColliderEntityIndex index;
        index.byCollider.emplace(42,target);
        w.setResource<lg::ColliderEntityIndex>(index);
        w.setResource<lg::DamageQueue>();
        w.setResource<ImpactReport>();
        lg::addProjectileFlightSystem(sim,"shared-projectile-flight",
            [this](Simulation&)->const world::SpatialWorld& {return collision_;},
            [](Simulation& s,const lg::ProjectileImpactEvent& hit) {
                lg::enqueueProjectileDamage(s,hit);
            });
        lg::addDamageResolutionSystem(sim,"shared-damage-commit",
            [](Simulation& s,const lg::DamageApplied& applied) {
                auto& report=*s.world().resource<ImpactReport>();
                ++report.count;
                report.lastShot=applied.shotId;
                report.totalDamage+=applied.applied;
            });
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation& sim,Hasher& hash)override {
        const World& w=sim.world();
        w.eachExistingConst<lg::HitPointsComponent>([&](Entity e,const lg::HitPointsComponent& hp) {
            hash.u64(e.packed());hash.i64(hp.current);hash.i64(hp.maximum);
        });
        w.eachExistingConst<lg::ProjectileFlightComponent>([&](Entity e,const lg::ProjectileFlightComponent& shot) {
            hash.u64(e.packed());
            hash.u32(shot.shotId);
            hash.i64(shot.state.position.x.raw);
            hash.i64(shot.state.velocity.x.raw);
            hash.u32(shot.state.age);
            hash.u32(shot.state.alive?1:0);
        });
        const auto* report=w.resourceConst<ImpactReport>();
        assert(report);
        hash.u32(report->count);
        hash.u32(report->lastShot);
        hash.i64(report->totalDamage);
    }
    int result(Simulation&)override{return 0;}
};

template<class Tag>
uint64_t run(bool cow) {
    auto host=std::make_unique<ProjectileHost<Tag>>();
    auto* game=host.get();
    Simulation sim(SimConfig{7,20,0,true},std::move(host));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    SimSnapshot start;
    sim.saveState(start);
    for(int i=0;i<8;++i)sim.step();
    const World& w=sim.world();
    const auto* hp=w.tryGetConst<lg::HitPointsComponent>(game->target);
    const auto* report=w.resourceConst<ImpactReport>();
    assert(hp && hp->current==65);
    assert(report && report->count==1 && report->totalDamage==35 && report->lastShot==19);
    assert(!w.alive(game->shot));
    const auto expected=sim.checksum();

    sim.loadState(start);
    assert(sim.world().alive(game->shot));
    assert(sim.world().tryGetConst<lg::HitPointsComponent>(game->target)->current==100);
    for(int i=0;i<8;++i)sim.step();
    assert(sim.checksum()==expected);
    return expected;
}

int main() {
    const auto shooter=run<ShooterTag>(false);
    assert(run<ArtilleryTag>(false)==shooter);
    assert(run<ShooterTag>(true)==shooter);
    assert(run<ArtilleryTag>(true)==shooter);
    std::cout<<"PASS shared ECS bullet/artillery flight, collision, ordered damage and COW rollback\n";
}
