// TerraWeave multi-domain VT sampling and GPU feedback routing.
// Independent Forest/Horizon/impostor archives each own a page table, physical
// atlas, immutable disk archive, streamer and GPU feedback render target.
// The packed feedback ID has NO domain field: feedback from each domain must
// be rendered into a separately configured pass and submitted only to its
// matching streamer. Main-scene model rendering remains one Renderer3D queue.
// GPU state is owned by the Device/render thread; no authoritative ECS logic.
#pragma once

#include "TerraWeaveVirtualAtlasArchive.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <tuple>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {

class TerraWeaveVirtualAtlasDomains {
public:
    using DomainId = uint32_t;
    using DomainLookup = std::function<bool(
        const Model&, const Material&, DomainId&, int&)>;

    struct Options {
        int feedbackWidth = 256;
        int feedbackHeight = 256;
        bool feedbackDepth = true;
        int physicalPages = 128;
        size_t bufferedDiskPages = 8;
        size_t maxDeferredFrames = 8;
        gfx::TextureStreamConfig gpuBudget{};
        gfx::VirtualTexturePrefetchConfig prefetch{};
    };
    struct StreamProgress {
        DomainId domain = 0;
        size_t uploadedPages = 0;
        bool capturedFeedbackPending = false;
        bool active = false;
    };

    explicit TerraWeaveVirtualAtlasDomains(gfx::Device& device)
        : device_(&device) {}
    TerraWeaveVirtualAtlasDomains(const TerraWeaveVirtualAtlasDomains&) = delete;
    TerraWeaveVirtualAtlasDomains& operator=(const TerraWeaveVirtualAtlasDomains&) = delete;
    ~TerraWeaveVirtualAtlasDomains() { shutdown(); }

    bool addDomain(DomainId id, Options settings) {
        if (!device_ || id == 0 || domains_.count(id) ||
            (feedback_ &&
             settings.feedbackDepth != feedback_->feedbackDepthAttachment()) ||
            domains_.size() >= 32 || settings.feedbackWidth < 1 ||
            settings.feedbackHeight < 1 ||
            settings.feedbackWidth > 4096 || settings.feedbackHeight > 4096 ||
            size_t(settings.feedbackWidth)*size_t(settings.feedbackHeight) >
                (size_t(4)<<20) ||
            settings.maxDeferredFrames < 1 || settings.maxDeferredFrames > 1024 ||
            settings.physicalPages < 1 || settings.bufferedDiskPages < 1 ||
            settings.bufferedDiskPages > 128)
            return false;
        Domain d;
        d.options = std::move(settings);
        d.session = std::make_unique<TerraWeaveVirtualAtlasSession>(
            *device_,d.options.gpuBudget);
        d.feedback = std::make_unique<gfx::VirtualTextureFeedbackPass>();
        domains_.emplace(id,std::move(d));
        return true;
    }

    // Creates the new disk-to-GPU streamer without affecting the current
    // atlas. This can occur while other domains continue to render.
    bool prepare(DomainId id,const gfx::VirtualTextureArchiveVersion& next) {
        Domain* d = find(id);
        if (!d) return false;
        return d->session->prepare(next,d->options.physicalPages,
                                   d->options.bufferedDiskPages);
    }
    // Import a Python/native immutable page archive into this specific
    // domain. The active atlas is untouched until explicit promote().
    // On failure inspect session(id)->lastError().
    bool prepareFromArchiveDirectory(
        DomainId id, const std::filesystem::path& directory,
        uint64_t importedRevision = 0,
        size_t maxArchiveBytes = size_t(512) << 20,
        size_t maxPages = 262144) {
        Domain* d=find(id);
        return d && d->session->prepareFromArchiveDirectory(
            directory,d->options.physicalPages,d->options.bufferedDiskPages,
            importedRevision,maxArchiveBytes,maxPages);
    }
    size_t pumpCandidate(DomainId id,size_t maxNewGpuPages=8) {
        Domain* d=find(id);
        return d ? d->session->pumpCandidate(maxNewGpuPages) : 0;
    }

