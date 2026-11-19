// Stage 104 deferred regression: persistent thread reuse, bounded ready
// queue, multi-worker import revisions, no GPU work on worker threads.
// Do NOT run until the animation feature phase finishes.
#include "engine/render/AsyncModelCpuQueue.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse::render;
    AsyncModelCpuQueue tasks;
    tasks.setWorkerCount(2);
    tasks.setMaxQueued(16);
    tasks.setMaxReady(2);
    auto textures=std::make_shared<ModelTextureCache>();
    std::atomic<int> active{0},peak{0};
    const FileReader missing=[&](const std::string&,std::string&) {
        const int current=++active;
        int old=peak.load();
        while(current>old && !peak.compare_exchange_weak(old,current)){}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        --active;
        return false;
    };

    assert(tasks.request("a.glb",missing,textures,ModelLoadPriority::Interactive));
    assert(tasks.request("b.glb",missing,textures,ModelLoadPriority::Interactive));
    assert(tasks.request("c.glb",missing,textures,ModelLoadPriority::Background));
    assert(tasks.request("d.glb",missing,textures,ModelLoadPriority::Visible));

    std::vector<std::string> results;
    const auto deadline=std::chrono::steady_clock::now()+
                         std::chrono::seconds(5);
    while(results.size()<4 && std::chrono::steady_clock::now()<deadline) {
        AsyncModelCpuQueue::Prepared ready;
        if(tasks.pollReady(ready))results.push_back(ready.source);
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(results.size()==4);
    assert(tasks.stats().finished==4);
    assert(tasks.stats().liveThreads==2); // persistent workers remain idle
    assert(tasks.stats().ready==0);
    assert(peak.load()>=1 && peak.load()<=2);

    // Pool reconfiguration joins only active CPU work, preserving queued
    // valid results. A new project reader can safely be installed after
    // all earlier tasks have joined.
    tasks.setWorkerCount(1);
    assert(tasks.workerCount()==1);
    assert(tasks.request("again.glb",missing,textures));
    bool received=false;
    while(!received && std::chrono::steady_clock::now()<deadline) {
        AsyncModelCpuQueue::Prepared ready;
        if(tasks.pollReady(ready)) {
            assert(ready.source=="again.glb");
            received=true;
        } else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(received);
    assert(tasks.stats().liveThreads==1);
    tasks.shutdown();
    assert(tasks.stats().liveThreads==0);
    assert(tasks.stats().activeWorkers==0);
    assert(tasks.stats().queued==0);
}
