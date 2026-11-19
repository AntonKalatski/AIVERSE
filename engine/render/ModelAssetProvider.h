// Native-project model provider: read, own, upload and retire ordinary
// authored model files without RTS/FPS/editor classes. GPU publication and
// retirement are always performed on the device owner thread.
#pragma once
#include "Model.h"
#include "ModelResourceCache.h"
#ifndef __EMSCRIPTEN__
#include "AsyncModelCpuQueue.h"
#endif
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
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

    void setReader(FileReader reader) {
#ifndef __EMSCRIPTEN__
        cpuQueue_.shutdown(); // old reader/project may be destroyed after this
#endif
        reader_=std::move(reader);
    }
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
        // Backpressure pauses NEW publication only; existing resources
        // and GPU-in-flight retirement leases must not be dropped.
        if(resources_.publicationBackpressured())return nullptr;
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

#ifndef __EMSCRIPTEN__
    using CpuStats=AsyncModelCpuQueue::Stats;
    CpuStats cpuStats() const noexcept {return cpuQueue_.stats();}
    // Background CPU decoding never touches the device or its buffers.
    // A render-thread caller must publishReady() in a later frame.
    bool requestAsync(
            const std::string& source,
            ModelLoadPriority priority=ModelLoadPriority::Visible) {
        if(source.empty() || resources_.contains(source) ||
           resources_.publicationBackpressured())return false;
        return cpuQueue_.request(source,reader_,resources_.textures(),priority);
    }
    // A count cap is hard. The elapsed-time cap is advisory: a single
    // GPU upload may exceed it; never abandon a completed CPU result.
    // budgetMs<=0 disables the wall-time limit.
    size_t publishReady(gfx::Device& device,size_t maxCount=1,
                        double budgetMs=0) {
        const auto started=std::chrono::steady_clock::now();
        size_t published=0;
        while(published<maxCount && !resources_.publicationBackpressured()) {
            if(published>0 && budgetMs>0) {
                const auto elapsed=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-started).count();
                if(elapsed>=budgetMs)break;
            }
            AsyncModelCpuQueue::Prepared prepared;
            if(!cpuQueue_.pollReady(prepared))break;
            for(auto& warning:prepared.warnings)
                warnings_.push_back(std::move(warning));
            if(resources_.contains(prepared.source))continue;
            if(prepared.model && !uploadModel(device,*prepared.model)) {
                warnings_.push_back(prepared.source+": model GPU upload failed");
                releaseModel(device,*prepared.model);
                prepared.model.reset();
            }
            resources_.insertIfAbsent(prepared.source,
                                      std::move(prepared.model));
            ++published;
        }
        return published;
    }
#endif
    bool publicationBackpressured() const noexcept {
        return resources_.publicationBackpressured();
    }
    template<class OnDetach>
    size_t invalidate(gfx::Device& device,
                      const std::vector<std::string>& sources,
                      OnDetach&& onDetach) {
#ifndef __EMSCRIPTEN__
        for(const auto& source:sources)cpuQueue_.invalidate(source);
#endif
        return resources_.invalidateSources(device,sources,
                                            std::forward<OnDetach>(onDetach));
    }
    bool collectRetired(gfx::Device& device) {
        return resources_.collectRetired(device);
    }
    void shutdown(gfx::Device& device) {
#ifndef __EMSCRIPTEN__
        cpuQueue_.shutdown(); // CPU source data never outlives texture cache
#endif
        resources_.shutdown(device);
    }
private:
    FileReader reader_;
    ModelResourceCache resources_;
#ifndef __EMSCRIPTEN__
    AsyncModelCpuQueue cpuQueue_;
#endif
    std::vector<std::string> warnings_;
};

} // namespace aiverse::render
