// SpectraFX Stage 36: native C++ example, independent of game genre.
// Copy/include this header from a native project's *presentation* target.
// The host still owns Device::beginFrame/endFrame and the scene render pass.
// No GPU handle or renderer state is stored in the authoritative ECS World.
#pragma once

#include <aiverselib/genre/SpectraFX.h>
#include <string>

namespace aiverse::examples::spectrafx {

// Example-authoritative pose: only fixed-point game state is in ECS.
// Real native projects may resolve poses from animation/interpolation instead.
struct DemoPose {
    Vec3 position{};
    Vec3 forward{0_fx,1_fx,0_fx};
};

inline Entity spawnDemoSource(World& world,const std::string& effect,
                              Vec3 position) {
    Entity e=world.create();
    world.add<DemoPose>(e).position=position;
    auto& component=world.add<lib::genre::SpectraSourceComponent>(e);
    component.effect=effect;
    component.scale=1_fx; // strict pooled GPU subset requires unit scale
    component.enabled=true;
    component.killOnDetach=true;
    return e;
}

// The pool must outlive GpuEcsAttachmentPresenter; member declaration order
// ensures the bridge is destroyed first. Device must outlive this object.
class NativeAttachmentExample final {
public:
    NativeAttachmentExample() : bridge_(pool_) {}
    NativeAttachmentExample(const NativeAttachmentExample&)=delete;
    NativeAttachmentExample& operator=(const NativeAttachmentExample&)=delete;

    // Requires a Device belonging to the host's graphics lifetime.
    // Unsupported graphs keep the original EffectDef in shared CPU world.
    bool init(gfx::Device& device,const fx::EffectDef& authoredEffect) {
        spectra::PoolWorldBudgets budgets{};
        budgets.maxGpuGroups=8;
        budgets.maxGroupCapacity=1024;
        if(!pool_.init(&device,budgets))return false;
        spectra::GraphRuntimeOptions gpu{};
        gpu.preference=spectra::ComputePreference::PreferGpu;
        gpu.gpuProgram=true;
        gpu.gpuMaterial=true;
        if(!pool_.defineEffect("demo_flame",authoredEffect,gpu))return false;

        spectra::PoolContinuousEmission rate{};
        rate.burstsPerSecond=12.f;
        rate.metresPerBurst=0.5f;
        rate.maxBurstsPerFrame=4;
        return bridge_.bindEffect("demo_flame",rate);
    }

    // After Device.beginFrame and BEFORE host starts the scene pass.
    bool prepare(const World& committedWorld,uint64_t committedTick,
                 const render::RTSCamera& camera,const render::mat4& viewProj,
                 int width,int height,float dt) {
        if(frameOpen_)return false;
        if(!pool_.beginPresentationFrame(camera,viewProj,width,height))
            return false;
        frameOpen_=true;
        // Resolve only fixed ECS data. Presentation may substitute an
        // interpolated native render pose here without writing it to World.
        const auto resolve=[](const World& world,Entity entity,
                              fx::float3& pos,fx::float3& dir) {
            const DemoPose* pose=world.tryGetConst<DemoPose>(entity);
            if(!pose)return false; // temporarily missing => pause emission
            pos={float(pose->position.x.toDouble()),
                 float(pose->position.y.toDouble()),
                 float(pose->position.z.toDouble())};
            dir={float(pose->forward.x.toDouble()),
                 float(pose->forward.y.toDouble()),
                 float(pose->forward.z.toDouble())};
            return true;
        };
        if(!bridge_.syncCommitted(committedTick,committedWorld,resolve) ||
           !pool_.step(dt)) {
            pool_.endPresentationFrame();
            frameOpen_=false;
            return false;
        }
        return true;
    }

    // Called INSIDE the open depth-tested scene pass, after scene geometry.
    void drawInsidePass(const fx::FxFog& fog={}) {
        if(frameOpen_)pool_.draw(fog);
    }

    // Called AFTER the host ends its scene pass but BEFORE Device.endFrame().
    void finish() {
        if(!frameOpen_)return;
        pool_.endPresentationFrame();
        frameOpen_=false;
    }

    // On a network rollback/replay: first finish the current render frame,
    // then reset visual state outside encoders, before replaying the new tick.
    // Host owns restoring the authoritative World snapshot.
    bool afterRollback() {
        if(frameOpen_)return false;
        return bridge_.resetForRollback();
    }

    // Explicitly release resources while Device is still alive.
    void shutdown() {
        if(frameOpen_)finish();
        pool_.shutdown();
    }

    const spectra::PoolWorldStats& stats() const {return pool_.stats();}
    const std::string& reason() const {
        return bridge_.reason().empty()?pool_.reason():bridge_.reason();
    }
private:
    spectra::GpuPoolWorld pool_;
    spectra::GpuEcsAttachmentPresenter bridge_;
    bool frameOpen_=false;
};

// Example call sequence (pseudocode around host-owned RHI):
//   NativeAttachmentExample fx;
//   fx.init(device,compiledEffect);
//   World world; auto flame=spawnDemoSource(world,"demo_flame",{});
//   Device.beginFrame();
//   fx.prepare(world,lastCommittedTick,camera,viewProj,w,h,dt);
//   Device.beginPass(scenePass); ... draw geometry ...
//   fx.drawInsidePass(); Device.endPass();
//   fx.finish(); Device.endFrame();
//   // Later, to demonstrate generation safety:
//   world.destroy(flame);
//   auto replacement=spawnDemoSource(world,"demo_flame",newPosition);
//   // After a gameplay rollback at a safe graphics boundary:
//   if(fx.afterRollback()) { world.copyFrom(savedAuthoritativeSnapshot); }
//   // Never restart presentation replay unless visual reset succeeded.

} // namespace aiverse::examples::spectrafx
