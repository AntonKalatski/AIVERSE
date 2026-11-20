// Opt-in VIVID hardware ID pass, currently Metal-only.
// Uses the canonical gfx::Device and an ordinary offscreen RGBA8 + depth
// pass, with one true asynchronous pixel readback. Non-Metal backends
// deliberately fail closed until their RHI pixel readback is implemented.
// This does not replace the CPU BVH picker, GPU actor pose, or ECS.
#pragma once
#include "SculptGpuPickShaders.h"
#include "SculptGpuAlphaMask.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace aiverse::vivid {

class SculptGpuIdPass final {
    struct Reply {
        std::vector<std::uint8_t> rgba;
        bool ready=false;
    };
public:
    struct Completed {
        SculptGpuPickRequest request{};
        std::vector<std::uint8_t> rgba;
    };
private:
    struct Pending {
        gfx::BufferHandle vertex;
        std::vector<gfx::BufferHandle> masked;
        std::shared_ptr<Reply> reply;
        SculptGpuPickRequest request{};
    };
    gfx::Device* device_=nullptr; // render-owner thread, borrowed
    gfx::ShaderHandle shader_{};
    gfx::PipelineHandle pipeline_{};
    gfx::ShaderHandle maskShader_{};
    gfx::PipelineHandle maskPipeline_{};
    gfx::TextureHandle color_,depth_;
    int width_=0,height_=0;
    std::vector<Pending> pending_;
    std::vector<SculptGpuIdVertex> vertices_;
    bool supported_=false;
public:
    SculptGpuIdPass()=default;
    SculptGpuIdPass(const SculptGpuIdPass&)=delete;
    SculptGpuIdPass& operator=(const SculptGpuIdPass&)=delete;

    bool initialize(gfx::Device& device) {
        if(device_ || device.info().backend!=gfx::Backend::Metal)return false;
        const auto shaderDesc=sculptGpuIdShaderDesc();
        auto shader=device.createShader(shaderDesc);
        if(!shader.valid())return false;
        auto pipeline=device.createPipeline(
            sculptGpuIdPipelineDesc(shader,gfx::CullMode::None));
        if(!pipeline.valid()) {
            device.destroyShader(shader);
            return false;
        }
        const auto maskShader=device.createShader(sculptGpuMaskShaderDesc());
        if(!maskShader.valid()) {
            device.destroyPipeline(pipeline);
            device.destroyShader(shader);
            return false;
        }
        const auto maskPipeline=device.createPipeline(
            sculptGpuMaskPipelineDesc(maskShader));
        if(!maskPipeline.valid()) {
            device.destroyShader(maskShader);
            device.destroyPipeline(pipeline);
            device.destroyShader(shader);
            return false;
        }
        device_=&device;
        shader_=shader;
        pipeline_=pipeline;
        maskShader_=maskShader;
        maskPipeline_=maskPipeline;
        supported_=true;
        return true;
    }
    bool supported() const noexcept {return supported_;}
    bool pending() const noexcept {return !pending_.empty();}
    bool hasTargets() const noexcept {return color_.valid()&&depth_.valid();}

    // Pixel callback marks Reply only: never captures CreatorApp or accesses
    // the Device off-thread. A later owner frame polls and retires buffers.
    bool takeCompleted(Completed& out) {
        if(!device_ || pending_.empty() ||
           !pending_.front().reply->ready)return false;
        auto p=std::move(pending_.front());
        pending_.erase(pending_.begin());
        // The completed callback proves the corresponding command buffer
        // finished, so its immutable vertex allocation can be released.
        if(p.vertex.valid())device_->destroyBuffer(p.vertex);
        for(auto handle:p.masked)device_->destroyBuffer(handle);
        out.request=p.request;
        out.rgba=std::move(p.reply->rgba);
        return true;
    }

