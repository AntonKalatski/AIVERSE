// AIVERSE LENS: optional offscreen camera integration for native app clients.
// Does not depend on FPS/RTS, platform filesystem or any gameplay scene type.
// The client controls the gfx frame, scene submission and GPU-idle shutdown.
#pragma once
#include "CameraRuntimeHost.h"
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace aiverse::render {
class CameraClientAdapter {
    CameraRuntimeHost host_;
    bool loaded_=false;
    uint64_t frame_=0;
public:
    // Supply bytes from project content. Screen cameras are deliberately
    // rejected because an existing game client owns its presented frame.
    // Native hosts that own the entire screen can use CameraRuntimeHost directly.
    bool loadOffscreenAsset(const std::string& json,std::string* error=nullptr) {
        if(loaded_) {
            if(error)*error="Camera client adapter is already configured";
            return false;
        }
        CameraRuntimeHost candidate;
        if(!candidate.loadAsset(json,error))return false;
        for(Entity e:candidate.entities()) {
            const auto* output=candidate.world().tryGetConst<CameraOutputComponent>(e);
            if(!output || output->kind!=CameraOutputKind::Texture ||
               output->targetAssetKey==0) {
                if(error)*error="Client camera asset requires texture-only outputs";
                return false;
            }
        }
        // Keep host ownership unchanged until explicit GPU-idle shutdown;
        // no opaque buffer/image handles escape ECS or asset JSON.
        if(!host_.loadAsset(json,error))return false;
        loaded_=true;
        return true;
    }
    bool init(gfx::Device& device,Renderer3D& renderer) {
        return loaded_ && host_.init(device,renderer);
    }
    template<class SceneSubmit>
    CameraExecutionResult render(double seconds,float dt,SceneSubmit&& submit,
                                  Vec3 renderOrigin={}) {
        return host_.render(++frame_,seconds,dt,
                            std::forward<SceneSubmit>(submit),renderOrigin);
    }
    bool loaded()const{return loaded_;}
    CameraRuntimeHost& host(){return host_;}
    const CameraRuntimeHost& host()const{return host_;}
    void shutdownAfterGpuIdle() {
        host_.shutdownAfterGpuIdle();
        loaded_=false;frame_=0;
    }
};
} // namespace aiverse::render
