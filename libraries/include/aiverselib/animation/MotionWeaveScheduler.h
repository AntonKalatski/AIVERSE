// MotionWeave hybrid, cost-aware animation scheduler.
// Worker-safe graph entities use a persistent work-stealing thread pool.
// Lua/native operations, rig contacts and host callbacks run on caller thread.
#pragma once
#include "AnimationJobScheduler.h"
#include "AnimationWorkPolicy.h"
#include "MotionWeaveWorkers.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace aiverse::lib::animation {
struct MotionWeaveScheduleOptions {
    size_t minimumParallelActors=16;
    size_t minimumParallelCost=256;
    size_t chunksPerWorker=4;
    size_t minChunkActors=4;
};
struct MotionWeaveScheduleStats {
    size_t eligible=0,parallelEntities=0,serialEntities=0;
    size_t batches=0,workerThreads=0;
    size_t estimatedSafeCost=0,dispatchedEntities=0;
    uint64_t heaviestBatchCost=0;
    double workerPhaseMs=0,serialPhaseMs=0;
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
    const MotionWeaveScheduleOptions& options() const noexcept {return options_;}
    // Apply host tuning between frames, not while update() is running.
    void setOptions(MotionWeaveScheduleOptions options) {options_=options;}

    MotionWeaveScheduleStats update(World& world,float dt) {
        last_={};
        last_.workerThreads=workerCount();
        if(!std::isfinite(dt)||dt<0.f)return last_;
        const auto prepared=prepareAnimationPresentation(world);
        if(!prepared.players || !prepared.poses)return last_;
        const World& read=world;
        const auto* inputs=read.poolIfExists<AnimationInputsComponent>();
        const auto* assets=read.poolIfExists<AnimationAssetComponent>();
        const auto* hints=read.poolIfExists<AnimationWorkHintComponent>();
        std::vector<size_t> safe,unsafe;
        std::vector<uint32_t> weight;
        safe.reserve(prepared.players->size());
        unsafe.reserve(prepared.players->size()/4+1);
        weight.reserve(prepared.players->size());
        for(size_t i=0;i<prepared.players->size();++i) {
            const Entity e=prepared.players->entityAt(i);
            if(!world.alive(e)||!prepared.poses->has(e.index))continue;
            const auto* asset=assets?assets->tryGet(e.index):nullptr;
            const auto* input=inputs?inputs->tryGet(e.index):nullptr;
            const auto* hint=hints?hints->tryGet(e.index):nullptr;
            const bool workerSafe=(!asset || !asset->graph ||
                    workerSafeGraph(*asset->graph)) &&
                (!input || !input->overrides.environment) &&
                (!hint || !hint->mainThread);
            if(!workerSafe) {unsafe.push_back(i);continue;}
            safe.push_back(i);
            const uint32_t base=asset && asset->graph?
                estimateAnimationWork(*asset->graph):1;
            const uint32_t cost=scaledAnimationCost(base,
                hint?hint->multiplier:1.f);
            weight.push_back(cost);
            last_.estimatedSafeCost+=cost;
        }
        last_.eligible=safe.size()+unsafe.size();
        last_.parallelEntities=safe.size();
        last_.serialEntities=unsafe.size();
        const bool eligible=workerCount()>0 &&
            safe.size()>=std::max<size_t>(1,options_.minimumParallelActors) &&
            last_.estimatedSafeCost>=options_.minimumParallelCost;
        const auto now=[](){return std::chrono::steady_clock::now();};
        const auto millis=[](auto span) {
            return std::chrono::duration<double,std::milli>(span).count();
        };
        const auto start=now();
        if(eligible) {
            const size_t target=(workerCount()+1)*
                std::max<size_t>(1,options_.chunksPerWorker);
            const auto jobs=partitionAnimationWork(weight,target,
                options_.minChunkActors);
            last_.batches=jobs.size();
            for(const auto& c:jobs)
                last_.heaviestBatchCost=std::max(last_.heaviestBatchCost,c.estimatedCost);
            if(jobs.size()>1) {
                workers_.parallelFor(jobs.size(),[&](size_t j) {
                    const auto& c=jobs[j];
                    evaluateAnimationPresentationRange(world,prepared,dt,
                        c.begin,c.end,&safe);
                });
                last_.usedWorkers=true;
                last_.dispatchedEntities=safe.size();
            } else {
                evaluateAnimationPresentationRange(world,prepared,dt,0,
                    safe.size(),&safe);
            }
        } else {
            evaluateAnimationPresentationRange(world,prepared,dt,0,
                safe.size(),&safe);
        }
        last_.workerPhaseMs=millis(now()-start);
        // Complete barrier: environment, Lua and host callbacks are now
        // guaranteed not to overlap any worker graph evaluation.
        const auto callbackStart=now();
        evaluateAnimationPresentationRange(world,prepared,dt,0,
            unsafe.size(),&unsafe);
        last_.serialPhaseMs=millis(now()-callbackStart);
        return last_;
    }
private:
    MotionWeaveWorkerPool workers_;
    MotionWeaveScheduleOptions options_;
    MotionWeaveScheduleStats last_;
};
} // namespace aiverse::lib::animation
