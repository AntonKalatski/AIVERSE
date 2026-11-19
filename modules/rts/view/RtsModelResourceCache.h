// Per-host model resource residency for RTS presentation.
// The cache owns models and decoded/uploaded textures; game rules,
// animation decisions, asset choices and View packet construction live
// outside this class. GPU operations must run on the render thread.
#pragma once
#include "engine/render/Model.h"
#include "engine/render/ModelTextureCache.h"
#ifndef __EMSCRIPTEN__
#include "RtsModelPreparationService.h"
#endif
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace aiverse::game::view_adapter {

class RtsModelResourceCache final {
public:
    using ModelMap=std::map<std::string,std::unique_ptr<render::Model>>;
    // Incremented by every cache-owned insertion, replacement, eviction,
    // and shutdown; the map is exposed read-only so writes cannot bypass it.
    uint64_t generation() const noexcept {return generation_;}

    struct Stats {
        size_t models=0,textures=0,decodedCpuBytes=0;
    };

#ifndef __EMSCRIPTEN__
    RtsModelPreparationService& preparation() noexcept {return preparation_;}
    const RtsModelPreparationService& preparation() const noexcept {
        return preparation_;
    }
#endif
    render::Model* find(const std::string& key) noexcept {
        const auto it=models_.find(key);
        return it==models_.end()?nullptr:it->second.get();
    }
    const render::Model* find(const std::string& key) const noexcept {
        const auto it=models_.find(key);
        return it==models_.end()?nullptr:it->second.get();
    }
    bool contains(const std::string& key) const {
        return models_.count(key)!=0; // null entries cache failed loads
    }
    // Insert without replacing a resident or a cached failed load.
    // The source model may already have been uploaded on the render thread.
    render::Model* insertIfAbsent(std::string key,
                                  std::unique_ptr<render::Model> model) {
        auto [it,inserted]=models_.try_emplace(std::move(key),std::move(model));
        if(inserted)++generation_;
        return it->second.get();
    }
    // For generated fallback assets and explicit reloads. Release the old
    // GPU buffers and invalidate host references before replacing them.
    template<class OnRelease>
    render::Model* replace(gfx::Device& dev,std::string key,
                           std::unique_ptr<render::Model> replacement,
                           OnRelease&& onRelease) {
        auto it=models_.find(key);
        if(it!=models_.end()) {
            if(it->second) {
                onRelease(it->second.get());
                render::releaseModel(dev,*it->second);
            }
            it->second=std::move(replacement);
            ++generation_;
            return it->second.get();
        }
        return insertIfAbsent(std::move(key),std::move(replacement));
    }

    // Publish a CPU-prepared model on the render thread. A synchronous
    // load may have installed this key while the worker ran; do not
    // overwrite that resource or upload its duplicate.
    template<class Prepared>
    bool publishPrepared(gfx::Device& dev,const std::string& key,
                         Prepared&& prepared) {
        if(models_.count(key))return false;
        if(prepared.model)render::uploadModel(dev,*prepared.model);
        insertIfAbsent(key,std::move(prepared.model));
        return true;
    }

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
            ++generation_;
        }
        textures_->trim(dev);
        return released;
    }

    // Explicitly release GPU assets before destroying the device.
    // No async loader may still reference these objects at this barrier.
    void shutdown(gfx::Device& dev) {
#ifndef __EMSCRIPTEN__
        preparation_.reset(); // join all texture/model CPU workers before release
#endif
        for(auto& [key,model]:models_) {
            (void)key;
            if(model)render::releaseModel(dev,*model);
        }
        models_.clear();
        ++generation_;
        textures_->shutdown(dev);
    }
private:
#ifndef __EMSCRIPTEN__
    RtsModelPreparationService preparation_;
#endif
    uint64_t generation_=0;
    ModelMap models_;
    std::shared_ptr<render::ModelTextureCache> textures_=
        std::make_shared<render::ModelTextureCache>();
};

} // namespace aiverse::game::view_adapter
