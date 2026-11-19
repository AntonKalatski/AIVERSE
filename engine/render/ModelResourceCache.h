// Generic per-host model/texture residency shared by presentation hosts.
// The cache owns models and decoded/uploaded textures; game rules,
// animation decisions, asset choices and View packet construction live
// outside this class. GPU operations must run on the render thread.
#pragma once
#include "Model.h"
#include "ModelTextureCache.h"
#include "AsyncResourceRetirementQueue.h"
#include "ModelResidentEstimate.h"
#include "RenderDeviceLifetime.h"
#ifndef __EMSCRIPTEN__
#include "ModelPreparationService.h"
#endif
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <limits>
#include <stdexcept>
#include <thread>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace aiverse::render {

class ModelResourceCache final {
public:
    // Observer registration has an independent lifetime marker. It expires
    // as soon as destruction begins, before any Model or GPU lease members.
    // Group snapshots and owner destruction must share the device thread.
    ~ModelResourceCache() { lifetimeToken_.reset(); }
    std::weak_ptr<const void> lifetimeToken() const noexcept {
        return lifetimeToken_;
    }
    // Explicit owner-thread and GPU-device binding. First bind is done
    // by the host, before any resource upload or group registration.
    // Subsequent attempts from another device/thread fail closed.
    void bindResourceDevice(gfx::Device& device) const {
        const auto thread=std::this_thread::get_id();
        if(resourceEpoch_!=0 && !resourceEpochLease_.validFor(device))
            throw std::logic_error("ModelResourceCache expired GPU device epoch");
        if(resourceDevice_ &&
           (resourceDevice_!=&device || resourceThread_!=thread))
            throw std::logic_error("ModelResourceCache device/thread mismatch");
        if(!resourceDevice_) {
            resourceDevice_=&device;
            resourceThread_=thread;
        }
    }
    void bindResourceDevice(
            gfx::Device& device,const RenderDeviceLifetime::Lease& lease) const {
        if(!lease.validFor(device))
            throw std::logic_error("ModelResourceCache invalid device generation");
        bindResourceDevice(device);
        if(resourceEpoch_!=0 && resourceEpoch_!=lease.generation())
            throw std::logic_error("ModelResourceCache device generation changed");
        resourceEpoch_=lease.generation();
        resourceEpochLease_=lease;
    }
    uint64_t resourceDeviceGeneration() const noexcept {
        return resourceEpoch_;
    }
    bool boundToResourceDevice(const gfx::Device& device) const noexcept {
        return resourceDevice_==&device &&
               resourceThread_==std::this_thread::get_id() &&
               (resourceEpoch_==0 || resourceEpochLease_.validFor(device));
    }
    using ModelMap=std::map<std::string,std::unique_ptr<render::Model>>;
    // Incremented by every cache-owned insertion, replacement, eviction,
    // and shutdown; the map is exposed read-only so writes cannot bypass it.
    uint64_t generation() const noexcept {return generation_;}

    struct Stats {
        size_t models=0,textures=0,decodedCpuBytes=0;
    };

