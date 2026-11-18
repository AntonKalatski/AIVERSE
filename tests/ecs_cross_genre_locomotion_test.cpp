// Common ECS locomotion policy in unrelated shooter and strategy hosts.
#include <aiverselib/genre/EcsLocomotionSystems.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace lg=aiverse::lib::genre;
static void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct ShooterTag {};
struct StrategyTag {};

template<class Tag>
class Host final:public GameModule {
public:
    Entity mover=kNullEntity;
    std::string name()const override{return "ecs-shared-locomotion";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        auto& world=sim.world();
        mover=world.create();
        world.add<Tag>(mover);
        world.add<lg::LocomotionParametersComponent>(mover);
        world.add<lg::LocomotionStateComponent>(mover);
        lg::MotionInput intent;
        intent.wish={1_fx,Fixed{}};
        intent.grounded=true;
        intent.jumpPressed=true;
        world.add<lg::LocomotionInputComponent>(mover,lg::LocomotionInputComponent{intent});
        world.add<lg::LocomotionOutputComponent>(mover);
        lg::addLocomotionPolicySystem(sim,"shared-character-motion");
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation& sim,Hasher& hash)override {
        const auto& world=sim.world();
        const auto* s=world.tryGetConst<lg::LocomotionStateComponent>(mover);
        const auto* i=world.tryGetConst<lg::LocomotionInputComponent>(mover);
        const auto* o=world.tryGetConst<lg::LocomotionOutputComponent>(mover);
        require(s&&i&&o,"missing ECS locomotion components");
        hash.i64(s->value.velocity.x.raw);hash.i64(s->value.velocity.y.raw);
        hash.i64(s->value.velocity.z.raw);hash.u32(s->value.jumps);
        hash.u32(s->value.lastTick);hash.u32(s->value.jumpTick);
        hash.u32(s->value.initialized?1:0);
        hash.u32(i->value.jumpPressed?1:0);hash.u32(i->value.grounded?1:0);
        hash.i64(o->value.velocity.x.raw);hash.i64(o->value.velocity.y.raw);
        hash.u32(o->tick);hash.u32(o->accepted?1:0);
    }
    int result(Simulation&)override{return 0;}
};
template<class Tag>
uint64_t exercise(bool cow) {
    auto module=std::make_unique<Host<Tag>>();
    auto* host=module.get();
    Simulation sim(SimConfig{41,20,0,true},std::move(module));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    SimSnapshot initial;sim.saveState(initial);
    auto run=[&]() {
        sim.step(); // Tick 0: jump, accelerate on ground
        {
            const auto& world=sim.world();
            const auto* state=world.tryGetConst<lg::LocomotionStateComponent>(host->mover);
            const auto* output=world.tryGetConst<lg::LocomotionOutputComponent>(host->mover);
            const auto* input=world.tryGetConst<lg::LocomotionInputComponent>(host->mover);
            require(state&&state->value.velocity.x==Fixed::fromRatio(3,2),
                    "wrong initial ground acceleration");
            require(output&&output->accepted&&output->value.jumped,"jump result lost");
            require(input&&!input->value.jumpPressed,"jump edge not consumed");
        }
        auto* input=sim.world().tryGet<lg::LocomotionInputComponent>(host->mover);
        require(input!=nullptr,"input component missing");
        input->value.grounded=false; // collision owner reported airborne
        sim.step();sim.step();
        const auto& world=sim.world();
        const auto* state=world.tryGetConst<lg::LocomotionStateComponent>(host->mover);
        const auto* output=world.tryGetConst<lg::LocomotionOutputComponent>(host->mover);
        require(state&&state->value.velocity.x==Fixed::fromRatio(9,2),
                "three ticks of horizontal acceleration mismatch");
        require(output&&output->accepted&&output->tick==2,
                "locomotion output not published at tick 2");
        return sim.checksum();
    };
    const uint64_t expected=run();
    sim.loadState(initial);
    require(run()==expected,"locomotion rollback checksum changed");
    return expected;
}
int main() {
    const uint64_t same=exercise<ShooterTag>(false);
    require(exercise<StrategyTag>(false)==same,"strategy and shooter diverged");
    require(exercise<ShooterTag>(true)==same,"shooter COW changed checksum");
    require(exercise<StrategyTag>(true)==same,"strategy COW changed checksum");
    std::cout<<"PASS: universal ECS locomotion, input edges, COW and replay\n";
}
