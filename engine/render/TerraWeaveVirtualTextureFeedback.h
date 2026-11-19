// TerraWeave -> genre-independent Virtual Texture feedback integration.
// Forest/Arbor, impostors and Horizon submit ordinary render::ModelDraws
// through Renderer3D. Reuse those draws: never duplicate Terra geometry,
// shadow, LOD or scene submission logic.
// This header belongs to the Texture RHI integration layer. It deliberately
// does NOT include TerraWeave's evolving generator/scene headers; the bridge
// compiles even before feature/terraweave is merged.
#pragma once
#include "Renderer3D.h"
#include "VirtualTextureModelFeedback.h"
#include "VirtualTextureDeformedFeedback.h"
#include "../gfx/VirtualTextureFeedbackPass.h"
#include <functional>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace aiverse::render {

class TerraWeaveVirtualTextureFeedback {
public:
    using Mapping = VirtualTextureMaterialMapping;
    using MaterialMapper = std::function<bool(
        const Model&, const Material&, Mapping&)>;

    static bool isTerraWeaveModel(const Model& model) {
        constexpr std::string_view prefix = "terraweave/";
        const std::string_view path(model.path);
        return path.size() >= prefix.size() &&
               path.substr(0, prefix.size()) == prefix;
    }

    // Feedback pass is provided by the caller and must already be initialized.
    // Terra's current Arbor, impostor and Horizon models are all ModelDraws;
    // deformed feedback can be enabled for future procedural animations.
    // Legacy single-archive mode: one application-owned GPU feedback pass.
    bool initialize(gfx::Device& device,
                    gfx::VirtualTextureFeedbackPass& pass,
                    Renderer3D& renderer,
                    bool includeDeformed = false) {
        return pass.ready() &&
            initializePrograms(device,renderer,pass.hasDepthAttachment(),
                               includeDeformed,&pass);
    }

    // Multi-archive mode: the domain manager owns separate feedback passes.
    // No dummy/legacy render target is required. The per-domain depth mode
    // passed here must match each domain's feedback target depth setting.
    bool initializeForDomains(gfx::Device& device,
                              Renderer3D& renderer,
                              bool depthAttachment = true,
                              bool includeDeformed = false) {
        return initializePrograms(device,renderer,depthAttachment,
                                  includeDeformed,nullptr);
    }

    // Call before new Terra submissions each frame to prevent stale pointer
    // keys from region rebuilds, forest unload and editor asset hot-reload.
    void beginFrame() { pages_.clear(); }

    // Optional feedback-domain selection; default accepts every mapped
    // material for the legacy single-domain use case. Does NOT affect
    // the main material sampler resolver.
    using FeedbackMaterialFilter = std::function<bool(const Model&,const Material&)>;
    void setFeedbackMaterialFilter(FeedbackMaterialFilter filter) {
        feedbackMaterialFilter_ = std::move(filter);
    }
    bool resolveFeedbackMaterialMapping(const Model& model,
                                       const Material& material,
                                       Mapping& mapping) const {
        mapping = {};
        if (feedbackMaterialFilter_ && !feedbackMaterialFilter_(model,material))
            return false;
        return resolveMaterialMapping(model,material,mapping);
    }

    // Exact mapping used by both feedback and main-frame VT sampling.
    // Keep these in sync across atlas revisions and impostor overrides.
    bool resolveMaterialMapping(const Model& model,
                                const Material& material,
                                Mapping& mapping) const {
        mapping = {};
        if (!isTerraWeaveModel(model)) return false;
        const auto it=pages_.find({&model,&material});
        if (it!=pages_.end()) {
            mapping=it->second;
            return validMapping(mapping);
        }
        if (liveMapper_) {
            Mapping candidate;
            if (liveMapper_(model,material,candidate) &&
                validMapping(candidate)) {
                mapping=candidate;
                return true;
            }
        }
        return false;
    }

    // Optional live lookup for Terra's private/replaced Horizon region
    // models. Called while rendering on the device thread; MUST be a
    // lightweight lookup of EXISTING VT allocations, not GPU allocation.
    // The callback can identify both model identity and material override.
    void setLiveMaterialMapper(MaterialMapper mapper) {
        liveMapper_ = std::move(mapper);
    }

    static bool validMapping(const Mapping& mapping) {
        return mapping.baseTilesX >= 1 && mapping.baseTilesX <= 2048 &&
            mapping.baseTilesY >= 1 && mapping.baseTilesY <= 2048 &&
            mapping.layer >= 0 && mapping.layer < 16 &&
            mapping.mipLevels >= 1 && mapping.mipLevels <= 30 &&
            mapping.explicitMip >= 0 &&
            mapping.explicitMip < mapping.mipLevels &&
            std::isfinite(mapping.texelWidth) && mapping.texelWidth > 0 &&
            std::isfinite(mapping.texelHeight) && mapping.texelHeight > 0 &&
            std::isfinite(mapping.mipBias) &&
            std::isfinite(mapping.uvScaleX) &&
            std::isfinite(mapping.uvScaleY) &&
            std::isfinite(mapping.uvOffsetX) &&
            std::isfinite(mapping.uvOffsetY);
    }