    // Model-cache keys include CPU geometry-policy variants; the source
    // file identity is independent of that variant and of load success.
    static std::string sourceForKey(std::string_view key) {
        constexpr std::string_view geometry="#geometry";
        if(key.ends_with(geometry))key.remove_suffix(geometry.size());
        if(key.empty())return {};
        return std::filesystem::path(std::string(key)).lexically_normal().generic_string();
    }
    struct AffectedModel {
        std::string key;
        const render::Model* model=nullptr; // null: failed-load tombstone
    };
    // Stable cache-key list for a changed source, including geometry
    // variants and failed/null cached loads. No assets are loaded here.
    std::vector<AffectedModel> affectedBySource(
            std::string_view source) const {
        std::vector<AffectedModel> result;
        const std::string normalized=sourceForKey(source);
        if(normalized.empty() || normalized.rfind("shape:",0)==0)return result;
        for(const auto& [key,model]:models_)
            if(sourceForKey(key)==normalized)
                result.push_back({key,model.get()});
        return result;
    }
    // Render-thread transaction. The host MUST detach TypeLook, pose/graph,
    // View packet, preview and other borrowed references in onDetach BEFORE
    // any GPU model is released. A throwing callback aborts without eviction.
    // Do not call while renderer submissions are in flight.
    template<class OnDetach>
    size_t invalidateSources(gfx::Device& dev,
                             const std::vector<std::string>& sources,
                             OnDetach&& onDetach) {
        bindResourceDevice(dev); // model must belong to this owner/device
        std::unordered_set<std::string> changed;
        for(const auto& source:sources)
            if(!source.empty() && source.rfind("shape:",0)!=0)
                changed.insert(sourceForKey(source));
        if(changed.empty())return 0;
        std::vector<AffectedModel> affected;
        for(const auto& [key,model]:models_)
            if(changed.contains(sourceForKey(key)))
                affected.push_back({key,model.get()});
        if(affected.empty())return 0;
#ifndef __EMSCRIPTEN__
        preparation_.reset(); // join CPU worker before cache mutation
#endif
        // Never attempt a partial eviction if a host has not detached.
        onDetach(affected);
        for(const auto& item:affected) {
            const auto it=models_.find(item.key);
            if(it==models_.end())continue;
            if(it->second)retired_.enqueue(std::move(it->second));
            models_.erase(it);
            ++generation_;
        }
        // GPU handles from stale models remain alive until collectRetired()
        // observes a definitive completed-work barrier on the owner thread.
        return affected.size();
    }

#ifndef __EMSCRIPTEN__
    ModelPreparationService& preparation() noexcept {return preparation_;}
    const ModelPreparationService& preparation() const noexcept {
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
        bindResourceDevice(dev); // old model moves into GPU-safe retirement queue
        auto it=models_.find(key);
        if(it!=models_.end()) {
            if(it->second) {
                onRelease(it->second.get());
                retired_.enqueue(std::move(it->second));
            }
            it->second=std::move(replacement);
            ++generation_;
            return it->second.get();
        }
        return insertIfAbsent(std::move(key),std::move(replacement));
    }

    // Commit an entire set of already uploaded, unpublished native Models.
    // Both maps have the SAME node type; after validating all keys, node
    // extraction/transfer does not allocate GPU objects or CPU map nodes.
    // Model* addresses remain stable in appearance packets and attachments.
    void publishStagedModels(ModelMap& staged) {
        for(const auto& [key,model]:staged) {
            (void)model;
            if(models_.contains(key))
                throw std::logic_error("speculative Model key already resident: "+key);
        }
        while(!staged.empty()) {
            auto node=staged.extract(staged.begin());
            models_.insert(std::move(node));
            ++generation_;
        }
    }

    // A failed appearance transaction must not destroy GPU geometry or
    // model textures while previous frame submissions can still use them.
    // Move a scratch Model into the SAME GPU-fenced retirement queue used
    // for ordinary resident models. On allocator failure the caller still
    // owns the Model and an explicit full-idle fallback is required.
    void retireUnpublished(gfx::Device& dev,std::unique_ptr<Model>& model) noexcept {
        if(!model)return;
        try {
            bindResourceDevice(dev);
            retired_.enqueueFrom(model);
        }catch(...) {
            ++stagingQueueFailures_;
            try {
                if(dev.waitForResourceIdle()) {
                    render::releaseModel(dev,*model);
                    model.reset();
                } else {
                    // A failed idle barrier is not a release authorization.
                    // Keep handles alive until device teardown rather than
                    // potentially freeing GPU-visible memory.
                    (void)model.release();
                    ++stagingOrphanModels_;
                }
            }catch(...) {
                (void)model.release();
                ++stagingOrphanModels_;
            }
        }
    }
    uint64_t stagingQueueFailures() const noexcept{return stagingQueueFailures_;}
    uint64_t stagingOrphanModels() const noexcept{return stagingOrphanModels_;}

    // Publish a CPU-prepared model on the render thread. A synchronous
    // load may have installed this key while the worker ran; do not
    // overwrite that resource or upload its duplicate.
    template<class Prepared>
    bool publishPrepared(gfx::Device& dev,const std::string& key,
                         Prepared&& prepared) {
        bindResourceDevice(dev);
        if(models_.count(key))return false; // in-flight worker results cannot be silently lost
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
        std::unordered_set<std::string> seen;
        for(const auto& [key,model]:models_) {
            (void)model;
            if(key.rfind("shape:",0)==0)continue;
            const std::string source=sourceForKey(key);
            if(!source.empty() && seen.insert(source).second)
                files.push_back(source);
        }
        return files;
    }

