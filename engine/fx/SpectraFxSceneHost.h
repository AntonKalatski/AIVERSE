// SpectraFX reusable opt-in scene presentation host for native games.
// No FPS/RTS/gameplay dependencies; clients supply a file reader, optional
// authored cue events and a camera. Graphics operations stay on the device
// thread. Does not own gfx::Device or the authoritative game Simulation.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include "ParticleSystem.h"
#include "FxModelMesh.h"
#include "SpectraFxGpuRoutes.h"
#include "../render/Renderer3D.h"
#include "../render/TextureCache.h"

namespace aiverse::fx {

class SpectraFxSceneHost {
public:
    using FileReader=std::function<bool(const std::string&,std::string&)>;
    SpectraFxSceneHost()=default;
    SpectraFxSceneHost(const SpectraFxSceneHost&)=delete;
    SpectraFxSceneHost& operator=(const SpectraFxSceneHost&)=delete;
    ~SpectraFxSceneHost()=default; // call shutdown before Device destruction

    // Missing fx/gpu/routes.json is normal: no new VFX resources or
    // behavior are introduced for projects that have not opted in.
    bool init(gfx::Device& device,FileReader reader,
              std::string textureBase={}) {
        shutdown();
        lastError_.clear();
        if(!reader) {lastError_="SpectraFX scene host missing reader";return false;}
        std::string routesText;
        if(!reader("fx/gpu/routes.json",routesText)) return true;
        SpectraFxGpuRouteAsset::Routes routes;
        SpectraFxGpuBudgetGovernor::Config budget;
        if(!SpectraFxGpuRouteAsset::readText(routesText,routes,&lastError_,
                                            &budget)) return false;
        if(routes.empty()) return true;
        reader_=std::move(reader);
        textureBase_=std::move(textureBase);
        textures_.init(device);
        cpu_.setQuality(FxQuality::Medium);
        cpu_.setFileReader(reader_);
        cpu_.setTextureLoader([this](const std::string& name,FxTexture& out){
            const auto texture=textures_.resolve(name,false,textureBase_);
            if(!texture.valid()) return false;
            out={texture.tex,texture.u0,texture.v0,texture.u1,texture.v1};
            return true;
        });
        cpu_.setMeshLoader([this](const std::string& path,FxMesh& out) {
            return meshFromModelFile(path,reader_,out);
        });
        if(!cpu_.init(&device)) {
            lastError_=cpu_.lastError();
            cpu_.shutdown();
            textures_.shutdown();
            reader_={};
            return false;
        }
        library_=std::make_unique<SpectraFxGpuEmitterLibrary>(reader_);
        playback_=std::make_unique<SpectraFxHybridPlayback>(
            device,cpu_,*library_);
        playback_->configureGpuBudget(budget);
        for(const auto& [type,route]:routes)
            if(route.playback.enabled)
                playback_->route(uint32_t(type),route.playback);
        enabled_=true;
        return true;
    }

    bool enabled() const {return enabled_;}
    const std::string& lastError() const {return lastError_;}
    bool dispatch(const SpectraFxCue& cue) {
        return playback_ && playback_->dispatch(cue);
    }
    // Presentation discontinuity: rollback/new match/floating-origin shift.
    // Leaves authoring routes and the CPU effect file cache installed.
    void resetScene() {
        if(playback_) playback_->reset();
        if(enabled_) cpu_.clear();
    }
    // Inside gfx::Device::beginFrame/endFrame and before an open pass.
    void prepareGpu(float dt,const render::RTSCamera& camera) {
        if(!enabled_ || !playback_) return;
        const auto eye=camera.eye();
        const auto forward=render::normalize(camera.target-eye);
        playback_->updateGpu(std::clamp(dt,0.0f,0.1f),eye,forward);
    }
    // After renderer.beginScene, before the open scene render pass begins.
    // The CPU particle system owns its own effect updates exactly once.
    void prepareCpu(float dt,const render::RTSCamera& camera,
                    const render::mat4& viewProj,int width,int height) {
        if(!enabled_) return;
        cpu_.setView(camera,viewProj,width,height);
        if(playback_) playback_->playGpuImpactEffects(32);
        cpu_.update(std::clamp(dt,0.0f,0.1f));
    }
    // With the main colour/depth pass open, after Renderer3D::render.
    void render(const render::Lighting& lighting,
                const render::mat4& viewProj,
                const render::RTSCamera& camera) {
        if(!enabled_) return;
        cpu_.setLightDir(lighting.sunDir);
        cpu_.render({lighting.fogColor,lighting.fogDensity});
        if(playback_) {
            const auto view=camera.view();
            playback_->renderGpu(viewProj,
                {view.at(0,0),view.at(0,1),view.at(0,2)},
                {view.at(1,0),view.at(1,1),view.at(1,2)});
        }
    }
    void shutdown() {
        if(playback_) playback_.reset(); // all GPU resources before RHI
        library_.reset();
        if(enabled_) cpu_.shutdown();
        if(enabled_) textures_.shutdown();
        enabled_=false;
        reader_={};
        textureBase_.clear();
    }
    const SpectraFxHybridPlayback* playback() const {
        return playback_.get();
    }
private:
    ParticleSystem cpu_;
    render::TextureCache textures_;
    std::unique_ptr<SpectraFxGpuEmitterLibrary> library_;
    std::unique_ptr<SpectraFxHybridPlayback> playback_;
    FileReader reader_;
    std::string textureBase_;
    std::string lastError_;
    bool enabled_=false;
};

} // namespace aiverse::fx
