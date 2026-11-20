#include "StudioFacialGpuPreview.h"
#include "apps/studio-native/StudioFacialPreviewPaths.h"
#include "engine/render/ModelAssetProvider.h"
#include <aiverselib/animation/FacialControlRig.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::studio {
using namespace aiverse::render;

struct StudioFacialGpuPreview::Resident {
    std::unique_ptr<ModelAssetProvider> models;
    std::shared_ptr<const lib::animation::FacialControlRig> program;
    lib::animation::FacialControlState state;
    Animator animator;
    const Model* model=nullptr; // owned ONLY by models above
    Pose pose;
    std::vector<mat4> nodeMatrices;
    bool playing=false;
};

StudioFacialGpuPreview::StudioFacialGpuPreview(gfx::Device& device)
    :deviceLifetime_(std::make_unique<RenderDeviceLifetime>(device)) {}

StudioFacialGpuPreview::~StudioFacialGpuPreview() {
    // If a native caller skipped explicit shutdown, never implicitly
    // destroy a potentially GPU-visible Model provider. Diagnose this
    // path through the owning StudioMetalScene shutdown error instead.
    if(live_)(void)live_.release();
}

void StudioFacialGpuPreview::retireCurrent(gfx::Device& device) {
    if(!live_)return;
    // Rendering submitted CPU read leases have already gone out of scope
    // after Renderer3D::render. GPU writes may be pending; do not rely
    // on an AppearanceAsset shared_ptr as proof of completion.
    if(!device.waitForResourceIdle()) {
        (void)live_.release(); // quarantine until true device teardown
        throw std::runtime_error("Facial preview GPU completion not proven");
    }
    live_->program.reset();
    live_->animator.setModel(nullptr);
    live_->pose.clear();
    live_->nodeMatrices.clear();
    live_->model=nullptr;
    if(live_->models) {
        try {live_->models->shutdown(device);}
        catch(...) {
            (void)live_.release();
            throw;
        }
        live_->models.reset();
    }
    live_.reset();
    currentGeneration_=0;
    renderedGeneration_=0;
}

void StudioFacialGpuPreview::load(
    gfx::Device& device,RTSCamera& camera,
    const StudioFacialPreviewState& request) {
    if(!request.active || request.projectRoot.empty() ||
       request.modelKey.empty() || !deviceLifetime_->lease().validFor(device))
        throw std::invalid_argument("Facial preview GPU epoch/project/model unavailable");
    // Avoid dropping the prior resident Model until GPU completion.
    retireCurrent(device);
    auto next=std::make_unique<Resident>();
    try {
        const auto reader=[root=request.projectRoot](
            const std::string& key,std::string& bytes) {
            return studioReadFacialAsset(root,key,bytes);
        };
        next->models=std::make_unique<ModelAssetProvider>(reader);
        next->models->setGpuDeviceLease(deviceLifetime_->lease());
        Model* model=next->models->load(device,request.modelKey);
        if(!model) {
            const auto& warnings=next->models->warnings();
            throw std::runtime_error("Native facial model import/upload failed: "+
                request.modelKey+(warnings.empty()?std::string{}:
                                  std::string(" — ")+warnings.back()));
        }
        next->model=model;
        // The MODEL ITSELF authorizes semantic face morph targets/rig bones.
        // The browser's schema validation cannot bypass this native compile.
        next->program=std::make_shared<const lib::animation::FacialControlRig>(
            lib::animation::FacialControlRig::compile(
                *model,request.authoredRig,request.controls));
        next->animator.setModel(model);
        if(!model->clips.empty()) {
            Animator::Play play;
            play.loop=true;
            play.restart=true;
            play.fade=0.f;
            // Host owns the sole base Animator, never a second Graph player.
            next->animator.play(0,0,play);
            next->playing=true;
        }
        const float3 min=model->boundsMin,max=model->boundsMax;
        const float3 center=(min+max)*.5f;
        const float extent=std::max({
            std::abs(max.x-min.x),std::abs(max.y-min.y),
            std::abs(max.z-min.z),0.25f});
        camera.target=center;
        camera.distance=std::clamp(extent*2.4f,1.5f,150.f);
        camera.yaw=.25f;
        camera.pitch=.35f;
        camera.minDistance=.3f;
        live_=std::move(next);
        currentGeneration_=request.generation;
        renderedGeneration_=0; // emits ready ONLY after an actual GPU frame
    }catch(...) {
        if(next && next->models) {
            try {next->models->shutdown(device);}
            catch(...) {
                // A partly uploaded Model is GPU-visible even on failed
                // compilation. Intentionally leak the whole owner if its
                // completion cannot be proven, not a use-after-free.
                (void)next.release();
            }
        }
        throw;
    }
}

void StudioFacialGpuPreview::frame(
    gfx::Device& device,Renderer3D& renderer,RTSCamera& camera,
    int width,int height,const StudioFacialPreviewState& request) {
    if(!request.active)throw std::logic_error("Inactive Facial preview frame");
    if(width<2 || height<2)return;
    if(!live_ || currentGeneration_!=request.generation)
        load(device,camera,request);
    if(!live_ || !live_->model || !live_->program)
        throw std::runtime_error("Facial GPU asset/rig is not resident");
    // The cache's MODEL read lease protects CPU metadata until after the
    // synchronous Renderer3D::render. It does not replace GPU-fence proof.
    const auto pin=live_->models->resources().acquireModelRead(live_->model);
    if(!pin)
        throw std::runtime_error("Facial GPU model lease expired");
    if(live_->playing)live_->animator.update(1.f/60.f);
    live_->animator.evaluate(live_->pose);
    std::string error;
    if(!live_->program->apply(
        *live_->model,request.controls,live_->pose,live_->state,
        1.f/60.f,1.f,&error))
        throw std::runtime_error("Native facial rig rejected pose: "+error);
    worldMatrices(*live_->model,live_->pose,live_->nodeMatrices);

    device.beginFrame(width,height);
    renderer.beginScene(camera,width,height);
    renderer.lighting=Lighting{};
    renderer.lighting.sunIntensity=1.1f;
    renderer.lighting.skyColor={.46f,.56f,.72f};
    renderer.lighting.groundColor={.26f,.28f,.36f};
    renderer.lighting.fogDensity=0.f;
    renderer.lights.add({1.8f,3.0f,2.0f},8.f,{1.f,.95f,.86f},2.5f);
    ModelDraw draw;
    draw.model=live_->model;
    draw.pose=&live_->pose;
    draw.nodeWorld=&live_->nodeMatrices;
    draw.persistentId=1;
    renderer.drawModel(draw);
    gfx::PassDesc pass;
    pass.clearColor[0]=.045f;pass.clearColor[1]=.066f;
    pass.clearColor[2]=.098f;pass.clearColor[3]=1.f;
    renderer.render(pass);
    device.endPass();
    device.endFrame();
    renderedGeneration_=request.generation;
}

void StudioFacialGpuPreview::shutdown(gfx::Device& device) {
    retireCurrent(device);
    if(deviceLifetime_) {
        deviceLifetime_->retire();
        deviceLifetime_.reset();
    }
}

} // namespace aiverse::studio