    // Retain shape fallbacks and any model still referenced by active
    // appearances, including attachments/LOD variants. Callback lets hosts
    // invalidate their own animation/graph caches BEFORE GPU destruction.
    template<class OnRelease>
    size_t trimUnused(gfx::Device& dev,
                      const std::unordered_set<const render::Model*>& used,
                      OnRelease&& onRelease) {
        bindResourceDevice(dev); // release happens at the host frame boundary
        size_t released=0;
        for(auto it=models_.begin();it!=models_.end();) {
            if(it->first.rfind("shape:",0)==0 ||
               used.contains(it->second.get())) {
                ++it;
                continue;
            }
            if(it->second) {
                onRelease(it->second.get());
                retired_.enqueue(std::move(it->second));
            }
            it=models_.erase(it);
            ++released;
            ++generation_;
        }
        return released;
    }

    using RetirementQueue=
        AsyncResourceRetirementQueue<std::unique_ptr<Model>>;
    using RetirementStats=RetirementQueue::Stats;
    using RetirementDiagnosticObserver=RetirementQueue::DiagnosticObserver;
    // The host may attach a read-only token trace, e.g. in a GPU debug
    // panel. Callback exceptions cannot interrupt resource retirement.
    void setRetirementDiagnosticObserver(RetirementDiagnosticObserver observer) {
        retired_.setDiagnosticObserver(std::move(observer));
    }
    size_t pendingRetirements() const noexcept{return retired_.pending();}
    RetirementStats retirementStats() const noexcept {
        return retired_.stats();
    }
    // 0 disables backpressure. This is an object-count bound, NOT an exact
    // byte/VRAM quota (shared images/geometry pools have other owners).
    void setRetirementSoftLimit(size_t count) noexcept {
        retirementSoftLimit_=count;
    }
    size_t retirementSoftLimit() const noexcept {
        return retirementSoftLimit_;
    }
    uint64_t estimatedRetiredGpuBytes() const {
        return retired_.estimatedPendingBytes(
            [](const std::unique_ptr<Model>& model)->uint64_t {
                return model?estimateModelResident(*model).knownBytes():0;
            });
    }
    struct GpuOwnerEstimate {
        uint64_t residentModelDedicatedEstimate=0;
        uint64_t retiredModelDedicatedEstimate=0;
        uint64_t sharedTextureRgba8Equivalent=0;
        size_t uniqueSharedTextures=0,compressedSharedTextures=0;
        size_t sharedGeometryResidentModels=0;
        // Shared geometry-pool bytes belong to the device-level pool,
        // NOT this host owner. Query geometryPoolStats(device) separately.
    };
    // Read-only enumeration for owner-group accounting. Each Model belongs
    // to this cache or its pending GPU-retirement batches; no pointer is
    // retained by the visitor unless the host guarantees its lifetime.
    // Source aliases/tombstones do not count as distinct live GPU Models.
    template<class Visit>
    void forEachOwnedModel(Visit&& visit) const {
        for(const auto& [key,model]:models_) {
            (void)key;
            if(model)visit(*model,false); // resident
        }
        retired_.forEachPending([&](const std::unique_ptr<Model>& model) {
            if(model)visit(*model,true); // waiting on its own GPU token
        });
    }

