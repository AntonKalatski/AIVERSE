// MotionWeave's host-owned hybrid animation scheduler.
// Thread-safe compiled clips/blend trees/IK work in parallel; Lua/native
// traits, environment and callback-bearing rigs execute on the caller only.
#pragma once
#include "AnimationJobScheduler.h"
#include "MotionWeaveWorkers.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace aiverse::lib::animation {
struct MotionWeaveScheduleOptions {
    size_t minimumParallelActors=48;
    size_t chunksPerWorker=4;
    size_t minChunkActors=4;
};
struct MotionWeaveScheduleStats {
    size_t eligible=0,parallelEntities=0,serialEntities=0;
    size_t batches=0,workerThreads=0;
    bool usedWorkers=false;
};
class MotionWeaveScheduler final {
public:
    explicit MotionWeaveScheduler(
        size_t backgroundWorkers=MotionWeaveWorkerPool::recommendedWorkers(),
        MotionWeaveScheduleOptions options={})
        :workers_(backgroundWorkers),options_(options){}
    MotionWeaveScheduler(const MotionWeaveScheduler&)=delete;
    MotionWeaveScheduler& operator=(const MotionWeaveScheduler&)=delete;

    size_t workerCount() const noexcept {return workers_.workerCount();}
    const MotionWeaveScheduleStats& stats() const noexcept {return last_;}

    // The caller must not mutate/snapshot World until the barrier returns.
    // All ECS writable pools are prepared before any worker is launched.
    // Per-entity callbacks execute strictly on the submitting thread,
    // AFTER parallel work finishes. Events are stored per entity.
    MotionWeaveScheduleStats update(World& world,float dt) {
        last_={};
        last_.workerThreads=workerCount();
        if(!std::isfinite(dt) || dt<0.f)return last_;
        auto prepared=prepareAnimationPresentation(world);
        if(!prepared.players || !prepared.poses)return last_;
        const World& read=world;
        const auto* inputs=read.poolIfExists<AnimationInputsComponent>();
        const auto* assets=read.poolIfExists<AnimationAssetComponent>();
        std::vector<size_t> safe,unsafe;
        safe.reserve(prepared.players->size());
        unsafe.reserve(prepared.players->size()/4+1);
        for(size_t i=0;i<prepared.players->size();++i) {
            Entity e=prepared.players->entityAt(i);
            if(!world.alive(e) || !prepared.poses->has(e.index))continue;
            const auto* asset=assets?assets->tryGet(e.index):nullptr;
            const auto* input=inputs?inputs->tryGet(e.index):nullptr;
            // Invalid/missing graph has no callbacks and may be processed in
            // a normal lane (which clears its stale output pose safely).
            const bool allowed=(!asset || !asset->graph ||
                workerSafeGraph(*asset->graph)) &&
                (!input || !input->overrides.environment);
            (allowed?safe:unsafe).push_back(i);
        }
        last_.eligible=safe.size()+unsafe.size();
        last_.parallelEntities=safe.size();
        last_.serialEntities=unsafe.size();
        const bool parallel=workerCount()>0 &&
            safe.size()>=std::max<size_t>(1,options_.minimumParallelActors);
        if(parallel) {
            const size_t slots=workerCount()+1;
            const size_t chunks=std::min(safe.size(),
                slots*std::max<size_t>(1,options_.chunksPerWorker));
            const size_t chunkSize=std::max<size_t>(
                std::max<size_t>(1,options_.minChunkActors),
                (safe.size()+chunks-1)/chunks);
            const size_t jobs=(safe.size()+chunkSize-1)/chunkSize;
            last_.batches=jobs;
            workers_.parallelFor(jobs,[&](size_t job){
                const size_t start=job*chunkSize;
                evaluateAnimationPresentationRange(world,prepared,dt,
                    start,std::min(safe.size(),start+chunkSize),&safe);
            });
            last_.usedWorkers=true;
        } else {
            evaluateAnimationPresentationRange(world,prepared,dt,0,
                safe.size(),&safe);
        }
        // All worker tasks have finished. Unsafe rig/Lua/native callbacks
        // cannot overlap worker access to shared host state.
        evaluateAnimationPresentationRange(world,prepared,dt,0,
            unsafe.size(),&unsafe);
        return last_;
    }
private:
    MotionWeaveWorkerPool workers_;
    MotionWeaveScheduleOptions options_;
    MotionWeaveScheduleStats last_;
};
} // namespace aiverse::lib::animation
