// Post-processing on the RHI: the effect chain of game.json / map "postfx" blocks (PrismaData.h).
// Plugs into Renderer3D through the PostProcess hook:
//
//   r3d.render(pass, &prisma);   // the scene goes into prisma's target when an effect is active
//   particles.render(...);       // still part of the scene
//   r3d.resolvePrisma();         // effects -> `pass`, which stays open for UI
//
// Every effect has GLSL + MSL code (PrismaShaders.h, one body for both) and runs per quality tier
// (PrismaQuality, PrismaData plans). RGBA16F on capable High-tier devices, RGBA8 fallback; no compute.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "PrismaData.h"
#include "PostFxData.h" // legacy aliases used by existing clients and editor
#include "PrismaVolumes.h"
#include "PrismaRenderGraph.h" // GPU-free post-pass graph planner; executor follows later
#include "PrismaBloomGraph.h" // persistent compiled Bloom topology per viewport
#include "PrismaRhiExecutor.h" // opt-in per-view graph GPU context
#include "PrismaGpuRetirement.h"
#include "Renderer3D.h"

namespace aiverse::render {

struct PrismaStats { int passes = 0; int effects = 0; int width = 0, height = 0; };

class PrismaVolumeStack;

class Prisma : public PostProcess {
public:
    bool init(gfx::Device& dev);
    void shutdown();
    // For hosts that can guarantee a truly idle GPU: drain quarantined
    // prior viewport-size render targets (never infer idle from CPU frames).
    void releaseRetiredSizesAfterGpuIdle();
    size_t pendingRetiredSizes() const { return retiredSizes_.size(); }
    // Close a presentation view without touching size-shared scene targets.
    // Exposure history is owned per view; borrowed velocity/reactive handles
    // are not destroyed. The current GPU submission fences owned textures.
    void retireView(uint64_t viewId);
    // At a graphics-owner frame boundary, retire size-keyed targets that NO
    // recorded view currently references. Never destroys active/in-flight GPU
    // resources synchronously. The caller keeps any desired view IDs live.
    size_t retireUnreferencedSizeCaches();
    // When Scene/Game is hidden and no new post frame is rendered, the
    // editor still polls verified GPU completions each UI frame.
    void collectGpuRetirements();
    struct RetirementStats {
        size_t activeSizes = 0;
        size_t retiredSizes = 0;
        size_t exposureViews = 0;
        size_t motionViews = 0;
        size_t temporalViews = 0;
        size_t fencedTextures = 0;
        size_t unfencedTextures = 0;
        size_t graphRetiredTextures = 0;
        size_t graphLiveTextures = 0;
        size_t graphUnknownTextures = 0;
        uint64_t graphLiveKnownBytes = 0;
        uint64_t graphPendingKnownBytes = 0;
        uint64_t knownPendingBytes = 0; // lower bound; includes retired graph handles
        uint64_t lastGpuCompletedFence = 0;
    };
    RetirementStats retirementStats() const;
    // CPU-only diagnostics: shared size caches are counted ONCE in totals.
    // A view reports the shared cache it references, not an allocation it owns.
    // A missing RHI metadata entry increments unknownTextures (not 0-byte VRAM).
    struct ViewMemoryStats {
        uint64_t viewId = 0;
        int width = 0, height = 0;
        size_t sharedSizeViews = 0; // >1 means other view IDs reference this same size cache
        size_t sharedTextures = 0, ownedHistoryTextures = 0;
        size_t ownedTemporalTextures = 0;
        size_t unknownTextures = 0;
        uint64_t sharedKnownBytes = 0, ownedHistoryKnownBytes = 0;
        uint64_t ownedTemporalKnownBytes = 0;
        uint64_t graphLiveKnownBytes = 0, graphPendingKnownBytes = 0;
        size_t graphLiveTextures = 0, graphPendingTextures = 0;
    };
    struct MemoryStats {
        std::vector<ViewMemoryStats> views;
        size_t activeSizeCaches = 0, unreferencedSizeCaches = 0;
        size_t liveTextures = 0, unknownTextures = 0;
        uint64_t liveKnownBytes = 0; // active size caches + per-view exposure + resident LUTs
        uint64_t sharedSizeKnownBytes = 0;
        uint64_t unreferencedSizeKnownBytes = 0; // subset of sharedSizeKnownBytes
        uint64_t viewHistoryKnownBytes = 0;
        uint64_t temporalHistoryKnownBytes = 0; // subset of viewHistoryKnownBytes
        uint64_t otherLiveKnownBytes = 0;
        uint64_t graphPendingKnownBytes = 0; // belongs to retirement, NOT live bytes
    };
    MemoryStats memoryStats() const;