    // The candidate's page-table geometry may differ from its predecessor.
    // Allocate its own decoder/feedback target before publishing the swap.
    // The previous pass must not have a pending or unconsumed readback.
    // Retired GPU resources remain in TerraWeaveVirtualAtlasSession until
    // the caller has observed a real graphics device fence.
    bool promote(DomainId id,bool requireFallback=true) {
        Domain* d=find(id);
        if (!d || !d->session->candidate() ||
            !d->session->candidate()->ready() ||
            (requireFallback && !d->session->candidateFallbackResident()) ||
            d->session->hasRetiredGpuResources() ||
            (d->feedback->ready() &&
             (d->feedback->active() || d->feedback->pending() ||
              d->feedback->readyToSubmit())))
            return false;
        const auto& version=d->session->candidateVersion();
        if (!version.valid()) return false;
        auto replacement=std::make_unique<gfx::VirtualTextureFeedbackPass>();
        gfx::VirtualTextureFeedbackConfig gpu;
        gpu.tilesX=version.baseTilesX;
        gpu.tilesY=version.baseTilesY;
        gpu.mipLevels=version.mipLevels;
        gpu.virtualLayers=version.virtualLayers;
        gpu.maxReadbackPixels=size_t(d->options.feedbackWidth)*
                              size_t(d->options.feedbackHeight);
        gfx::VirtualTexturePrefetchConfig prefetch=d->options.prefetch;
        prefetch.tilesX=gpu.tilesX;
        prefetch.tilesY=gpu.tilesY;
        prefetch.mipLevels=gpu.mipLevels;
        prefetch.virtualLayers=gpu.virtualLayers;
        if (!replacement->initialize(
                *device_,d->options.feedbackWidth,d->options.feedbackHeight,
                gpu,prefetch,d->options.maxDeferredFrames,
                d->options.feedbackDepth))
            return false;
        // Ownership remains stable until the new feedback decoder and its
        // atlas are both ready: a failed prepare never releases active GPU
        // page data. The old feedback pass is idle by construction.
        if (!d->session->promote(*d->feedback,requireFallback))
            return false;
        d->feedback.swap(replacement);
        return true;
    }

    // One mapping per model/material in the current frame. Explicit IDs are
    // not derived from a temporary pointer, atlas path or resource handle.
    // Call beginFrame() before submitting new Arbor/Flora/Impostor draws.
    void beginFrame() {
        assignments_.clear();
        if (feedback_) feedback_->beginFrame();
    }
    bool registerMaterial(const Model& model,const Material& material,
                          DomainId id,int layer=0) {
        if (!TerraWeaveVirtualTextureFeedback::isTerraWeaveModel(model) ||
            !domainLayerReady(id,layer))
            return false;
        assignments_[{&model,&material}]={id,layer};
        return true;
    }
    size_t registerDraw(const ModelDraw& draw,DomainId id,int layer=0) {
        if (!draw.model || !domainLayerReady(id,layer)) return 0;
        size_t count=0;
        for (const auto& material : draw.model->materials)
            count += registerMaterial(*draw.model,material,id,layer)?1u:0u;
        // Original material pointers and per-azimuth impostor overrides may
        // both be submitted by Renderer3D, depending on the primitive.
        if (draw.materials && draw.materials!=&draw.model->materials)
            for (const auto& material : *draw.materials)
                count += registerMaterial(*draw.model,material,id,layer)?1u:0u;
        return count;
    }
    void setLiveDomainLookup(DomainLookup fn) {
        liveLookup_=std::move(fn);
    }

