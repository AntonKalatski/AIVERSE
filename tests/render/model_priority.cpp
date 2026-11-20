// Stage 102 deferred regression: priority/FIFO and duplicate promotion.
#include "engine/render/AsyncModelCpuQueue.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
using namespace aiverse::render;
int main() {
    AsyncModelCpuQueue queue;
    auto cache=std::make_shared<ModelTextureCache>();
    std::atomic<bool> entered{false},release{false};
    FileReader reader=[&](const std::string& path,std::string&)->bool {
        if(path=="busy.glb") {
            entered=true;
            while(!release.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };
    assert(queue.request("busy.glb",reader,cache,ModelLoadPriority::Visible));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(!entered && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(entered);
    assert(queue.request("background.glb",reader,cache,ModelLoadPriority::Background));
    assert(queue.request("visible.glb",reader,cache,ModelLoadPriority::Visible));
    assert(queue.request("interactive.glb",reader,cache,ModelLoadPriority::Interactive));
    assert(!queue.request("interactive.glb",reader,cache,ModelLoadPriority::Interactive));
    assert(queue.request("background.glb",reader,cache,ModelLoadPriority::Interactive));
    assert(queue.stats().queued==3);
    release=true;

    std::vector<std::string> completed;
    AsyncModelCpuQueue::Prepared prepared;
    while(completed.size()<4 && std::chrono::steady_clock::now()<deadline) {
        if(queue.pollReady(prepared))
            completed.push_back(prepared.source);
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(completed.size()==4);
    assert(completed[0]=="busy.glb");
    // Promoted background waits before the lower-priority visible entry.
    assert(completed[1]=="background.glb");
    assert(completed[2]=="interactive.glb");
    assert(completed[3]=="visible.glb");
    queue.shutdown();
}