    // Register only actual per-material page allocations. Unregistered
    // materials still draw into feedback depth and write zero page IDs.
    // The physical atlas/page table and streaming archive are owned by
    // the caller, not this bridge.
    bool registerMaterial(const Model& model, const Material& material,
                          Mapping mapping) {
        if (!renderer_ || !isTerraWeaveModel(model) ||
            !validMapping(mapping)) return false;
        pages_[{&model, &material}] = mapping;
        return true;
    }

    // Feed the EXACT ModelDraw that Terra's ForestAdapter::forEachDraw
    // sends to Renderer3D; handles per-azimuth impostor material overrides.
    // Mapper returns false for a material with no VT allocation.
    // Register before calling renderer.drawModel(draw).
    size_t registerDraw(const ModelDraw& draw, const MaterialMapper& mapper) {
        if (!draw.model || !mapper || !isTerraWeaveModel(*draw.model))
            return 0;
        size_t registered = 0;
        const auto add = [&](const Material& material) {
            Mapping mapping;
            if (mapper(*draw.model,material,mapping) &&
                registerMaterial(*draw.model,material,mapping))
                ++registered;
        };
        for (const auto& material : draw.model->materials) add(material);
        // Material overrides are separate pointers, and Renderer3D may
        // use any of them; never assume the model's original material
        // pointers remain valid when a forest impostor azimuth changes.
        if (draw.materials && draw.materials != &draw.model->materials)
            for (const auto& material : *draw.materials) add(material);
        return registered;
    }

    // For a TerraWeaveHorizonScene that owns its active models internally,
    // register current material allocations at the point a model is
    // uploaded/replaced; use beginFrame()/registerDraw() when models swap.
    size_t registerModel(const Model& model, const MaterialMapper& mapper) {
        if (!mapper || !isTerraWeaveModel(model)) return 0;
        size_t n = 0;
        for (const Material& material : model.materials) {
            Mapping mapping;
            if (mapper(model,material,mapping) &&
                registerMaterial(model,material,mapping))
                ++n;
        }
        return n;
    }

    bool ready() const { return renderer_ && rigid_.ready(); }
    size_t mappedMaterialCount() const { return pages_.size(); }
    bool deformedEnabled() const { return deformedEnabled_; }

    // Always disconnect before destroying the Renderer3D or feedback pass.
    // Does not destroy the caller-owned pass or virtual texture streamer.
    void shutdown() {
        if (renderer_) {
            renderer_->clearVirtualTextureFeedback();
            renderer_->clearModelVirtualTextureResolver();
        }
        deformed_.shutdown();
        rigid_.shutdown();
        pages_.clear();
        liveMapper_ = {};
        feedbackMaterialFilter_ = {};
        renderer_ = nullptr;
        pass_ = nullptr;
        deformedEnabled_ = false;
    }
private:
    bool initializePrograms(gfx::Device& device,Renderer3D& renderer,
                            bool depthAttachment,bool includeDeformed,
                            gfx::VirtualTextureFeedbackPass* pass) {
        if (renderer_) return false;
        renderer_=&renderer;
        pass_=pass;
        if (!rigid_.initialize(device,depthAttachment)) {
            renderer_=nullptr;pass_=nullptr;
            return false;
        }
        if (includeDeformed &&
            !deformed_.initialize(device,depthAttachment)) {
            rigid_.shutdown();
            renderer_=nullptr;pass_=nullptr;
            return false;
        }
        auto resolve=[this](const Model& model,const Material& material,
                            Mapping& mapping) {
            return resolveFeedbackMaterialMapping(model,material,mapping);
        };
        if (!rigid_.connect(renderer,resolve) ||
            (includeDeformed && !deformed_.connect(renderer,resolve))) {
            shutdown();
            return false;
        }
        // A null legacy pass means the multi-domain scheduler will submit
        // its own per-domain targets; no feedback pass is implicitly run.
        renderer.setVirtualTextureFeedback(pass,{},{});
        deformedEnabled_=includeDeformed;
        return true;
    }
    struct Key {
        const Model* model = nullptr;
        const Material* material = nullptr;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            const auto a = reinterpret_cast<uintptr_t>(k.model);
            const auto b = reinterpret_cast<uintptr_t>(k.material);
            return size_t((a >> 4) ^ (b << 3) ^ (b >> 7));
        }
    };
    Renderer3D* renderer_ = nullptr; // non-owning
    gfx::VirtualTextureFeedbackPass* pass_ = nullptr; // non-owning
    bool deformedEnabled_ = false;
    MaterialMapper liveMapper_;
    FeedbackMaterialFilter feedbackMaterialFilter_;
    std::unordered_map<Key, Mapping, KeyHash> pages_;
    VirtualTextureModelFeedback rigid_;
    VirtualTextureDeformedModelFeedback deformed_;
};

} // namespace aiverse::render
