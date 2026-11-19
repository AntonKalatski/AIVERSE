// Post-processing on the RHI: the effect chain of game.json / map "postfx" blocks (PostFxData.h).
// Plugs into Renderer3D through the PostProcess hook:
//
//   r3d.render(pass, &postFx);   // the scene goes into postFx's target when an effect is active
//   particles.render(...);       // still part of the scene
//   r3d.resolvePostFx();         // effects -> `pass`, which stays open for UI
//
// Every effect has GLSL + MSL code (PostFxShaders.h, one body for both) and runs per quality tier
// (PostFxQuality, PostFxData plans). RGBA16F on capable High-tier devices, RGBA8 fallback; no compute.
#pragma once
#include <functional>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include "PostFxData.h"
#include "Renderer3D.h"
#include "RenderDeviceLifetime.h"

namespace aiverse::render {

struct PostFxStats { int passes = 0; int effects = 0; int width = 0, height = 0; };

// Per-frame, presentation-only environmental tint from a volume system.
// PostFx never queries ECS or owns the meaning of any water volume.
struct PostFxUnderwater {
    bool submerged=false;
    float tintR=0, tintG=0, tintB=0;
    float absorption=0;
    float depthMetres=0;
    float distortion=0, scattering=0;
    float surfaceBlend=-1; // negative: legacy binary; otherwise 0..1 near boundary
    float surfaceOffset=0; // signed water plane height relative to camera Y
};
struct PostFxUnderwaterState {
    float blend=0;
    float tintR=0, tintG=0, tintB=0;
    float absorption=0, depthMetres=0;
    float distortion=0, scattering=0;
    float crossing=0;
    bool wasSubmerged=false;
    float surfaceBlend=0;
    float surfaceOffset=0;
};

// Pure presentation transition policy, testable without a GPU device.
inline PostFxUnderwaterState advancePostFxUnderwater(
        PostFxUnderwaterState state,PostFxUnderwater target,float seconds) {
    const float dt=std::isfinite(seconds)?std::clamp(seconds,0.f,0.25f):0.f;
    const float alpha=1.f-std::exp(-dt*6.f);
    auto smooth=[alpha](float& v,float goal){v+=alpha*(goal-v);};
    auto finite=[](float v,float lo,float hi) {
        return std::isfinite(v)?std::clamp(v,lo,hi):lo;
    };
    if(target.submerged!=state.wasSubmerged && dt>0.f)
        state.crossing=1.f;
    state.wasSubmerged=target.submerged;
    state.crossing*=std::exp(-dt*8.f);
    const float opacity=target.surfaceBlend>=0.f ?
        finite(target.surfaceBlend,0.f,1.f) : (target.submerged?1.f:0.f);
    smooth(state.blend,opacity);
    state.surfaceBlend=opacity;
    if(target.submerged || opacity>0.f) {
        smooth(state.tintR,finite(target.tintR,0.f,1.f));
        smooth(state.tintG,finite(target.tintG,0.f,1.f));
        smooth(state.tintB,finite(target.tintB,0.f,1.f));
        smooth(state.absorption,finite(target.absorption,0.f,4.f));
        smooth(state.depthMetres,finite(target.depthMetres,0.f,10000.f));
        // Signed distance is required by the depth compositor to clip each
        // scene ray against the water plane instead of tinting air segments.
        state.surfaceOffset=finite(target.surfaceOffset,-10000.f,10000.f);
        smooth(state.distortion,finite(target.distortion,0.f,1.f));
        smooth(state.scattering,finite(target.scattering,0.f,1.f));
    }
    if(state.blend<0.0001f && state.crossing<0.0001f && !target.submerged)
        state={};
    return state;
}

// Fraction of a camera-to-scene segment below the water plane.
// a/b are signed (surfaceY - endpointY). Handles partial crossings in
// either direction, a fully submerged ray, and a fully airborne ray.
inline float postFxUnderwaterRayFraction(float a,float b) {
    if(!std::isfinite(a)||!std::isfinite(b))return 0.f;
    if(a>=0.f&&b>=0.f)return 1.f;
    if(a<=0.f&&b<=0.f)return 0.f;
    return std::clamp(std::max(a,b)/(std::abs(a)+std::abs(b)),0.f,1.f);
}

struct PostFxUnderwaterBudget {
    float distortion=0,scattering=0,crossing=0;
    uint32_t samples=0;
};
// Explicit mobile-friendly quality policy: no extra scene passes or textures.
inline PostFxUnderwaterBudget postFxUnderwaterBudget(PostFxQuality quality) {
    switch(quality) {
        case PostFxQuality::Off:return {};
        case PostFxQuality::Low:return {0.2f,0.25f,0.3f,1};
        case PostFxQuality::Medium:return {0.6f,0.7f,0.7f,2};
        case PostFxQuality::High:return {1.f,1.f,1.f,3};
    }
    return {};
}

class PostFx : public PostProcess {
public:
    void setGpuDeviceLease(RenderDeviceLifetime::Lease lease) {gpuDeviceLease_=std::move(lease);}
    bool init(gfx::Device& dev);
    void shutdown();

