// Deterministic ECS impact request/commit barrier for any game genre.
// Different motion/targeting systems enqueue impacts, while the host supplies
// armor, shields, faction, status and collision/spatial-query rules.
// No RTS/FPS module dependency; no secondary health or projectile owner.
#pragma once
#include "EcsOrderedImpactSystems.h"
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::genre {

struct QueuedProjectileImpact {
    uint64_t sequence=0;
    OrderedProjectileImpact impact{};
};

// Rollback-owned, bounded world resource. Sequence numbers and pending
// requests must be included in authoritative snapshots/checksums.
struct OrderedImpactQueue {
    static constexpr size_t kMaximumPending=8192;
    uint64_t nextSequence=1;
    std::vector<QueuedProjectileImpact> pending;
};

// Submit after validated weapon/collision rules, in a deterministic order.
// Failure does not consume a sequence ID or append a partial request.
inline uint64_t enqueueOrderedImpact(World& world,
                                     OrderedProjectileImpact impact) {
    auto* queue=world.resource<OrderedImpactQueue>();
    if(!queue || queue->pending.size()>=OrderedImpactQueue::kMaximumPending ||
       queue->nextSequence==0 ||
       queue->nextSequence==std::numeric_limits<uint64_t>::max())
        return 0;
    const uint64_t sequence=queue->nextSequence++;
    queue->pending.push_back({sequence,std::move(impact)});
    return sequence;
}

// Signature of callbacks:
//   DirectHit  (Simulation&, const QueuedProjectileImpact&, Entity target)
//   AfterDirect(Simulation&, const QueuedProjectileImpact&, Entity target)
//   QueryArea  (Simulation&, const QueuedProjectileImpact&, Vec3, Fixed,
//               visitor(Entity))
//   CanSplash  (Simulation&, const QueuedProjectileImpact&, Entity) -> bool
//   SplashHit  (Simulation&, const QueuedProjectileImpact&, Entity)
//
// The queue is swapped at the barrier. Requests created by callbacks are
// processed at the NEXT invocation, never recursively in the same pass.
// A caller whose legacy mechanics require immediate per-projectile impacts
// must register/execute a compatible barrier for each relevant stage; blindly
// batching all hits at end-of-tick can change shield, kill and trigger order.
template<class DirectHit,class AfterDirect,class QueryArea,
         class CanSplash,class SplashHit>
void addOrderedImpactResolutionSystem(Simulation& sim,std::string name,
                                      DirectHit directHit,
                                      AfterDirect afterDirect,
                                      QueryArea queryArea,
                                      CanSplash canSplash,
                                      SplashHit splashHit) {
    sim.addSystem(std::move(name),
        [directHit=std::move(directHit),
         afterDirect=std::move(afterDirect),
         queryArea=std::move(queryArea),
         canSplash=std::move(canSplash),
         splashHit=std::move(splashHit)](Simulation& current) mutable {
            auto* queue=current.world().resource<OrderedImpactQueue>();
            if(!queue || queue->pending.empty())return;
            std::vector<QueuedProjectileImpact> batch;
            batch.swap(queue->pending);
            for(const auto& request:batch)
                resolveOrderedProjectileImpact(
                    request.impact,
                    [&](Entity target){directHit(current,request,target);},
                    [&](Entity target){afterDirect(current,request,target);},
                    [&](Vec3 point,Fixed radius,auto&& visit) {
                        queryArea(current,request,point,radius,
                                  std::forward<decltype(visit)>(visit));
                    },
                    [&](Entity target)->bool {
                        return canSplash(current,request,target);
                    },
                    [&](Entity target){splashHit(current,request,target);});
        });
}

} // namespace aiverse::lib::genre