    // Must be attached AFTER the ordinary Terra feedback adapters have
    // connected their model and optional animated-model feedback callbacks.
    // The manager owns only the callbacks; it does not own Renderer3D or the
    // Terra feedback adapter. Call detach() before destroying either.
    bool attach(Renderer3D& renderer,TerraWeaveVirtualTextureFeedback& feedback) {
        if (renderer_ || !feedback.ready()) return false;
        // Feedback pipeline depth state is selected when model shader
        // programs are created; every domain must use the same mode.
        for (const auto& [id,domain] : domains_) {
            (void)id;
            if (domain.options.feedbackDepth != feedback.feedbackDepthAttachment())
                return false;
        }
        renderer_=&renderer;
        feedback_=&feedback;
        feedback_->setLiveMaterialMapper(
            [this](const Model& model,const Material& material,
                   VirtualTextureMaterialMapping& mapping) {
                return resolveMapping(model,material,mapping);
            });
        feedback_->setFeedbackMaterialFilter(
            [this](const Model& model,const Material& material) {
                DomainId id=0; int layer=0;
                return selectedFeedbackDomain_!=0 &&
                       locate(model,material,id,layer) &&
                       id==selectedFeedbackDomain_;
            });
        renderer_->setModelVirtualTextureResolver(
            [this](const Model& model,const Material& material,
                   ModelVirtualTextureBinding& binding) {
                return resolveSampling(model,material,binding);
            });
        renderer_->setVirtualTextureFeedbackScheduler(
            [this](Renderer3D& scene) { captureNext(scene); });
        return true;
    }
    void detach() {
        if (renderer_) {
            renderer_->clearVirtualTextureFeedbackScheduler();
            renderer_->clearModelVirtualTextureResolver();
        }
        if (feedback_) {
            feedback_->setFeedbackMaterialFilter({});
            feedback_->setLiveMaterialMapper({});
        }
        renderer_=nullptr;
        feedback_=nullptr;
        selectedFeedbackDomain_=0;
    }

    // Each render() call chooses at most one idle/ready archive domain by
    // stable round-robin order. All other materials still write feedback
    // depth, but only the selected domain produces page IDs. The independent
    // capture remains asynchronous. It will be submitted to the matching
    // file streamer by pumpActive() on a later graphics frame.
    bool captureNext(Renderer3D& scene) {
        if (!renderer_ || !feedback_ || &scene!=renderer_ ||
            domains_.empty() || selectedFeedbackDomain_!=0)
            return false;
        std::vector<DomainId> ids;
        ids.reserve(domains_.size());
        for (const auto& [id,d] : domains_)
            if (d.session->active() && d.feedback->ready() &&
                !d.feedback->active() && !d.feedback->pending() &&
                !d.feedback->readyToSubmit())
                ids.push_back(id);
        if (ids.empty()) return false;
        const auto next=std::lower_bound(ids.begin(),ids.end(),nextDomainHint_);
        const DomainId id=next==ids.end()?ids.front():*next;
        selectedFeedbackDomain_=id;
        struct ResetSelectedDomain {
            DomainId& id;
            ~ResetSelectedDomain() { id=0; }
        } reset{selectedFeedbackDomain_};
        Domain* d=find(id);
        // The filtered material callback sees this ID during the draw.
        // ResetSelectedDomain also restores the filter if a renderer
        // callback throws while the feedback pass is being closed.
        const bool ok=scene.renderVirtualTextureFeedback(
            *d->feedback,{},Renderer3D::FeedbackMeshBinder{});
        nextDomainHint_=id==std::numeric_limits<DomainId>::max()?1:id+1;
        return ok;
    }

    // Poll ALL domain readbacks and advance each matching streamer, never a
    // different archive. Must run after Device::beginFrame has delivered
    // async readback callbacks and before opening another render pass.
    std::vector<StreamProgress> pumpActive(size_t perDomainGpuBudget=8) {
        std::vector<StreamProgress> result;
        for (auto& [id,d] : domains_) {
            auto* streamer=d.session->active();
            if (!streamer || !streamer->ready()) continue;
            StreamProgress record;
            record.domain=id;
            record.active=true;
            if (d.feedback->ready()) {
                const auto step=d.feedback->updateStreaming(
                    *streamer,perDomainGpuBudget);
                record.uploadedPages=step.stagedGpuPages;
                record.capturedFeedbackPending=d.feedback->pending() ||
                                               d.feedback->readyToSubmit();
            } else {
                record.uploadedPages=streamer->pump(perDomainGpuBudget);
            }
            result.push_back(record);
        }
        return result;
    }

    // CPU-side prefetch or high-priority fallback; useful for domains whose
    // GPU feedback is throttled by round-robin scheduling.
    gfx::VirtualDiskPageTicket requestPage(
        DomainId id,gfx::VirtualPageKey key,bool pin=false) {
        Domain* d=find(id);
        auto* stream=d?d->session->active():nullptr;
        return stream?stream->requestPage(key,pin):gfx::VirtualDiskPageTicket{};
    }