    // game.json "postfx" then the map's (either may be null); problems go to `issues`.
    void configure(const Json& game, const Json& map, std::vector<std::string>* issues = nullptr);
    void setSettings(PostFxSettings s);
    // Apply the volume owner's latest camera sample, once per frame.
    // deltaSeconds is supplied by the host and only affects visual fading.
    void setUnderwater(PostFxUnderwater target,float deltaSeconds);
    const PostFxUnderwaterState& underwater()const{return underwater_;}
    void resetUnderwater(){underwater_={};}

    const PostFxSettings& settings() const { return settings_; }

    void setQuality(PostFxQuality q) { quality_ = q; }
    PostFxQuality quality() const { return quality_; }
    static PostFxQuality defaultQualityFor(const gfx::Device& dev, bool mobile);   // AIVERSE_POSTFX overrides
    void setEnabled(bool on) { enabled_ = on; }          // editor: preview on / off
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

    const PostFxStats& stats() const { return stats_; }
    const std::string& lastError() const { return error_; }

private:
    struct Target { gfx::TextureHandle tex; int w = 0, h = 0; gfx::PixelFormat format = gfx::PixelFormat::RGBA8; };
    struct Sized {                         // targets for one output size (editor: Scene + Game views)
        int w = 0, h = 0;
        uint64_t lastUse = 0;
        std::map<int, Target> targets;     // by role
        std::map<std::pair<uint64_t,int>, gfx::TextureHandle> depths;   // Metal: every pass has a depth attachment, shared per size
    };
    struct Lut { gfx::TextureHandle tex; int n = 0; bool failed = false; };
    enum Prog { kPrefilter, kDown, kUp, kBlur, kShaftsPre, kShafts, kFxaa, kProgCount };

    Target& target(Sized& s, int role, int w, int h);
    gfx::TextureHandle depthFor(Sized& s, int w, int h, int role=0);
    Sized& sized(int w, int h);
    gfx::PipelineHandle program(Prog p);
    gfx::PipelineHandle composite(uint32_t mask, const Json* graph = nullptr);
    void draw(Sized& s, const Target& dst, gfx::PipelineHandle pipe, const void* u, size_t size,
              std::initializer_list<std::pair<int, gfx::TextureHandle>> tex);
    const Lut* lut(const std::string& path);
    void release(gfx::TextureHandle& t);

    void checkGpuDevice(const gfx::Device& dev) const {
        if(gpuDeviceLease_.device() && !gpuDeviceLease_.validFor(dev))
            throw std::logic_error("PostFx GPU device generation expired");
    }
    RenderDeviceLifetime::Lease gpuDeviceLease_;
    gfx::Device* dev_ = nullptr;
    PostFxSettings settings_;
    PostFxQuality quality_ = PostFxQuality::Medium;
    bool enabled_ = true;
    bool hdrFrame_ = false, sceneReady_ = false;
    bool hdrFailed_ = false; // allocation failure: stay on the low-cost path
    gfx::BufferHandle triangle_;
    gfx::TextureHandle black_;
    gfx::ShaderHandle shaders_[kProgCount];
    gfx::PipelineHandle pipes_[kProgCount];
    bool failed_[kProgCount] = {};
    std::map<std::string, std::pair<gfx::ShaderHandle, gfx::PipelineHandle>> composites_;
    std::map<std::string, bool> compositeFailed_;
    std::vector<Sized> sizes_;
    std::map<std::string, Lut> luts_;
    ImageLoader loader_;
    std::map<std::string,gfx::TextureHandle> graphTextures_;
    std::map<std::string,std::vector<std::string>> graphPaths_;
    gfx::TextureHandle graphTexture(const std::string& path);
    double graphEpoch_ = -1;
    uint64_t frame_ = 0;
    PostFxUnderwaterState underwater_{};
    PostFxStats stats_;
    std::string error_;
};

} // namespace aiverse::render
