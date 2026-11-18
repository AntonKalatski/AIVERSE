// Same VFX presenter consumes data-only ECS components in unrelated game styles.
#include <aiverselib/genre/EcsVfxPresentation.h>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
namespace lg = aiverse::lib::genre;
static void check(bool ok,const char* why) {
    if (!ok) throw std::runtime_error(why);
}
static fx::EffectDef continuous() {
    fx::EffectDef out;
    std::vector<std::string> errors;
    check(fx::parseEffect(Json::parse(R"JSON({"emitters":[{
      "texture":"builtin:soft", "loop":true,"duration":0.5,"rate":10,
      "speed":0,"life":0.25,"size":0.1
    }]})JSON"),"test",out,errors),"continuous fx fixture");
    return out;
}
static fx::EffectDef oneShot() {
    fx::EffectDef out;
    std::vector<std::string> errors;
    check(fx::parseEffect(Json::parse(R"JSON({"emitters":[{
      "texture":"builtin:spark", "burst":3,
      "speed":0,"life":0.5,"size":0.1
    }]})JSON"),"test",out,errors),"oneshot fx fixture");
    return out;
}
struct SpaceUnit {fx::float3 pos{2,3,4};};
struct RacingVehicle {fx::float3 pos{5,6,7};};

template <typename Kind>
static void scenario() {
    fx::ParticleSystem ps;
    ps.setQuality(fx::FxQuality::High);
    check(ps.init(nullptr),"headless backend");
    check(ps.define("engine_fire",continuous())>=0,"define continuous");
    check(ps.define("impact_flash",oneShot())>=0,"define event effect");
    World world;
    Entity source=world.create();
    world.add<Kind>(source);
    lg::VfxSourceComponent fxSource;
    fxSource.effect="engine_fire";
    fxSource.killOnDetach=true;
    world.add<lg::VfxSourceComponent>(source,fxSource);
    lg::VfxPresentationSystem presenter(ps);
    auto pose=[](const World& w,Entity e,fx::float3& pos,fx::float3& dir) {
        if (const Kind* kind=w.tryGetConst<Kind>(e)) {
            pos=kind->pos;
            dir={0,1,0};
            return true;
        }
        return false;
    };
    presenter.sync(world,pose);
    check(presenter.activeAttachments()==1,"attached emitter absent");
    ps.update(0.1f);
    check(ps.stats().particles>0,"no continuous particles");
    presenter.sync(world,pose);
    check(presenter.activeAttachments()==1,"duplicate attached emitter");

    SimEvent hit;
    hit.type=72;
    hit.pos={1_fx,2_fx,3_fx};
    const std::map<uint16_t,lg::VfxEventBinding> bindings{{72,{"impact_flash",1_fx}}};
    const size_t before=ps.stats().particles;
    presenter.consumeTickEvents(40,{hit},bindings);
    ps.update(0.1f);
    const size_t once=ps.stats().particles;
    check(once>0 && once>=before-20,"gameplay event did not play");
    presenter.consumeTickEvents(40,{hit},bindings);
    check(presenter.lastPresentedTick()==40,"duplicate tick changed cursor");
    ps.update(0.1f);

    world.destroy(source);
    Entity recycled=world.create();
    check(recycled.index==source.index && recycled.gen!=source.gen,"generation reuse fixture");
    world.add<Kind>(recycled);
    world.add<lg::VfxSourceComponent>(recycled,fxSource);
    presenter.sync(world,pose);
    check(presenter.activeAttachments()==1,"recycled entity attached twice");
    presenter.reset();
    check(presenter.activeAttachments()==0,"reset retained renderer handles");
    ps.snapshot(particles);
    check(particles.empty(),"reset must kill attached and recent one-shot particles");
    presenter.consumeTickEvents(39,{hit},bindings);
    check(presenter.lastPresentedTick()==39,"rollback reset did not clear event cursor");
}
int main() {
    scenario<SpaceUnit>();
    scenario<RacingVehicle>();
    std::cout << "PASS vfx_ecs_presentation_test (two genres, attaches, events, generation and reset)\n";
}