    bool resolveMapping(const Model& model,const Material& material,
                        VirtualTextureMaterialMapping& mapping) const {
        mapping={};
        DomainId id=0; int layer=0;
        if (!locate(model,material,id,layer)) return false;
        const Domain* domain=find(id);
        return domain &&
            terraWeaveAtlasMappingForMaterial(
                domain->session->activeVersion(),material,mapping,layer);
    }
    bool resolveSampling(const Model& model,const Material& material,
                         ModelVirtualTextureBinding& binding) const {
        binding={};
        DomainId id=0;int layer=0;
        if (!locate(model,material,id,layer)) return false;
        const Domain* d=find(id);
        if (!d || !d->session->active()) return false;
        VirtualTextureMaterialMapping mapping;
        if (!terraWeaveAtlasMappingForMaterial(
                d->session->activeVersion(),material,mapping,layer))
            return false;
        auto* stream=d->session->active();
        if (!stream->ready()) return false;
        binding.physicalAtlas=stream->physicalAtlas();
        binding.pageTable=stream->pageTableTexture();
        binding.layer=layer;
        binding.gutterPixels=float(d->session->activeVersion().gutter);
        binding.addressU=d->session->activeVersion().addressU;
        binding.addressV=d->session->activeVersion().addressV;
        return binding.valid();
    }

    TerraWeaveVirtualAtlasSession* session(DomainId id) {
        Domain* d=find(id); return d?d->session.get():nullptr;
    }
    gfx::VirtualTextureFeedbackPass* feedbackPass(DomainId id) {
        Domain* d=find(id); return d?d->feedback.get():nullptr;
    }
    void retireCompletedGpuWork(DomainId id) {
        Domain* d=find(id);
        if (d) d->session->retireCompletedGpuWork();
    }
    size_t domainCount() const { return domains_.size(); }
    void shutdown() {
        detach();
        assignments_.clear();
        liveLookup_={};
        // The caller must ensure all GPU work has completed before shutdown.
        domains_.clear();
    }
private:
    struct MaterialIdentity {
        const Model* model = nullptr;
        const Material* material = nullptr;
        bool operator<(const MaterialIdentity& rhs) const {
            const std::less<const Model*> modelLess;
            const std::less<const Material*> materialLess;
            if (modelLess(model,rhs.model)) return true;
            if (modelLess(rhs.model,model)) return false;
            return materialLess(material,rhs.material);
        }
    };
    struct Assignment { DomainId domain=0; int layer=0; };
    struct Domain {
        Options options;
        std::unique_ptr<TerraWeaveVirtualAtlasSession> session;
        std::unique_ptr<gfx::VirtualTextureFeedbackPass> feedback;
    };
    Domain* find(DomainId id) {
        const auto it=domains_.find(id);
        return it==domains_.end()?nullptr:&it->second;
    }
    const Domain* find(DomainId id) const {
        const auto it=domains_.find(id);
        return it==domains_.end()?nullptr:&it->second;
    }
    bool domainLayerReady(DomainId id,int layer) const {
        const Domain* d=find(id);
        return d && d->session->activeVersion().valid() &&
               layer>=0 && layer<d->session->activeVersion().virtualLayers;
    }
    bool locate(const Model& model,const Material& material,
                DomainId& id,int& layer) const {
        id=0;layer=0;
        if (!TerraWeaveVirtualTextureFeedback::isTerraWeaveModel(model))
            return false;
        const auto it=assignments_.find({&model,&material});
        if (it!=assignments_.end()) {
            id=it->second.domain;
            layer=it->second.layer;
        } else if (!liveLookup_ || !liveLookup_(model,material,id,layer)) {
            return false;
        }
        return domainLayerReady(id,layer);
    }
    gfx::Device* device_=nullptr; // non-owning
    Renderer3D* renderer_=nullptr; // non-owning
    TerraWeaveVirtualTextureFeedback* feedback_=nullptr; // non-owning
    std::map<DomainId,Domain> domains_;
    std::map<MaterialIdentity,Assignment> assignments_;
    DomainLookup liveLookup_;
    DomainId selectedFeedbackDomain_=0;
    DomainId nextDomainHint_=1;
};

} // namespace aiverse::render
