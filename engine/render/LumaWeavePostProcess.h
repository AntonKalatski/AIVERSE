// Explicit material-aware LumaWeave HDR integration.
// Wrap any existing PostProcess (PostFx or ScreenLighting) without replacing
// its HDR/tone-map pipeline. The host MUST supply a per-frame material buffer:
// RGB = linear diffuse baseColor, alpha = metallic (0..1), full scene size.
// Without a genuine material buffer, LumaWeave is NOT composited: do not fake
// albedo by sampling the already-lighted HDR scene color.
// Exclusivity: this wrapper refuses to add direct light if the original
// LightSystem also contains dynamic lights or active static point direct light.
// Default Renderer3D/LightSystem behaviour is untouched until a host opts in.
#pragma once
#include "Renderer3D.h"
#include "Lights.h"
#include "LumaWeaveQuality.h"
#include "LumaWeaveHdrCompositeShader.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace aiverse::render {

class LumaWeavePostProcess final : public PostProcess {
public:
    struct FrameStats {
        bool eligible=false,exclusive=false,composited=false,reusedPrevious=false;
        bool legacyConflict=false,materialMissing=false,gpuBusy=false;
        LumaWeaveDecision quality{};
        LumaWeaveScreenMetal::Submission submission{};
        LumaWeaveScreenMetal::Completion lastGpuCompletion{};
    };

    LumaWeavePostProcess(PostProcess& downstream,LightSystem& legacy)
        :downstream_(downstream),legacy_(legacy) {}
    LumaWeavePostProcess(const LumaWeavePostProcess&)=delete;
    LumaWeavePostProcess& operator=(const LumaWeavePostProcess&)=delete;
    ~LumaWeavePostProcess() override {(void)shutdown();}

    bool initialize(gfx::Device& dev) {
        if(!shutdown())return false;
        if(!producer_.initialize(dev,LumaWeaveQualityPolicy::settingsFor(LumaWeaveQuality::Ultra)))
            return false;
        gfx::ShaderDesc sd;
        sd.msl=AIVERSE_MSL_LUMAWEAVE_HDR_COMPOSITE;
        sd.textureSlots=3;sd.uniformSlots=1;
        sd.linearTarget=true;
        sd.label="LumaWeave explicit HDR direct compositor";
        const auto shader=dev.createShader(sd);
        if(!shader.valid()){producer_.shutdown();return false;}
        gfx::PipelineDesc pipe;
        pipe.shader=shader;
        pipe.attrs[0]={0,gfx::VertexFormat::Float2,0};
        pipe.attrCount=1;pipe.stride=8;
        pipe.indexType=gfx::IndexType::None;
        pipe.cull=gfx::CullMode::None;
        pipe.blend=gfx::BlendMode::Additive;
        pipe.depthTest=pipe.depthWrite=false;
        pipe.colorFormat=gfx::PixelFormat::RGBA16F;
        pipe.label="LumaWeave linear direct diffuse additive layer";
        const auto pipeline=dev.createPipeline(pipe);
        if(!pipeline.valid()){
            dev.destroyShader(shader);
            producer_.shutdown();return false;
        }
        const float vertices[]={-1,-1,3,-1,-1,3};
        gfx::BufferDesc bd;
        bd.type=gfx::BufferType::Vertex;
        bd.usage=gfx::Usage::Immutable;
        bd.size=sizeof(vertices);bd.data=vertices;
        bd.label="LumaWeave fullscreen triangle";
        const auto vb=dev.createBuffer(bd);
        if(!vb.valid()){
            dev.destroyPipeline(pipeline);dev.destroyShader(shader);
            producer_.shutdown();return false;
        }
        dev_=&dev;shader_=shader;pipeline_=pipeline;vertex_=vb;
        return true;
    }

    bool shutdown() {
        if(!producer_.shutdown())return false;
        if(dev_) {
            if(vertex_.valid())dev_->destroyBuffer(vertex_);
            if(pipeline_.valid())dev_->destroyPipeline(pipeline_);
            if(shader_.valid())dev_->destroyShader(shader_);
        }
        dev_=nullptr;vertex_={};pipeline_={};shader_={};scene_={};
        material_={};materialWidth_=materialHeight_=0;
        sceneToken_=materialToken_=0;frameCounter_=0;
        sourcesPublished_=geometryPublished_=enabled_=false;
        frame_={};last_={};
        return true;
    }

    bool ready() const{return dev_ && producer_.ready() && pipeline_.valid();}
    void setEnabled(bool on){enabled_=on;}
    bool enabled() const{return enabled_;}
    void setQuality(LumaWeaveQuality q){quality_=q;}
    LumaWeaveQuality quality() const{return quality_;}
    const FrameStats& stats() const{return last_;}
    const LumaWeaveScreenMetal& screen() const{return producer_;}

    // Host must call on the owning device thread after beginFrame and before
    // any rendering pass. This allows changes after a prior GPU fence arrived.
    LumaWeaveScreenMetal::Completion pollGpu(){
        return producer_.poll();
    }

    bool publishSources(const LumaWeaveRegistry& sources,
                        const LumaWeaveClusters& clusters) {
        if(!ready())return false;
        if(sourcesPublished_ && producer_.sourceRevision()==sources.revision())
            return true;
        const auto packet=packLumaSources(sources,clusters);
        if(!producer_.setSources(packet))return false;
        sourcesPublished_=true;
        return true;
    }
    bool publishGeometry(const GiGpuBvh& bvh,uint64_t revision) {
        if(!ready())return false;
        if(geometryPublished_ && producer_.geometryRevision()==revision)
            return true;
        if(!producer_.setGeometry(bvh,revision))return false;
        geometryPublished_=true;
        return true;
    }

