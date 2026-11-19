// First executable native SpectraFX contract checks: presentation-only,
// no RHI device, editor, network, or GPU requirements.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "engine/fx/SpectraFxGpuRoutes.h"
#include "engine/fx/SpectraFxCueInbox.h"
#include "engine/fx/SpectraFxGpuLod.h"
#include "engine/fx/SpectraFxGpuBudgetGovernor.h"

using namespace aiverse;
using namespace aiverse::fx;

namespace {
int failures=0;
void expect(bool value,const char* test) {
    if(!value) {
        std::fprintf(stderr,"FAILED SpectraFX contract: %s\n",test);
        ++failures;
    }
}
} // namespace

int main() {
    std::string error;
    SpectraFxHybridEmitter::Config emitter;
    expect(SpectraFxGpuEmitterAsset::readText(
        R"JSON({"version":1,"cpuEffect":"smoke","preference":"auto",
                 "particles":128,"allowCpuFallback":true,
                 "appearance":{"additive":true},
                 "spawn":{"burst":8,"rate":20,"life":0.5}})JSON",
        emitter,&error), "valid authored GPU emitter");
    expect(emitter.backend.particles==128,"GPU capacity parsed");
    expect(emitter.appearance.additive,"additive flag parsed");
    expect(!SpectraFxGpuEmitterAsset::readText(
        R"JSON({"version":1,"cpuEffect":"smoke","particles":128,
                 "gpuSortAlpha":true,"gpuIndirectCompaction":true})JSON",
        emitter,&error), "sorting and compaction mutually exclusive");

    SpectraFxGpuRouteAsset::Routes routes;
    std::optional<FxQuality> sceneQuality;
    SpectraFxGpuBudgetGovernor::Config budget;
    expect(SpectraFxGpuRouteAsset::readText(
        R"JSON({"version":1,"sceneQuality":"low",
                 "gpuFrameBudgetMs":12,"minGpuSpawnScale":0.25,
                 "routes":[{"eventType":7,"asset":"smoke",
                            "sharedPool":"fire","sharedPoolCapacity":1024}]})JSON",
        routes,&error,&budget,&sceneQuality), "valid project routes");
    expect(routes.count(7)==1 && routes.at(7).playback.sharedPool=="fire",
           "route pool resolved");
    expect(sceneQuality && *sceneQuality==FxQuality::Low,
           "scene quality parsed");
    expect(budget.targetFrameMs==12.0f,"GPU budget parsed");
    const auto preserved=routes;
    expect(!SpectraFxGpuRouteAsset::readText(
        R"JSON({"version":1,"routes":[{"eventType":7,"asset":"smoke"},
                  {"eventType":7,"asset":"smoke2"}]})JSON",
        routes,&error), "duplicate cue rejected");
    expect(routes.size()==preserved.size() && routes.count(7)==1,
           "failed route edit preserves old map");
    expect(!SpectraFxGpuRouteAsset::readText(
        R"JSON({"version":1,"routes":[{"eventType":1,"asset":"smoke",
                 "ownPlayerOnly":false}]})JSON",routes,&error),
           "invisible/unauthorized routes rejected");

    SpectraFxCueInbox inbox(2);
    const auto epoch=inbox.epoch();
    SpectraFxCue play{};
    play.id=7; play.key=5;
    expect(inbox.submit(play,true,epoch),"enqueue keyed play");
    SpectraFxCue move=play;
    move.action=SpectraFxCue::Action::Move;
    move.position={1,2,3};
    expect(inbox.submit(move,true,epoch),"enqueue move");
    move.position={4,5,6};
    expect(inbox.submit(move,true,epoch),"coalesce same-key move");
    expect(inbox.pending()==2 && inbox.stats().coalescedMoves==1,
           "move coalescing preserves capacity");
    SpectraFxCue kill=play;kill.action=SpectraFxCue::Action::Kill;
    expect(inbox.submit(kill,true,epoch),"kill prioritized in full queue");
    expect(inbox.pending()==2 && inbox.stats().evicted==1,
           "full queue evicts a cosmetic cue for Kill");
    int killed=0;
    inbox.drain([&](const SpectraFxCue& cue) {
        if(cue.action==SpectraFxCue::Action::Kill) ++killed;
        return true;
    });
    expect(killed==1,"bounded inbox drains lifecycle action");
    inbox.reset();
    expect(!inbox.submit(play,true,epoch) && inbox.stats().stale==1,
           "old scene generation rejected");
    expect(!inbox.submit(play,false,inbox.epoch()),
           "visibility denial is honored");

    SpectraFxGpuLodSettings lod;
    lod.nearDistance=10;lod.farDistance=100;
    expect(lod.validate(),"valid camera LOD");
    expect(lod.spawnScale(0)==1.0f && lod.spawnScale(101)==0,
           "distance LOD bounds");

    SpectraFxGpuBudgetGovernor governor;
    SpectraFxGpuBudgetGovernor::Config config;
    config.targetFrameMs=12;
    expect(governor.configure(config),"valid GPU frame-budget config");
    for(int i=0;i<60;++i) governor.observe(24,1.0f/60);
    expect(governor.spawnScale()<1,"over-budget GPU frames lower new-spawn scale");
    expect(governor.spawnScale()>=config.minSpawnScale,
           "spawn governor never goes below configured minimum");
    if(failures) {
        std::fprintf(stderr,"%d SpectraFX contract failures\n",failures);
        return 1;
    }
    std::puts("SpectraFX native contracts passed");
    return 0;
}
