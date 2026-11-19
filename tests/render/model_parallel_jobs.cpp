// Stage 103 source regression (execution deferred): multi-worker imports,
// source revisions, bounded in-flight work and background non-starvation.
#include "engine/render/AsyncModelCpuQueue.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

int main() {
    using namespace aiverse::render;
    AsyncModelCpuQueue queue;
    queue.setWorkerCount(2);
    queue.setMaxQueued(12);
    auto textures=std::make_shared<ModelTextureCache>();
    std::atomic<int> running{0},peak{0};
    FileReader reader=[&](const std::string&,std::string&)->bool {
        const int active=++running;
        int before=peak.load();
        while(active>before && !peak.compare_exchange_weak(before,active)){}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        --running;
        return false; // missing models still produce a valid null result
    };
    assert(queue.workerCount()==2);
    assert(queue.request("busy0.glb",reader,textures));
    assert(queue.request("busy1.glb",reader,textures));
    assert(queue.stats().activeWorkers==2);
    assert(queue.request("background.glb",reader,textures,
                         ModelLoadPriority::Background));
    assert(queue.request("interactive.glb",reader,textures,
                         ModelLoadPriority::Interactive));
    assert(queue.request("stale.glb",reader,textures,
                         ModelLoadPriority::Visible));
    queue.invalidate("stale.glb");
    assert(queue.request("stale.glb",reader,textures,
                         ModelLoadPriority::Visible));
    std::vector<std::string> done;
    const auto deadline=std::chrono::steady_clock::now()+
                         std::chrono::seconds(4);
    while(done.size()<5 && std::chrono::steady_clock::now()<deadline) {
        AsyncModelCpuQueue::Prepared model;
        if(queue.pollReady(model))done.push_back(model.source);
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(done.size()==5);
    assert(std::count(done.begin(),done.end(),"stale.glb")==1);
    assert(std::count(done.begin(),done.end(),"background.glb")==1);
    assert(std::count(done.begin(),done.end(),"interactive.glb")==1);
    assert(peak.load()<=2);
    assert(peak.load()>=1);
    assert(queue.stats().finished==5);
    assert(queue.stats().queued==0);
    assert(queue.stats().activeWorkers==0);
    queue.setWorkerCount(0); // clamped to one
    assert(queue.workerCount()==1);
    queue.setWorkerCount(100); // clamped to eight
    assert(queue.workerCount()==8);
    queue.shutdown();
}
