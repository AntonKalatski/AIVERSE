// AIVERSE LENS Stage 20: GPU allocation fault injection without a native GPU.
#include "engine/render/SpriteBatch.h"
#include "modules/rts/view/Rts2DView.h"
#include "tests/cloth/FakeDevice.h"
#include <stdexcept>
#include <string>

namespace {
struct FaultDevice final : FakeDevice {
    int allocation=0, failAt=0, liveBuffers=0, liveShaders=0, livePipelines=0;
    bool idleAllowed=true;
    int idleRequests=0;
    bool waitForResourceIdle() override { ++idleRequests; return idleAllowed; }
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override {
        if (failAt && ++allocation == failAt) return {};
        ++liveShaders;
        return {next++};
    }
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override {
        if (failAt && ++allocation == failAt) return {};
        ++livePipelines;
        return {next++};
    }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& d) override {
        if (failAt && ++allocation == failAt) return {};
        auto h=FakeDevice::createBuffer(d);
        ++liveBuffers;
        return h;
    }
    void destroyShader(gfx::ShaderHandle h) override {
        if(h.valid())--liveShaders;
    }
    void destroyPipeline(gfx::PipelineHandle h) override {
        if(h.valid())--livePipelines;
    }
    void destroyBuffer(gfx::BufferHandle h) override {
        if(h.valid()) { FakeDevice::destroyBuffer(h); --liveBuffers; }
    }
    bool clean() const {
        return liveBuffers==0 && liveShaders==0 && livePipelines==0;
    }
};
void require(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(reason);
}
}

int aiverseMain(int,char**) {
    // Every allocation point is injected independently; both early and
    // late failures must leave no GPU allocations after a successful idle.
    for(int failure=1;failure<=32;++failure) {
        FaultDevice dev;
        dev.failAt=failure;
        aiverse::render::SpriteBatch sprites;
        bool initialized=sprites.init(dev);
        if (initialized) {
            require(sprites.shutdownAfterGpuIdle(),"successful shutdown failed");
        } else {
            require(dev.idleRequests>0,"failed init bypassed idle barrier");
            require(sprites.shutdownAfterGpuIdle(),"failed init recovery");
        }
        require(dev.clean(),"GPU allocation leak after init failure");
    }

    // GPU idle refusal must preserve handles for explicit later retry.
    {
        FaultDevice dev;
        dev.failAt=2;
        dev.idleAllowed=false;
        aiverse::render::SpriteBatch sprites;
        bool threw=false;
        try { (void)sprites.init(dev); }
        catch(const std::runtime_error&) { threw=true; }
        require(threw,"GPU idle refusal was not reported");
        require(!dev.clean(),"failed idle incorrectly destroyed handles");
        require(!sprites.shutdownAfterGpuIdle(),"idle refusal was not preserved");
        dev.idleAllowed=true;
        require(sprites.shutdownAfterGpuIdle(),"recovery after idle restoration failed");
        require(dev.clean(),"failed recovery leaked GPU allocations");
        require(sprites.shutdownAfterGpuIdle(),"repeated safe shutdown failed");
        dev.failAt=0;
        require(sprites.init(dev),"reinitialization after safe cleanup failed");
        require(sprites.shutdownAfterGpuIdle(),"reinitialized shutdown failed");
        require(dev.clean(),"reinitialization leaked GPU objects");
    }
    // Stage 22: a failed initialization retains the original device identity.
    // A different device must never be allowed to release those GPU handles.
    {
        struct FailedViewDevice final : FakeDevice {
            bool idleAllowed=false;
            int waits=0;
            gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override { return {}; }
            bool waitForResourceIdle() override { ++waits; return idleAllowed; }
        } owner, foreign;
        aiverse::game::Rules rules;
        aiverse::game::Rts2DView view;
        bool failed=false;
        try { (void)view.init(owner,rules,"",{},true); }
        catch(const std::runtime_error&) { failed=true; }
        require(failed && owner.waits>0,"failed init did not retain recovery state");
        bool rejected=false;
        try { (void)view.shutdownAfterGpuIdle(foreign); }
        catch(const std::logic_error&) { rejected=true; }
        require(rejected,"cross-device sprite view recovery was not rejected");
        require(foreign.waits==0,"wrong device was asked for GPU idle");
        owner.idleAllowed=true;
        require(view.shutdownAfterGpuIdle(owner),"correct-device retry failed");
        require(view.shutdownAfterGpuIdle(owner),"repeated cleanup was not idempotent");
    }
    return 0;
}
