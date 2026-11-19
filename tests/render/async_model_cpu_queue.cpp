// Stage 101 deferred: native CPU queue dedupe, revision and shutdown.
#include "engine/render/AsyncModelCpuQueue.h"
#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

int main() {
    using namespace aiverse::render;
    AsyncModelCpuQueue queue;
    auto textures=std::make_shared<ModelTextureCache>();
    const FileReader absent=[](const std::string&,std::string&) {
        return false;
    };
    queue.setMaxQueued(1);
    assert(queue.request("missing/a.glb",absent,textures));
    assert(queue.contains("missing/a.glb"));
    assert(!queue.request("missing/a.glb",absent,textures)); // dedupe
    assert(queue.request("missing/b.glb",absent,textures));  // one queued
    assert(!queue.request("missing/c.glb",absent,textures)); // bounded
    queue.invalidate("missing/a.glb"); // running import becomes stale

    AsyncModelCpuQueue::Prepared prepared;
    bool gotB=false;
    const auto deadline=std::chrono::steady_clock::now()+
                         std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<deadline) {
        if(queue.pollReady(prepared)) {
            assert(prepared.source=="missing/b.glb");
            assert(prepared.model==nullptr);
            gotB=true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(gotB);
    assert(queue.stats().discarded==1);
    assert(queue.stats().finished==1);
    assert(queue.stats().queued==0);
    assert(!queue.stats().workerActive);

    // Source recovery may be enqueued with a new revision; shutdown
    // joins the CPU worker before callers dispose of reader/texture state.
    assert(queue.request("missing/a.glb",absent,textures));
    queue.shutdown();
    assert(!queue.stats().workerActive && queue.stats().queued==0);
}
