// Native-project model provider: read, own, upload and retire ordinary
// authored model files without RTS/FPS/editor classes. GPU publication and
// retirement are always performed on the device owner thread.
#pragma once
#include "Model.h"
#include "ModelResourceCache.h"
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {

class ModelAssetProvider final {
public:
    explicit ModelAssetProvider(FileReader reader={})
        :reader_(std::move(reader)){}
    ModelAssetProvider(const ModelAssetProvider&)=delete;
    ModelAssetProvider& operator=(const ModelAssetProvider&)=delete;

    void setReader(FileReader reader) {reader_=std::move(reader);}
    ModelResourceCache& resources() noexcept {return resources_;}
    const ModelResourceCache& resources() const noexcept {return resources_;}
    const std::vector<std::string>& warnings() const noexcept {return warnings_;}
    void clearWarnings() {warnings_.clear();}
    Model* find(const std::string& source) noexcept {
        return resources_.find(source);
    }
    const Model* find(const std::string& source) const noexcept {
        return resources_.find(source);
    }

    // Returns a BORROWED pointer stable until invalidation/shutdown.
    // A failed import is cached as a null entry and can be recovered by
    // invalidating that source. Do not call while renderer submissions
    // reference a model whose source is being replaced.
    Model* load(gfx::Device& device,const std::string& source) {
        if(source.empty())return nullptr;
        if(resources_.contains(source))return resources_.find(source);
        std::unique_ptr<Model> model;
        try {
            if(!reader_)throw std::runtime_error("missing model file reader");
            model=loadModel(source,reader_,&warnings_,resources_.textures());
            if(model) {
                prepareModelUpload(*model);
                if(!uploadModel(device,*model))
                    throw std::runtime_error("model GPU upload failed");
            }
        }catch(const std::exception& e) {
            warnings_.push_back(source+": "+e.what());
            // Failed GPU upload may leave temporary allocations.
            if(model)releaseModel(device,*model);
            model.reset();
        }
        return resources_.insertIfAbsent(source,std::move(model));
    }

    template<class OnDetach>
    size_t invalidate(gfx::Device& device,
                      const std::vector<std::string>& sources,
                      OnDetach&& onDetach) {
        return resources_.invalidateSources(device,sources,
                                            std::forward<OnDetach>(onDetach));
    }
    bool collectRetired(gfx::Device& device) {
        return resources_.collectRetired(device);
    }
    void shutdown(gfx::Device& device) {resources_.shutdown(device);}
private:
    FileReader reader_;
    ModelResourceCache resources_;
    std::vector<std::string> warnings_;
};

} // namespace aiverse::render
