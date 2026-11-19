// Per-host model resource residency for RTS presentation.
// The cache owns models and decoded/uploaded textures; game rules,
// animation decisions, asset choices and View packet construction live
// outside this class. GPU operations must run on the render thread.
#pragma once
#include "engine/render/Model.h"
#include "engine/render/ModelTextureCache.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace aiverse::game::view_adapter {

class RtsModelResourceCache final {
public:
    using ModelMap=std::map<std::string,std::unique_ptr<render::Model>>;
    struct Stats {
        size_t models=0,textures=0,decodedCpuBytes=0;
    };

    ModelMap& models() noexcept {return models_;}
    const ModelMap& models() const noexcept {return models_;}
    std::shared_ptr<render::ModelTextureCache> textures() const noexcept {
        return textures_;
    }
    Stats stats() const {
        return {models_.size(),textures_->gpuCount(),textures_->decodedBytes()};
    }
    std::vector<std::string> modelFiles() const {
        std::vector<std::string> files;
        for(const auto& [key,model]:models_)
            if(key.rfind("shape:",0)!=0)
                files.push_back(model?model->path:key);
        return files;
    }

    // Retain shape fallbacks and any model still referenced by active
    // appearances, including attachments/LOD variants. Callback lets hosts
    // invalidate their own animation/graph caches BEFORE GPU destruction.
    template<class OnRelease>
    size_t trimUnused(gfx::Device& dev,
                      const std::unordered_set<const render::Model*>& used,
                      OnRelease&& onRelease) {
        size_t released=0;
        for(auto it=models_.begin();it!=models_.end();) {
            if(it->first.rfind("shape:",0)==0 ||
               used.contains(it->second.get())) {
                ++it;
                continue;
            }
            if(it->second) {
                onRelease(it->second.get());
                render::releaseModel(dev,*it->second);
            }
            it=models_.erase(it);
            ++released;
        }
        textures_->trim(dev);
        return released;
    }

    // Explicitly release GPU assets before destroying the device.
    // No async loader may still reference these objects at this barrier.
    void shutdown(gfx::Device& dev) {
        for(auto& [key,model]:models_) {
            (void)key;
            if(model)render::releaseModel(dev,*model);
        }
        models_.clear();
        textures_->shutdown(dev);
    }
private:
    ModelMap models_;
    std::shared_ptr<render::ModelTextureCache> textures_=
        std::make_shared<render::ModelTextureCache>();
};

} // namespace aiverse::game::view_adapter
