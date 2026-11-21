// Scrolling GI cascades: toroidal probe slots keep their world cell, only
// cells entering the volume are recycled, zeroed on the GPU (clear dispatch
// with zero samples) and re-traced; GI stays presented after convergence.
#include "engine/render/GiMainGpuBridge.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
using namespace aiverse::render;
struct ScrollDevice:FakeDevice {
    uint64_t submitted=1,completed=0;
    gfx::BufferHandle jobs{};uint32_t lastSamples=99,lastJobs=0;size_t clears=0,traces=0;
    ScrollDevice(){info_.backend=gfx::Backend::Metal;computeEnabled=true;}
    bool supports(gfx::PixelFormat) const override{return true;}
    uint64_t gpuSubmissionFence() const override{return inFrame?submitted:0;}
    uint64_t gpuCompletedFence() const override{return completed;}
    bool gpuHealthy() const override{return true;}
    bool computeDispatchSucceeded() const override{return dispatchCount>0;}
    void setComputeBuffer(int slot,gfx::BufferHandle h) override{if(slot==2)jobs=h;}
    void setComputeUniforms(const void* p,size_t) override{
        GiGpuSmallScene::Uniforms u;std::memcpy(&u,p,sizeof u);lastSamples=u.samples;lastJobs=u.jobs;
        (u.samples?traces:clears)++;
    }
    // Probe indices of the last dispatch (GpuJob = index + xyz, 16 bytes).
    std::set<uint32_t> lastIndices() const {
        std::set<uint32_t> out;const auto& b=buffers.at(jobs.id);
        for(uint32_t i=0;i<lastJobs;++i){uint32_t v;std::memcpy(&v,b.data()+size_t(i)*16,4);out.insert(v);}
        return out;
    }
};
static bool near(float a,float b){return std::abs(a-b)<1e-4f;}
int main() {
    // Scheduler: shifting by one cell recycles one face; kept slots keep their world position.
    {
        GiUpdateScheduler s;s.configure({0,0,0},1,4,3,2);
        for(auto t:s.claim(100,{0,0,0}))s.complete(t);
        AIVERSE_TEST_CHECK(!s.pending());
        std::vector<float3> before;for(uint32_t i=0;i<s.size();++i)before.push_back(s.position(i));
        const auto ids=s.scroll(1,0,0,GiDirtyReason::Geometry);
        AIVERSE_TEST_CHECK(ids.size()==6 && s.pending()==6 && s.ring(0)==1 && near(s.origin().x,1));
        std::set<uint32_t> fresh(ids.begin(),ids.end());
        for(uint32_t i=0;i<s.size();++i) {
            const auto p=s.position(i);
            if(fresh.count(i))AIVERSE_TEST_CHECK(near(p.x,4.5f));           // entered on the +x side
            else AIVERSE_TEST_CHECK(near(p.x,before[i].x) && near(p.y,before[i].y) && near(p.z,before[i].z));
            AIVERSE_TEST_CHECK(p.x>1 && p.x<5);
        }
        // Region invalidation follows the ring: the cell around x=4.5 is a recycled slot.
        for(auto t:s.claim(100,{0,0,0}))s.complete(t);
        std::vector<uint32_t> hit;s.invalidate({{4.4f,.4f,.4f},{4.6f,.6f,.6f}},GiDirtyReason::Lighting,0,&hit);
        AIVERSE_TEST_CHECK(hit.size()==1 && fresh.count(hit[0]) && near(s.position(hit[0]).x,4.5f));
        // Negative shift and wrap; a full-width jump recycles every slot.
        AIVERSE_TEST_CHECK(s.scroll(-2,0,-1,GiDirtyReason::Geometry).size()==24-2*3*1);
        AIVERSE_TEST_CHECK(s.ring(0)==3 && s.ring(2)==1);
        AIVERSE_TEST_CHECK(s.scroll(0,5,0,GiDirtyReason::Geometry).size()==24);
    }
    // Bridge: CPU fallback refuses to scroll.
    GiMainCpuConfig cfg;cfg.origin={-2,-2,-2};cfg.spacing=1;cfg.nx=cfg.ny=cfg.nz=4;
    cfg.samples=16;cfg.traceDistance=2;cfg.cascades=2;cfg.scroll=true;
    {ScrollDevice d;GiMainCpuBridge cpu;AIVERSE_TEST_CHECK(!cpu.initialize(d,cfg));}

    GiChunkedGeometryBuilder builder;
    builder.replace(1,{{{-1,-1,1},{1,-1,1},{1,1,1},0}},{{1,1,1}});
    builder.replace(2,{{{40,0,0},{41,0,0},{40,1,0},0},{{40,0,1},{41,0,1},{40,1,1},0}},{{1,1,1}});
    auto scene=std::make_shared<const GiChunkedGeometrySnapshot>(builder.publish());
    ScrollDevice d;GiMainGpuBridge bridge;
    AIVERSE_TEST_CHECK(bridge.initialize(d,cfg,4));
    d.beginFrame(16,16);
    AIVERSE_TEST_CHECK(bridge.publishGeometry(scene,1) && bridge.publishLights({}));
    AIVERSE_TEST_CHECK(bridge.residency().excludedRemoteChunks==1);
    AIVERSE_TEST_CHECK(!bridge.canPresent());
    for(int f=0;f<16 && bridge.pendingProbes();++f) {
        AIVERSE_TEST_CHECK(bridge.updateFrame({0,0,0}));++d.submitted;d.completed=d.submitted;bridge.poll();
    }
    AIVERSE_TEST_CHECK(!bridge.pendingProbes() && bridge.canPresent() && d.clears==0);

    // Half a cell of motion is inside the dead band: nothing scrolls.
    AIVERSE_TEST_CHECK(bridge.updateFrame({.5f,0,0}) && bridge.scrollStats().scrolls==0);
    // 1.2 cells: cascade 0 (spacing 1) moves one cell; cascade 1 (spacing 2) not yet.
    const size_t tracesBefore=d.traces;
    AIVERSE_TEST_CHECK(bridge.updateFrame({1.2f,0,0}));
    AIVERSE_TEST_CHECK(bridge.scrollStats().scrolls==1 && bridge.scrollStats().recycledProbes==16);
    AIVERSE_TEST_CHECK(near(bridge.cascade(0).origin.x,-1) && near(bridge.cascade(1).origin.x,-4));
    AIVERSE_TEST_CHECK(bridge.cascadeRing(0)[0]==1 && bridge.cascadeRing(1)[0]==0);
    // The clear came first (zero samples) and the trace of the same probes followed.
    AIVERSE_TEST_CHECK(d.clears==1 && d.traces==tracesBefore+1 && d.lastSamples==16);
    // Presentation continues while the recycled probes are re-traced.
    AIVERSE_TEST_CHECK(bridge.pendingProbes()>0 && bridge.canPresent());
    const auto traced=d.lastIndices();
    for(auto i:traced)AIVERSE_TEST_CHECK(i<64);                           // cascade 0 slots
    ++d.submitted;d.completed=d.submitted;bridge.poll();
    for(int f=0;f<8 && bridge.pendingProbes();++f) {
        AIVERSE_TEST_CHECK(bridge.updateFrame({1.2f,0,0}));++d.submitted;d.completed=d.submitted;bridge.poll();
    }
    AIVERSE_TEST_CHECK(!bridge.pendingProbes() && bridge.canPresent());

    // Moving far: the outer cascade leaves the planned box, the resident set is
    // re-planned around it: the two-triangle chunk at x=40 becomes resident
    // and the one near the start is left behind.
    float x=1.2f;
    for(int f=0;f<200 && bridge.residency().relevantTriangles!=2;++f) {
        x+=1.0f;bridge.updateFrame({x,0,0});++d.submitted;d.completed=d.submitted;bridge.poll();
    }
    AIVERSE_TEST_CHECK(bridge.residency().relevantTriangles==2 && bridge.residency().excludedRemoteChunks==1);
    AIVERSE_TEST_CHECK(bridge.scrollStats().residencyReplans>0);
    AIVERSE_TEST_CHECK(bridge.canPresent());
    // Each probe of the moved cascades still sits at its own world cell.
    for(int k=0;k<2;++k) {
        const auto c=bridge.cascade(k);
        AIVERSE_TEST_CHECK(std::abs(c.origin.x+c.spacing*2-x)<=c.spacing*2);
    }
    d.endFrame();
    AIVERSE_TEST_CHECK(bridge.shutdownAfterGpuIdle());
    AIVERSE_TEST_CHECK(d.buffers.empty() && d.textures.empty());
    return 0;
}
