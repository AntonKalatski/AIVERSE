// Cross-genre compile/runtime contract: the exact same ECS mechanic policies
// run in two unrelated GameModule/component types with no RTS/FPS includes.
// Does NOT imply all existing RtsModule/fps::Module methods are universal.
#include "libraries/include/aiverselib/genre/EcsMechanicSystems.h"
#include "libraries/include/aiverselib/genre/Locomotion.h"
#include "libraries/include/aiverselib/genre/Weapons.h"
#include "libraries/include/aiverselib/genre/Match.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
using namespace aiverse;
namespace lg=aiverse::lib::genre;

struct MechanicData {
    lg::MotionState motion;
    lg::RecoilState recoil;
};
struct ShooterCharacter {MechanicData gameplay;};
struct StrategySoldier {MechanicData gameplay;};

template<class Component>
class GenreHost final:public GameModule {
public:
    std::string name() const override{return "generic-policy-host";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        World& world=sim.world();
        for(int i=0;i<32;++i) {
            Entity entity=world.create();
            world.add<Component>(entity);
        }
        world.setResource<lg::CaptureState>();
        // The SAME movement and recoil policies, regardless of the genre or
        // name/layout of the component stored in the ECS pool.
        lg::addEcsMechanicSystem<Component>(sim,"shared-locomotion-and-recoil",
            [](Simulation& s,Entity,Component& c) {
                lg::MotionConfig config;
                lg::MotionInput input;
                input.wish={1_fx,Fixed{}};
                input.grounded=true;
                const auto result=lg::stepMotion(config,c.gameplay.motion,
                                                  input,s.tick(),s.dt());
                assert(result.has_value());
                const bool accepted=lg::stepRecoil(
                    lg::RecoilConfig{},c.gameplay.recoil,s.tick()%3==0);
                assert(accepted);
            });
        sim.addSystem("shared-capture-objective",[](Simulation& s) {
            auto* capture=s.world().resource<lg::CaptureState>();
            assert(capture);
            std::map<int64_t,uint32_t> occupants{{1,32}};
            const auto result=lg::stepCapture(*capture,occupants,4,s.tick());
            assert(result.has_value());
        });
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation& sim,Hasher& hash) override {
        const World& world=sim.world();
        world.eachExistingConst<Component>([&](Entity e,const Component& actor) {
            hash.u64(e.packed());
            const auto& m=actor.gameplay;
            hash.i64(m.motion.velocity.x.raw);
            hash.i64(m.motion.velocity.y.raw);
            hash.i64(m.recoil.yaw.raw);
            hash.i64(m.recoil.pitch.raw);
        });
        const auto* objective=world.resourceConst<lg::CaptureState>();
        assert(objective);
        hash.i64(objective->owner);
        hash.i64(objective->candidate);
        hash.u32(objective->progress);
    }
    int result(Simulation&) override{return 0;}
};

template<class T>
uint64_t runGenre(bool cow) {
    Simulation sim(SimConfig{73,20,0,true},
                   std::make_unique<GenreHost<T>>());
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    for(int i=0;i<10;++i)sim.step();
    SimSnapshot snapshot;
    sim.saveState(snapshot);
    for(int i=0;i<10;++i)sim.step();
    const auto expected=sim.checksum();
    sim.loadState(snapshot);
    for(int i=0;i<10;++i)sim.step();
    assert(sim.checksum()==expected);
    return expected;
}
int main(){
    const auto baseline=runGenre<ShooterCharacter>(false);
    assert(runGenre<StrategySoldier>(false)==baseline);
    assert(runGenre<ShooterCharacter>(true)==baseline);
    assert(runGenre<StrategySoldier>(true)==baseline);
    std::cout<<"PASS cross-genre ECS locomotion/recoil/capture policies, COW rollback and checksum parity\n";
}