    // Recording occurs only on the owner thread during a frame, outside any
    // other render/compute pass. x/y use top-left frame coordinates. Callback
    // will be delivered on a later beginFrame and consumed on an owner frame.
    // Opaque triangles and zero-ID scene occluders are drawn first.
    // Eligible standard alpha-masked materials use their original GPU
    // base texture and alphaCutoff in a second depth-tested shader,
    // discarding transparent pixels. Unsupported/custom/Blend paths
    // continue as conservative zero-ID depth blockers. Renderer3D's
    // actual vertex override, LOD and arbitrary external scene inputs
    // remain separate acceptance gates.
    bool record(const render::Model& model,
                const SkinnedWeightSurface& surface,
                std::size_t authorCount,
                std::span<const std::uint32_t> authorIds,
                const render::mat4& viewProj,
                const SculptGpuPickRequest& request,
                std::span<const SculptGpuIdVertex> occluders={},
                std::span<const SculptGpuMaskBatch> maskedOccluders={}) {
        if(!device_||!supported_||!request.valid()||
           !device_->frameSerial() ||
           device_->frameSerial()!=request.frameSerial ||
           request.meshId!=surface.mesh ||
           !pending_.empty() || retiredWithoutCallback_.size()>=512u ||
           request.width>4096u || request.height>4096u ||
           !device_->gpuHealthy())return false;
        vertices_.clear();
        std::vector<SculptGpuMaskBatch> grouped;
        if(!appendSculptGpuMaterialGeometry(
                model,surface,authorCount,authorIds,true,
                vertices_,grouped))return false;
        if(maskedOccluders.size()+grouped.size()>256u)return false;
        std::size_t total=vertices_.size()+occluders.size();
        if(total>3000000u)return false;
        for(const auto& batch:grouped) {
            if(batch.vertices.size()>3000000u-total)return false;
            total+=batch.vertices.size();
        }
        for(const auto& batch:maskedOccluders) {
            if(!batch.baseTexture.valid() || batch.vertices.empty() ||
               batch.vertices.size()%3u ||
               batch.vertices.size()>3000000u-total)
                return false;
            for(const auto& v:batch.vertices) {
                if(v.rgba[0]||v.rgba[1]||v.rgba[2]||v.rgba[3]||
                   !std::isfinite(v.world.x)||
                   !std::isfinite(v.world.y)||
                   !std::isfinite(v.world.z))return false;
                for(float value:v.uvAlpha)
                    if(!std::isfinite(value))return false;
            }
            for(float value:batch.uniform.alpha)
                if(!std::isfinite(value))return false;
            for(float value:batch.uniform.uv0)
                if(!std::isfinite(value))return false;
            for(float value:batch.uniform.uv1)
                if(!std::isfinite(value))return false;
            total+=batch.vertices.size();
        }
        if(!total)return false;
        for(const auto& batch:maskedOccluders)
            grouped.push_back(batch);
        // Explicit scene occluders must never carry a selectable ID.
        // Reject mismatched data instead of accidentally returning the
        // ordinal of a hair/floor/background triangle.
        for(const auto& vertex:occluders) {
            if(vertex.rgba[0]||vertex.rgba[1]||
               vertex.rgba[2]||vertex.rgba[3]||
               !std::isfinite(vertex.world.x)||
               !std::isfinite(vertex.world.y)||
               !std::isfinite(vertex.world.z))return false;
        }
        vertices_.insert(vertices_.end(),occluders.begin(),occluders.end());
        for(float v:viewProj.m)if(!std::isfinite(v))return false;
        const int w=int(request.width),h=int(request.height);
        if((!color_.valid()||!depth_.valid()) || w!=width_ || h!=height_) {
            // Resizing is rare. GPU owner must prove the prior submitted
            // frame no longer uses old RTs before reallocating them.
            if((color_.valid()||depth_.valid()) &&
               !device_->waitForResourceIdle())return false;
            if(color_.valid())device_->destroyTexture(color_);
            if(depth_.valid())device_->destroyTexture(depth_);
            color_=depth_={};
            width_=height_=0;
            gfx::TextureDesc d;
            d.width=w;d.height=h;
            d.renderTarget=true;
            d.filter=gfx::Filter::Nearest;
            d.format=gfx::PixelFormat::RGBA8;
            d.label="VIVID sculpt ID color RT";
            auto color=device_->createTexture(d);
            d.format=gfx::PixelFormat::Depth;
            d.label="VIVID sculpt depth RT";
            auto depth=device_->createTexture(d);
            if(!color.valid()||!depth.valid()) {
                if(color.valid())device_->destroyTexture(color);
                if(depth.valid())device_->destroyTexture(depth);
                return false;
            }
            color_=color;depth_=depth;
            width_=w;height_=h;
        }
        gfx::BufferDesc v;
        v.type=gfx::BufferType::Vertex;
        v.usage=gfx::Usage::Immutable;
        gfx::BufferHandle vertex{};
        if(!vertices_.empty()) {
            v.size=vertices_.size()*sizeof(SculptGpuIdVertex);
            v.data=vertices_.data();
            v.label="VIVID opaque triangle IDs (one click)";
            vertex=device_->createBuffer(v);
            if(!vertex.valid())return false;
        }
        std::vector<gfx::BufferHandle> maskBuffers;
        maskBuffers.reserve(grouped.size());
        for(const auto& batch:grouped) {
            if(!batch.baseTexture.valid() || batch.vertices.empty() ||
               batch.vertices.size()%3u) {
                if(vertex.valid())device_->destroyBuffer(vertex);
                for(auto handle:maskBuffers)device_->destroyBuffer(handle);
                return false;
            }
            v.size=batch.vertices.size()*sizeof(SculptGpuMaskVertex);
            v.data=batch.vertices.data();
            v.label="VIVID masked triangle IDs (one click)";
            auto buffer=device_->createBuffer(v);
            if(!buffer.valid()) {
                if(vertex.valid())device_->destroyBuffer(vertex);
                for(auto handle:maskBuffers)device_->destroyBuffer(handle);
                return false; // no GPU commands have been recorded
            }
            maskBuffers.push_back(buffer);
        }
        auto reply=std::make_shared<Reply>();
        // Metal viewport/scissor use the top-left convention exposed by
        // gfx::Device; no framebuffer origin conversion belongs in VIVID.
        gfx::PassDesc pass;
        pass.colorTarget=color_;
        pass.depthTarget=depth_;
        pass.clearColor[0]=pass.clearColor[1]=
            pass.clearColor[2]=pass.clearColor[3]=0;
        device_->beginPass(pass);
        device_->setViewport({0,0,w,h});
        device_->setScissor({int(request.x),int(request.y),1,1});
        if(vertex.valid()) {
            device_->setPipeline(pipeline_);
            device_->setVertexBuffer(vertex);
            device_->setUniforms(0,viewProj.m,sizeof(viewProj.m));
            device_->draw(std::uint32_t(vertices_.size()));
        }
        // Masked fragments use native GPU sampling and discard. Draw AFTER
        // opaque/zero-ID blockers, sharing their hardware depth buffer.
        for(std::size_t i=0;i<grouped.size();++i) {
            const auto& batch=grouped[i];
            device_->setPipeline(maskPipeline_);
            device_->setVertexBuffer(maskBuffers[i]);
            device_->setTexture(0,batch.baseTexture);
            device_->setUniforms(0,viewProj.m,sizeof(viewProj.m));
            device_->setUniforms(1,&batch.uniform,sizeof(batch.uniform));
            device_->draw(std::uint32_t(batch.vertices.size()));
        }
        device_->endPass();
        // Completion is genuinely asynchronous on Metal. This may fail if
        // a device is unsupported/lost, but the draw already references the
        // buffer; retain it until a verified idle barrier on shutdown.
        bool read=device_->readTexturePixelAsync(
            color_,int(request.x),int(request.y),
            [reply](std::vector<std::uint8_t> bytes) {
                reply->rgba=std::move(bytes);
                reply->ready=true;
            });
        if(!read) {
            if(vertex.valid())retiredWithoutCallback_.push_back(vertex);
            retiredWithoutCallback_.insert(retiredWithoutCallback_.end(),
                                           maskBuffers.begin(),maskBuffers.end());
            return false;
        }
        pending_.push_back({vertex,std::move(maskBuffers),
                            std::move(reply),request});
        return true;
    }