    // game.json "postfx" then the map's (either may be null); problems go to `issues`.
    void configure(const Json& game, const Json& map, std::vector<std::string>* issues = nullptr);
    void setSettings(PrismaSettings s);
    const PrismaSettings& settings() const { return settings_; }
    const PrismaSettings& baseSettings() const { return baseSettings_; }

    // Native/ECS users choose per-camera profiles without modifying game.json.
    // Call before Renderer3D::render(); uses original configured settings as base.
    void applyCameraVolumes(const PrismaVolumeStack& stack, float3 eye,
                            uint32_t cameraMask = 0xffffffffu);
    void clearCameraVolumes();
    // Camera cuts/teleports invalidate the temporal exposure history per viewport.
    void resetAutoExposure();
    // Stable presentation-only camera/view identity. 0 is the legacy default.
    // Set before rendering a view, even when two views have equal dimensions.
    void setViewId(uint64_t id) { viewId_ = id; }
    uint64_t viewId() const { return viewId_; }
    // Optional subpixel jitter for a COPY of the current render camera.
    // Call after setViewId/applyCameraVolumes and BEFORE Renderer3D::beginScene;
    // retain the original camera for gameplay picking and 2D overlays.
    // Active only with HDR temporalResolve + AIVERSE_PRISMA_JITTER=1.
    RTSCamera jitteredCamera(RTSCamera camera, int width, int height);
    void resetAutoExposure(uint64_t id);
    // Camera cut/teleport: stop motion blur on the next rendered frame.
    void resetMotionHistory(uint64_t id);
    void resetMotionHistory();
    // Optional per-frame object velocity: RG = current-minus-previous screen UV,
    // B > 0.5 = valid object pixel. Must match this view's render dimensions.
    // The external texture is borrowed and cleared after each resolve.
    void setObjectMotionVectors(uint64_t view, gfx::TextureHandle velocity);
    void setObjectMotionVectors(uint64_t view, MotionTextureInput velocity);
    // Optional current-frame R=0..1 reactive coverage; borrowed until resolve.
    void setMotionReactiveMask(uint64_t view, gfx::TextureHandle mask);
    void setMotionReactiveMask(uint64_t view, MotionTextureInput mask);

    void setQuality(PrismaQuality q) {
        if (quality_ != q) { quality_ = q; resetAutoExposure(); resetMotionHistory(); }
    }
    PrismaQuality quality() const { return quality_; }
    static PrismaQuality defaultQualityFor(const gfx::Device& dev, bool mobile);   // AIVERSE_POSTFX overrides
    void setEnabled(bool on) {
        if (enabled_ != on) { enabled_ = on; resetAutoExposure(); resetMotionHistory(); }
    }          // editor: preview on / off
    bool enabled() const { return enabled_; }

    // Reads an image of the game (LUTs): path -> RGBA8 top row first. Client: content files; editor: project dir.
    using ImageLoader = std::function<bool(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba)>;
    void setImageLoader(ImageLoader fn);

    // PostProcess
    bool active() const override;
    bool linearScene() const override { return hdrFrame_; }
    bool hdrRequested() const;
    gfx::PassDesc scenePass(const gfx::PassDesc& out, int w, int h) override;
    void resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& frame) override;
    bool wantsObjectMotionVectors() const override;
    gfx::TextureHandle objectMotionSceneDepth() const override { return sceneDepthForMotion_; }
    uint64_t objectMotionEpoch(uint64_t viewId) const override;
    bool objectMotionContext(uint64_t& viewId, mat4& previousViewProj) const override;
    void acceptObjectMotionVectors(gfx::TextureHandle velocity) override;
    void acceptObjectMotionVectors(MotionTextureInput velocity) override;
    void acceptMotionReactiveMask(gfx::TextureHandle reactive) override;
    void acceptMotionReactiveMask(MotionTextureInput reactive) override;

