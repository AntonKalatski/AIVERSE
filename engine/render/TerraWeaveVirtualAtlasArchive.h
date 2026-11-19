// Runtime TerraWeave Atlas -> versioned VT page archives and safe stream swap.
// Uses the generic gfx::VirtualTexturePageArchiveWriter: no duplicated page
// format or sampling algorithm. Terra's generated HorizonBake::atlasRgba can
// be passed as a span even if Model::Image::rgba was released after upload.
// Forest/impostor images can be packed before GPU upload using Image::rgba
// or Image::sharedRgba. All GPU stream transitions run on the device thread.
// The caller owns GPU-fence completion and old-resource retirement.
#pragma once

#include "Model.h"
#include "VirtualTextureModelFeedback.h"
#include "../gfx/VirtualTexturePageArchiveWriter.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aiverse::render {

struct TerraWeaveVirtualAtlasSource {
    int width = 0, height = 0;
    std::span<const uint8_t> rgba;
};

inline TerraWeaveVirtualAtlasSource terraWeaveAtlasPixels(const Image& image) {
    TerraWeaveVirtualAtlasSource result;
    result.width = image.width;
    result.height = image.height;
    if (!image.rgba.empty())
        result.rgba = std::span<const uint8_t>(image.rgba.data(),image.rgba.size());
    else if (image.sharedRgba && !image.sharedRgba->empty())
        result.rgba = std::span<const uint8_t>(
            image.sharedRgba->data(),image.sharedRgba->size());
    return result;
}

// Calling code supplies a stable horizon content revision. Generation should
// happen while HorizonBake::atlasRgba is retained, not after a GPU-only model
// discards its CPU rgba. Each revision has a distinct immutable archive path.
inline bool packTerraWeaveAtlas(
    const std::filesystem::path& versionsRoot, uint64_t revision,
    TerraWeaveVirtualAtlasSource atlas,
    const gfx::VirtualTextureArchivePackConfig& config,
    gfx::VirtualTextureArchiveVersion& out, std::string& error) {
    const gfx::VirtualTextureArchiveLayer layer{
        atlas.width,atlas.height,atlas.rgba};
    return gfx::VirtualTexturePageArchiveWriter::pack(
        versionsRoot, revision,
        std::span<const gfx::VirtualTextureArchiveLayer>(&layer,1),
        config,out,error);
}

inline bool packTerraWeaveModelImage(
    const std::filesystem::path& versionsRoot, uint64_t revision,
    const Model& model, size_t imageIndex,
    const gfx::VirtualTextureArchivePackConfig& config,
    gfx::VirtualTextureArchiveVersion& out, std::string& error) {
    if (model.path.compare(0,10,"terraweave/") != 0 ||
        imageIndex >= model.images.size()) {
        error = "not a TerraWeave model or image index out of range";
        out = {};
        return false;
    }
    const auto pixels=terraWeaveAtlasPixels(model.images[imageIndex]);
    if (pixels.rgba.empty()) {
        error = "TerraWeave image has no CPU RGBA pixels; pack before GPU upload";
        out = {};
        return false;
    }
    return packTerraWeaveAtlas(
        versionsRoot,revision,pixels,config,out,error);
}

// One complete archive per VT sampling domain. The atlas and page-table
// represent exactly the immutable archive in active(). This object does not
// call Device::finish or force a GPU fence; the owner must make that barrier
// explicit before retireCompletedGpuWork().
class TerraWeaveVirtualAtlasSession {
public:
    explicit TerraWeaveVirtualAtlasSession(
        gfx::Device& device, gfx::TextureStreamConfig budget = {})
        : device_(&device),budget_(budget) {}
    TerraWeaveVirtualAtlasSession(const TerraWeaveVirtualAtlasSession&) = delete;
    TerraWeaveVirtualAtlasSession& operator=(const TerraWeaveVirtualAtlasSession&) = delete;