    // Caller must be at a safe render-owner shutdown/idle point. If native
    // GPU completion cannot be proven, do not release in-flight allocations.
    bool shutdownAfterIdle() {
        if(!device_)return true;
        if(!device_->waitForResourceIdle())return false;
        for(auto& p:pending_) {
            if(p.vertex.valid())device_->destroyBuffer(p.vertex);
            for(auto buffer:p.masked)device_->destroyBuffer(buffer);
        }
        pending_.clear();
        for(auto b:retiredWithoutCallback_)device_->destroyBuffer(b);
        retiredWithoutCallback_.clear();
        if(color_.valid())device_->destroyTexture(color_);
        if(depth_.valid())device_->destroyTexture(depth_);
        if(maskPipeline_.valid())device_->destroyPipeline(maskPipeline_);
        if(maskShader_.valid())device_->destroyShader(maskShader_);
        if(pipeline_.valid())device_->destroyPipeline(pipeline_);
        if(shader_.valid())device_->destroyShader(shader_);
        color_=depth_={};
        pipeline_= {};
        shader_={};
        maskPipeline_={};
        maskShader_={};
        device_=nullptr;
        supported_=false;
        width_=height_=0;
        return true;
    }
private:
    // Rare command submission failures: retain these GPU buffers until
    // a verified idle barrier. Never free a buffer in the same GPU frame
    // merely because the async readback request was rejected.
    std::vector<gfx::BufferHandle> retiredWithoutCallback_;
};

} // namespace aiverse::vivid