    const PrismaStats& stats() const { return stats_; }
    const std::string& lastError() const { return error_; }

private:
    struct Target { gfx::TextureHandle tex; int w = 0, h = 0; gfx::PixelFormat format = gfx::PixelFormat::RGBA8; };
    struct Sized {                         // targets for one output size (editor: Scene + Game views)
        int w = 0, h = 0;
        uint64_t lastUse = 0;
        std::map<int, Target> targets;     // by role
        std::map<uint64_t, gfx::TextureHandle> depths;   // Metal: every pass has a depth attachment, shared per size
        PrismaBloomGraphCache bloomGraph; // invalidates on size, mip count or HDR format changes
        std::unique_ptr<PrismaRhiExecutor> graphExecutor; // size-shared graph cache, NOT view-owned
    };
    struct RetiredSized {
        Sized resources;
        uint64_t lastSubmittedFence = 0; // 0 = no backend proof, await GPU idle
    };
    struct ExposureView {
        gfx::TextureHandle history[2];
        int current = 0;
        bool valid = false;
        double time = 0;
    };
    struct TemporalView {
        gfx::TextureHandle history[2]; // RGBA16F: HDR RGB + previous hardware depth in A
        int width = 0, height = 0;
        int current = 0; // most recently written history index
        bool valid = false;
        uint64_t jitterSequence = 0;
        float2 currentJitterUv{}, previousJitterUv{}; // NDC jitter / 2, unflipped UV

    };
    struct MotionView {
        mat4 previousViewProj;
        float3 previousCameraPos{};
        bool valid = false;
        uint64_t cutEpoch = 1;
        int width = 0, height = 0;
        MotionTextureInput objectVelocity; // borrowed for this frame only
        MotionTextureInput reactiveMask; // borrowed for this frame only
    };
    struct Lut { gfx::TextureHandle tex; int n = 0; bool failed = false; };
    enum Prog { kPrefilter, kDown, kUp, kBlur, kShaftsPre, kShafts, kFxaa, kDepthOfField, kExposureLog, kExposureReduce, kExposureAdapt, kMotionVectors, kMotionBlur, kTemporalResolve, kProgCount };

    Target& target(Sized& s, int role, int w, int h);
    gfx::TextureHandle depthFor(Sized& s, int w, int h, bool scratch = false);
    Sized& sized(int w, int h);
    void destroySized(Sized& s);
    void collectCompletedSizes(uint64_t completedGpuFence);
    gfx::PipelineHandle program(Prog p);
    gfx::PipelineHandle composite(uint32_t mask, const Json* graph = nullptr);
    void draw(Sized& s, const Target& dst, gfx::PipelineHandle pipe, const void* u, size_t size,
              std::initializer_list<std::pair<int, gfx::TextureHandle>> tex);
    const Lut* lut(const std::string& path);
    void release(gfx::TextureHandle& t);
    void retireTarget(gfx::TextureHandle& t); // holds until real GPU fence

    gfx::Device* dev_ = nullptr;
    PrismaSettings settings_;
    PrismaSettings baseSettings_;
    PrismaQuality quality_ = PrismaQuality::Medium;
    bool enabled_ = true;
    bool hdrFrame_ = false, sceneReady_ = false;
    bool hdrFailed_ = false; // allocation failure: stay on the low-cost path
    gfx::BufferHandle triangle_;
    gfx::TextureHandle black_;
    gfx::TextureHandle sceneDepthForMotion_;
    gfx::ShaderHandle shaders_[kProgCount];
    gfx::PipelineHandle pipes_[kProgCount];
    bool failed_[kProgCount] = {};
    std::map<std::string, std::pair<gfx::ShaderHandle, gfx::PipelineHandle>> composites_;
    std::map<std::string, bool> compositeFailed_;
    std::vector<Sized> sizes_;
    std::vector<RetiredSized> retiredSizes_; // retains GPU resources until verified safe
    PrismaGpuRetirementQueue<gfx::TextureHandle> retiredTargets_;
    std::vector<gfx::TextureHandle> unfencedTargets_;
    // Temporal state is keyed by view identity, NOT by render target size.
    std::map<uint64_t, ExposureView> exposureViews_;
    std::map<uint64_t, MotionView> motionViews_;
    std::map<uint64_t, TemporalView> temporalViews_;
    // Last successful postprocess scene pass per stable view ID. A size cache
    // can be shared by several views; retireView() removes a view reference.
    std::map<uint64_t, std::pair<int, int>> viewDimensions_;
    uint64_t viewId_ = 0;
    std::map<std::string, Lut> luts_;
    ImageLoader loader_;
    std::map<std::string,gfx::TextureHandle> graphTextures_;
    std::map<std::string,std::vector<std::string>> graphPaths_;
    gfx::TextureHandle graphTexture(const std::string& path);
    double graphEpoch_ = -1;
    uint64_t frame_ = 0;
    PrismaStats stats_;
    std::string error_;
};


} // namespace aiverse::render