    // Prepare new GPU atlas+table while the old atlas remains active.
    // The old archive remains immutable on disk. Only the render thread
    // may call this or pumpCandidate(). A failed initialization leaves
    // the active version and its GPU handles unchanged.
    bool prepare(const gfx::VirtualTextureArchiveVersion& version,
                 int physicalPages = 128,
                 size_t maxBufferedPages = 8) {
        error_.clear();
        if (!version.valid() || !device_ || candidate_ ||
            (activeVersion_.valid() &&
             activeVersion_.directory == version.directory)) {
            error_="invalid, duplicate or already preparing VT revision";
            return false;
        }
        auto next=std::make_unique<gfx::VirtualTextureFileStreamer>(*device_,budget_);
        if (!next->initialize(version.streamConfig(physicalPages),
                              version.diskConfig(),maxBufferedPages)) {
            error_=next->lastError();
            return false;
        }
        // Request the coarsest (single-tile) fallback per logical layer
        // before publication. The owner pumps the candidate until all
        // fallback pages are resident, or explicitly overrides this gate.
        for(int layer=0;layer<version.virtualLayers;++layer) {
            const gfx::VirtualPageKey fallback{
                version.mipLevels-1,0,0,layer};
            if (!next->requestPage(fallback,true).valid()) {
                error_=next->lastError();
                return false;
            }
        }
        candidateVersion_=version;
        candidate_=std::move(next);
        return true;
    }

    size_t pumpCandidate(size_t pageBudget=8) {
        return candidate_ ? candidate_->pump(pageBudget) : 0;
    }

    bool candidateFallbackResident() const {
        if (!candidate_ || !candidate_->ready()) return false;
        for(int layer=0;layer<candidateVersion_.virtualLayers;++layer)
            if(!candidate_->exactResidentPage({
                    candidateVersion_.mipLevels-1,0,0,layer}))
                return false;
        return true;
    }

    // Promote only at a frame boundary when there is no feedback readback
    // referencing the old page IDs and the caller has arranged to bind the
    // NEW physicalAtlas()/pageTableTexture() for subsequent VT sampling.
    // The old streamer is RETAINED; never destroy it until a real GPU fence
    // confirms its sampling and page uploads have completed.
    bool promote(const gfx::VirtualTextureFeedbackPass& feedback,
                 bool requireFallback = true) {
        if (!candidate_ || !candidate_->ready() ||
            (requireFallback && !candidateFallbackResident()) ||
            feedback.active() || feedback.pending() ||
            feedback.readyToSubmit() || retired_) {
            error_="VT revision switch blocked by missing fallback, feedback or old GPU work";
            return false;
        }
        retired_=std::move(active_);
        retiredVersion_=std::move(activeVersion_);
        active_=std::move(candidate_);
        activeVersion_=std::move(candidateVersion_);
        candidateVersion_={};
        error_.clear();
        return true;
    }

    // The caller MUST have waited for device completion/fence for every
    // command referencing retired_->physicalAtlas/pageTableTexture.
    // Call neither during a render pass nor as a timer-based guess.
    void retireCompletedGpuWork() {
        retired_.reset();
        retiredVersion_={};
    }

    // Lifetime: call shutdown only when no GPU work still references these
    // three streamers; users own the device command-queue fence.
    void shutdown() {
        candidate_.reset();
        active_.reset();
        retired_.reset();
        candidateVersion_={};
        activeVersion_={};
        retiredVersion_={};
    }

    gfx::VirtualTextureFileStreamer* active() const { return active_.get(); }
    gfx::VirtualTextureFileStreamer* candidate() const { return candidate_.get(); }
    const gfx::VirtualTextureArchiveVersion& activeVersion() const {
        return activeVersion_;
    }
    const gfx::VirtualTextureArchiveVersion& candidateVersion() const {
        return candidateVersion_;
    }
    const gfx::VirtualTextureArchiveVersion& retiredVersion() const {
        return retiredVersion_;
    }
    bool hasRetiredGpuResources() const { return retired_!=nullptr; }
    const std::string& lastError() const { return error_; }
private:
    gfx::Device* device_ = nullptr;
    gfx::TextureStreamConfig budget_;
    std::unique_ptr<gfx::VirtualTextureFileStreamer> active_,candidate_,retired_;
    gfx::VirtualTextureArchiveVersion activeVersion_,candidateVersion_,
                                      retiredVersion_;
    std::string error_;
};

// Configure the existing renderer feedback resolver from an archive version.
// For a one-layer generated Horizon atlas, page layer defaults to 0.
// The caller must also bind the active VT physical atlas/page table in the
// actual material shader; feedback is the residency request path ONLY.
inline VirtualTextureMaterialMapping terraWeaveAtlasMapping(
    const gfx::VirtualTextureArchiveVersion& v,int layer=0) {
    VirtualTextureMaterialMapping map;
    if(!v.valid() || layer<0 || layer>=v.virtualLayers) return map;
    map.baseTilesX=v.baseTilesX;
    map.baseTilesY=v.baseTilesY;
    map.layer=layer;
    map.mipLevels=v.mipLevels;
    map.explicitMip=0;
    map.texelWidth=float(v.width);
    map.texelHeight=float(v.height);
    return map;
}

} // namespace aiverse::render
