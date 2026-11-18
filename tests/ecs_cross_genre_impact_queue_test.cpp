// One ordered ECS impact-resolution barrier for unrelated game hosts.
// Exercises queued direct/splash damage, delayed reentrant submission,
// sequence IDs and deep/COW snapshot rollback without genre-module linkage.
#include <aiverselib/genre/EcsImpactQueueSystems.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace aiverse;
namespace lg=aiverse::lib::genre;
static void check(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}

struct ShooterTag {};
struct StrategyTag {};
struct TestHealth {int32_t current=100;};
struct TestTeam {uint8_t value=0;};
struct HitTrace {std::vector<int> stages;};

template<class Tag>
class Host final:public GameModule {
public:
    Entity shooter=kNullEntity,projectile=kNullEntity;
    Entity primary=kNullEntity,ally=kNullEntity,secondary=kNullEntity;
    std::string name()const override{return "shared-ecs-impact-queue";}

    void setup(Simulation& sim,const Json&,const Json&)override {
        World& world=sim.world();
        shooter=world.create();
        projectile=world.create();
        primary=world.create();
        ally=world.create();
        secondary=world.create();
        world.add<Tag>(shooter);
        world.add<TestTeam>(shooter,TestTeam{1});
        world.add<TestTeam>(primary,TestTeam{2});
        world.add<TestTeam>(ally,TestTeam{1});
        world.add<TestTeam>(secondary,TestTeam{2});
        for(Entity entity:{primary,ally,secondary})
            world.add<TestHealth>(entity,TestHealth{100});
        world.setResource<lg::OrderedImpactQueue>();
        world.setResource<HitTrace>();
        check(lg::enqueueOrderedImpact(world,{
            projectile,shooter,primary,Vec3{},10_fx,2_fx
        })==1,"first impact sequence mismatch");
        check(lg::enqueueOrderedImpact(world,{
            projectile,shooter,secondary,Vec3{},10_fx,Fixed{}
        })==2,"second impact sequence mismatch");

        lg::addOrderedImpactResolutionSystem(sim,"shared-ordered-impacts",
            [](Simulation& current,const lg::QueuedProjectileImpact& request,
               Entity target) {
                auto* hp=current.world().tryGet<TestHealth>(target);
                check(hp!=nullptr,"direct hit target missing");
                hp->current-=int32_t(request.impact.damage.toInt());
                current.world().resource<HitTrace>()->stages.push_back(
                    10+int(request.sequence));
            },
            [this](Simulation& current,const lg::QueuedProjectileImpact& request,
                   Entity target) {
                current.world().resource<HitTrace>()->stages.push_back(
                    20+int(request.sequence));
                if(request.sequence==1) {
                    // A second-stage impact must be deferred to the next
                    // barrier, even though it was enqueued inside this one.
                    check(target==primary,"unexpected first direct target");
                    check(lg::enqueueOrderedImpact(current.world(),{
                        projectile,shooter,primary,Vec3{},3_fx,Fixed{}
                    })==3,"nested impact sequence mismatch");
                }
            },
            [this](Simulation& current,const lg::QueuedProjectileImpact& request,
                   Vec3 point,Fixed radius,auto&& visit) {
                check(request.sequence==1 && point==Vec3{} &&
                      radius==2_fx,"unexpected splash query");
                current.world().resource<HitTrace>()->stages.push_back(30);
                visit(primary);  // primary must not receive splash twice
                visit(ally);     // team filter skips friend
                visit(secondary);
            },
            [](Simulation& current,const lg::QueuedProjectileImpact& request,
                   Entity target) {
                const World& world=current.world();
                const auto* source=world.tryGetConst<TestTeam>(request.impact.source);
                const auto* other=world.tryGetConst<TestTeam>(target);
                return world.alive(target) && source && other &&
                       source->value!=other->value;
            },
            [](Simulation& current,const lg::QueuedProjectileImpact& request,
               Entity target) {
                auto* hp=current.world().tryGet<TestHealth>(target);
                check(hp!=nullptr,"splash target missing");
                hp->current-=int32_t(request.impact.damage.toInt()/2);
                current.world().resource<HitTrace>()->stages.push_back(40);
            });
    }
    void applyCommand(Simulation&,const Command&)override {}
    void hashState(Simulation& sim,Hasher& hash)override {
        const World& world=sim.world();
        for(Entity entity:{primary,ally,secondary}) {
            const auto* health=world.tryGetConst<TestHealth>(entity);
            check(health!=nullptr,"health vanished during hash");
            hash.i32(health->current);
        }
        const auto* queue=world.resourceConst<lg::OrderedImpactQueue>();
        const auto* trace=world.resourceConst<HitTrace>();
        check(queue && trace,"world resources missing during hash");
        lg::hashOrderedImpactQueue(hash,*queue);
        hash.u32(uint32_t(trace->stages.size()));
        for(int stage:trace->stages)hash.i32(stage);
    }
    int result(Simulation&)override{return 0;}
};