    // Models can share geometry pages across multiple hosts. Give the
    // device-level geometry pool ONE combined model set when computing
    // page capacity; include retired models until their GPU token completes.
    std::vector<const Model*> geometryPoolModelReferences() const {
        std::vector<const Model*> result;
        for(const auto& [key,model]:models_) {
            (void)key;
            if(model && model->geometryPoolPage)result.push_back(model.get());
        }
        retired_.forEachPending([&](const std::unique_ptr<Model>& model) {
            if(model && model->geometryPoolPage)result.push_back(model.get());
        });
        return result;
    }
    GpuOwnerEstimate gpuOwnerEstimate() const {
        GpuOwnerEstimate result;
        const auto max=std::numeric_limits<uint64_t>::max();
        auto add=[max](uint64_t& dst,uint64_t value) {
            dst=value>max-dst?max:dst+value;
        };
        for(const auto& [key,model]:models_) {
            (void)key;
            if(!model)continue;
            const auto estimate=estimateModelResident(*model);
            add(result.residentModelDedicatedEstimate,
                estimate.knownBytes());
            result.sharedGeometryResidentModels+=estimate.sharedGeometryModels;
        }
        result.retiredModelDedicatedEstimate=estimatedRetiredGpuBytes();
        const auto shared=textures_->gpuOwnerEstimate();
        result.sharedTextureRgba8Equivalent=
            shared.rgba8BaseMipEquivalentBytes;
        result.uniqueSharedTextures=shared.uniqueTextures;
        result.compressedSharedTextures=shared.compressedTextures;
        return result;
    }
    // 0 disables the estimated-byte admission limit. The estimate excludes
    // shared geometry pages, borrowed textures, morph height, driver padding
    // and hidden RHI allocations. Not an actual VRAM usage counter.
    void setEstimatedRetirementByteLimit(uint64_t value) noexcept {
        estimatedByteLimit_=value;
    }
    uint64_t estimatedRetirementByteLimit() const noexcept {
        return estimatedByteLimit_;
    }
    bool publicationBackpressured() const {
        return retired_.saturated(retirementSoftLimit_) ||
            (estimatedByteLimit_!=0 &&
             estimatedRetiredGpuBytes()>=estimatedByteLimit_);
    }
    // Schedule async completion tokens and poll old batches without waiting
    // for whole-GPU idle. Each batch covers exactly the resources queued
    // before its marker; newer evictions can NEVER ride an older fence.
    // Returns true only once all pending resources have been released.
    bool collectRetired(gfx::Device& dev) {
        bindResourceDevice(dev);
        if(retired_.empty())return true;
        const auto before=retired_.pending();
        const bool done=retired_.collect(
            [&]{return dev.beginResourceRetirement();},
            [&](uint64_t token){return dev.resourceRetirementComplete(token);},
            [&](uint64_t token) {
                // On normal completed batches close the native marker.
                // If a later unsupported-marker fallback proved full
                // device idle, a previous FAILED marker can be abandoned
                // without freeing any in-flight resource prematurely.
                if(dev.resourceRetirementStatus(token)==
                   gfx::ResourceRetirementStatus::Complete)
                    dev.endResourceRetirement(token);
                else
                    dev.abandonRetirementTokenAfterIdle(token);
            },
            [&]{return dev.waitForResourceIdle();},
            [&](std::unique_ptr<Model>& model) {
                if(model)render::releaseModel(dev,*model);
            },
            [&](uint64_t token) {
                return dev.resourceRetirementStatus(token)==
                    gfx::ResourceRetirementStatus::Failed;
            });
        if(retired_.pending()<before)textures_->trim(dev);
        return done;
    }

    // Explicitly release GPU assets before destroying the device.
    // No async loader may still reference these objects at this barrier.
    void shutdown(gfx::Device& dev) {
        bindResourceDevice(dev);
#ifndef __EMSCRIPTEN__
        preparation_.reset(); // join all texture/model CPU workers before release
#endif
        // Fail closed: no GPU objects (nor borrowed CPU Model storage) may
        // be destroyed if the backend cannot prove prior GPU completion.
        if(!dev.waitForResourceIdle())
            throw std::runtime_error("model cache shutdown: GPU retirement barrier failed");
        for(auto& [key,model]:models_) {
            (void)key;
            if(model)render::releaseModel(dev,*model);
        }
        models_.clear();
        ++generation_;
        // The successful barrier above makes every token safe to close,
        // including callbacks whose status notification is still queued.
        if(!retired_.drain([]{return true;},
            [&](uint64_t token) {
                // A successful waitForResourceIdle already proved safety.
                dev.abandonRetirementTokenAfterIdle(token);
            },
            [&](std::unique_ptr<Model>& model) {
                if(model)render::releaseModel(dev,*model);
            }))
            throw std::runtime_error("model cache: retired GPU resources not released");
        textures_->shutdown(dev);
        // The cache is empty and its GPU leases have been released. A
        // later init may bind this same host to a NEW device only after
        // every scoped observer for the old device has been released.
        resourceDevice_=nullptr;
        resourceThread_={};
        resourceEpoch_=0;
        resourceEpochLease_={};
    }
private:
    std::shared_ptr<const void> lifetimeToken_=
        std::make_shared<const uint8_t>(1);
    mutable gfx::Device* resourceDevice_=nullptr;
    mutable std::thread::id resourceThread_;
    mutable uint64_t resourceEpoch_=0;
    mutable RenderDeviceLifetime::Lease resourceEpochLease_;
#ifndef __EMSCRIPTEN__
    ModelPreparationService preparation_;
#endif
    uint64_t generation_=0;
    uint64_t stagingQueueFailures_=0,stagingOrphanModels_=0;
    size_t retirementSoftLimit_=128;
    uint64_t estimatedByteLimit_=0; // optional; never claims exact VRAM usage
    ModelMap models_;
    AsyncResourceRetirementQueue<std::unique_ptr<Model>> retired_;
    std::shared_ptr<render::ModelTextureCache> textures_=
        std::make_shared<render::ModelTextureCache>();
};

} // namespace aiverse::render