    // The caller must publish this material texture for EXACTLY this scene
    // token; the texture must be filled during the same view's renderer pass.
    // In particular an old material G-buffer from another viewport is unsafe.
    void beginView(uint64_t sceneToken) {
        if(!sceneToken)throw std::invalid_argument("LumaWeave requires nonzero view serial");
        sceneToken_=sceneToken;
        material_={};
        materialToken_=0;
    }
    bool setReceiverMaterial(gfx::TextureHandle albedoMetallic,uint32_t width,
                             uint32_t height,uint64_t sceneToken) {
        if(!ready() || !albedoMetallic.valid() || !sceneToken ||
           sceneToken!=sceneToken_ || !width||!height ||
           width>16384||height>16384)
            return false;
        material_=albedoMetallic;
        materialWidth_=width;materialHeight_=height;
        materialToken_=sceneToken;
        return true;
    }

    bool active() const override {return downstream_.active();}
    bool linearScene() const override {return downstream_.linearScene();}
    bool temporalShadowResolve() const override {return downstream_.temporalShadowResolve();}

    gfx::PassDesc scenePass(const gfx::PassDesc& out,int w,int h) override {
        scene_=downstream_.scenePass(out,w,h);
        viewWidth_=w;viewHeight_=h;
        frame_={};
        frame_.lastGpuCompletion=producer_.poll();
        frame_.eligible=enabled_ && quality_!=LumaWeaveQuality::Off &&
                        ready() && downstream_.linearScene() &&
                        scene_.colorTarget.valid() && scene_.depthTarget.valid() &&
                        w>0 && h>0 && uint32_t(w)<=16384 && uint32_t(h)<=16384 &&
                        sourcesPublished_ && geometryPublished_;
        return scene_;
    }

    // Renderer3D::resolvePostFx has already ended the full HDR scene pass.
    // The compute writes and additive pass are encoded before the downstream
    // tonemapping/presentation. No read/write feedback: additive blending only.
    void resolve(gfx::Device& device,const gfx::PassDesc& out,
                 const PostFrame& frame) override {
        last_=frame_;
        const bool sameMaterial=material_.valid() && materialToken_==sceneToken_ &&
            materialWidth_==uint32_t(viewWidth_) &&
            materialHeight_==uint32_t(viewHeight_);
        last_.materialMissing=!sameMaterial;
        last_.legacyConflict=legacy_.count()!=0 || legacy_.staticPointLightsActive();
        last_.exclusive=!last_.legacyConflict;
        if(frame_.eligible && sameMaterial && !last_.legacyConflict &&
           &device==dev_ && int(frame.width)==viewWidth_ &&
           int(frame.height)==viewHeight_) {
            last_.quality=LumaWeaveQualityPolicy::choose(quality_,uint32_t(viewWidth_),
                uint32_t(viewHeight_),producer_.sourceCount(),producer_.settings());
            gfx::TextureHandle direct;
            const auto& decision=last_.quality;
            if(decision.admitted && !producer_.busy() &&
               producer_.resize(decision.width,decision.height)) {
                last_.submission=producer_.dispatch(scene_.depthTarget,
                    uint32_t(viewWidth_),uint32_t(viewHeight_),
                    frame.viewProj.inverse(),frame.viewProj,
                    frame.cameraPos,decision.samples,uint32_t(++frameCounter_));
                if(last_.submission.encoded)
                    direct=producer_.encodedOutput(last_.submission.serial);
            } else {
                // The previous confirmed color is immutable while another
                // ping-pong slot is in flight. Reuse only for the EXACT view
                // matrix and eye, never with a camera cut or changed sources.
                last_.gpuBusy=producer_.busy();
                if(producer_.canReuseConfirmedFor(frame.viewProj,frame.cameraPos)) {
                    direct=producer_.confirmedOutput();
                    last_.reusedPrevious=direct.valid();
                }
            }
            if(direct.valid())
                last_.composited=additiveCompose(device,direct);
        }
        // Do not bypass existing ScreenLighting, PostFx, HUD or UI pipeline.
        downstream_.resolve(device,out,frame);
    }

private:
    bool additiveCompose(gfx::Device& d,gfx::TextureHandle direct) const {
        if(!pipeline_.valid()||!scene_.colorTarget.valid()||
           !scene_.depthTarget.valid()||!direct.valid())return false;
        struct Params {float4 options;};
        const Params opts{{d.info().renderTargetFlipY?1.f:0.f,.008f,1.f,0.f}};
        gfx::PassDesc p=scene_;
        p.clearColorEnabled=false;
        p.clearDepthEnabled=false;
        d.beginPass(p);
        d.setViewport({0,0,viewWidth_,viewHeight_});
        d.setPipeline(pipeline_);
        d.setVertexBuffer(vertex_);
        d.setUniforms(0,&opts,sizeof(opts));
        d.setTexture(0,direct);
        d.setTexture(1,material_);
        d.setTexture(2,scene_.depthTarget);
        d.draw(3);
        d.endPass();
        return true;
    }

    PostProcess& downstream_;
    LightSystem& legacy_;
    LumaWeaveScreenMetal producer_;
    gfx::Device* dev_=nullptr;
    gfx::ShaderHandle shader_{};
    gfx::PipelineHandle pipeline_{};
    gfx::BufferHandle vertex_{};
    gfx::PassDesc scene_{};
    gfx::TextureHandle material_{};
    uint32_t materialWidth_=0,materialHeight_=0;
    int viewWidth_=0,viewHeight_=0;
    uint64_t sceneToken_=0,materialToken_=0,frameCounter_=0;
    bool enabled_=false,sourcesPublished_=false,geometryPublished_=false;
    LumaWeaveQuality quality_=LumaWeaveQuality::Balanced;
    FrameStats frame_{},last_{};
};

} // namespace aiverse::render