template<class Tag>
uint64_t exercise(bool cow) {
    auto game=std::make_unique<Host<Tag>>();
    auto* host=game.get();
    Simulation sim(SimConfig{59,20,0,true},std::move(game));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    SimSnapshot before;
    sim.saveState(before);
    auto run=[&]() {
        sim.step();
        const World& first=sim.world();
        const auto* queue=first.resourceConst<lg::OrderedImpactQueue>();
        const auto* trace=first.resourceConst<HitTrace>();
        check(queue&&queue->nextSequence==4&&queue->pending.size()==1 &&
              queue->pending.front().sequence==3,
              "callback-generated request was not deferred");
        check(trace&&trace->stages==std::vector<int>({11,21,30,40,12,22}),
              "immediate direct/on-hit/splash order changed");
        const std::string savedQueue=lg::saveOrderedImpactQueue(*queue);
        check(!savedQueue.empty(),"impact queue save failed");
        lg::OrderedImpactQueue loadedQueue;
        check(lg::loadOrderedImpactQueue(savedQueue,loadedQueue) &&
              lg::saveOrderedImpactQueue(loadedQueue)==savedQueue,
              "impact queue serialization round-trip mismatch");
        auto invalidQueue=savedQueue;
        invalidQueue.pop_back();
        check(!lg::loadOrderedImpactQueue(invalidQueue,loadedQueue) &&
              lg::saveOrderedImpactQueue(loadedQueue)==savedQueue,
              "truncated queue load overwrote a valid destination");
        invalidQueue=savedQueue;
        invalidQueue[0]=char(255); // invalid format version
        check(!lg::loadOrderedImpactQueue(invalidQueue,loadedQueue) &&
              lg::saveOrderedImpactQueue(loadedQueue)==savedQueue,
              "invalid format queue load overwrote a valid destination");

        check(first.tryGetConst<TestHealth>(host->primary)->current==90,
              "first direct hit applied incorrectly");
        check(first.tryGetConst<TestHealth>(host->ally)->current==100,
              "friendly fire filter was ignored");
        check(first.tryGetConst<TestHealth>(host->secondary)->current==85,
              "splash and second direct hit ordered incorrectly");

        sim.step();
        const World& second=sim.world();
        const auto* pending=second.resourceConst<lg::OrderedImpactQueue>();
        const auto* after=second.resourceConst<HitTrace>();
        check(pending && pending->pending.empty() && pending->nextSequence==4,
              "nested request not consumed exactly once");
        check(after && after->stages==
              std::vector<int>({11,21,30,40,12,22,13,23}),
              "deferred impact ran in the wrong barrier");
        check(second.tryGetConst<TestHealth>(host->primary)->current==87,
              "second barrier direct damage mismatch");
        return sim.checksum();
    };
    const uint64_t expected=run();
    sim.loadState(before);
    check(sim.world().resourceConst<lg::OrderedImpactQueue>()->pending.size()==2,
          "rollback did not restore queued impacts");
    check(run()==expected,"impact queue checksum diverged after rollback");
    return expected;
}
// Submission failure must not mutate the request queue or consume IDs.
static void exerciseQueueBoundaries() {
    World world;
    world.setResource<lg::OrderedImpactQueue>();
    auto* queue=world.resource<lg::OrderedImpactQueue>();
    check(queue!=nullptr,"missing queue resource");
    lg::OrderedProjectileImpact impact;
    queue->nextSequence=0;
    check(lg::enqueueOrderedImpact(world,impact)==0 &&
          queue->nextSequence==0 && queue->pending.empty(),
          "invalid zero sequence accepted");
    queue->nextSequence=UINT64_MAX;
    check(lg::enqueueOrderedImpact(world,impact)==0 &&
          queue->nextSequence==UINT64_MAX && queue->pending.empty(),
          "sequence ID overflow accepted");
    queue->nextSequence=8;
    queue->pending.resize(lg::OrderedImpactQueue::kMaximumPending);
    check(lg::enqueueOrderedImpact(world,impact)==0 &&
          queue->nextSequence==8 &&
          queue->pending.size()==lg::OrderedImpactQueue::kMaximumPending,
          "queue capacity failure mutated state");
}
int main() {
    exerciseQueueBoundaries();
    const uint64_t baseline=exercise<ShooterTag>(false);
    check(exercise<StrategyTag>(false)==baseline,
          "shooting and strategy hosts diverged");
    check(exercise<ShooterTag>(true)==baseline,
          "shooter COW impact queue checksum mismatch");
    check(exercise<StrategyTag>(true)==baseline,
          "strategy COW impact queue checksum mismatch");
    std::cout<<"PASS ECS ordered impact queue, delayed callbacks and rollback\n";
}
