// AIVERSE LENS: managed color/depth render targets addressed by stable asset keys.
// Render targets are presentation resources, never ECS component fields.
// An acquired target is stable while its specification remains unchanged.
// Replacement textures are retired only after a confirmed GPU completion
// serial; unknown completion defers destruction until explicitly safe shutdown.
#pragma once
#include "../gfx/Gfx.h"
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <utility>

namespace aiverse::render {

class CameraRenderTargetPool {
public:
    struct Target {
        gfx::TextureHandle color{}, depth{};
        int width=0, height=0;
        bool hdr=false, exposedDepth=true, ready=false;
        bool matches(int w,int h,bool useHdr,bool wantDepth) const {
            return width==w && height==h && hdr==useHdr && exposedDepth==wantDepth &&
                   color.valid() && depth.valid();
        }
    };
    CameraRenderTargetPool() = default;
    CameraRenderTargetPool(const CameraRenderTargetPool&) = delete;
    CameraRenderTargetPool& operator=(const CameraRenderTargetPool&) = delete;
    bool init(gfx::Device& device) {
        if (device_ && device_!=&device) return false;
        device_=&device;
        return true;
    }
    const Target* find(uint64_t key) const {
        auto it=targets_.find(key);
        return it==targets_.end() ? nullptr : &it->second;
    }
    gfx::TextureHandle color(uint64_t key) const {
        const auto* target=find(key);
        return target && target->ready ? target->color : gfx::TextureHandle{};
    }
    gfx::TextureHandle depth(uint64_t key) const {
        const auto* target=find(key);
        return target && target->ready && target->exposedDepth ? target->depth : gfx::TextureHandle{};
    }
    const Target* acquire(uint64_t key,int width,int height,bool hdr=false,bool exposeDepth=true) {
        if (!device_ || !key || width<=0 || height<=0 ||
            width>8192 || height>8192) return nullptr;
        if (hdr && !device_->supports(gfx::PixelFormat::RGBA16F)) return nullptr;
        auto found=targets_.find(key);
        if (found!=targets_.end() && found->second.matches(width,height,hdr,exposeDepth))
            return &found->second;
        gfx::TextureDesc colorDesc;
        colorDesc.width=width;
        colorDesc.height=height;
        colorDesc.renderTarget=true;
        colorDesc.format=hdr?gfx::PixelFormat::RGBA16F:gfx::PixelFormat::RGBA8;
        colorDesc.label="lens camera color";
        Target fresh{};
        fresh.width=width;
        fresh.height=height;
        fresh.hdr=hdr;
        fresh.exposedDepth=exposeDepth;
        fresh.color=device_->createTexture(colorDesc);
        if (!fresh.color.valid()) return nullptr;
        auto depthDesc=colorDesc;
        depthDesc.format=gfx::PixelFormat::Depth;
        depthDesc.filter=gfx::Filter::Nearest;
        depthDesc.label="lens camera depth";
        // Even color-only captures require a depth attachment for correct
        // 3D occlusion. exposedDepth controls public access, not attachment.
        fresh.depth=device_->createTexture(depthDesc);
        if (!fresh.depth.valid()) {
            device_->destroyTexture(fresh.color); // not yet submitted to GPU
            return nullptr;
        }
        if (found!=targets_.end()) {
            retire(found->second);
            found->second=fresh;
            return &found->second;
        }
        auto inserted=targets_.emplace(key,fresh);
        return &inserted.first->second;
    }
    void markReady(uint64_t key) {
        auto it=targets_.find(key);
        if (it!=targets_.end()) it->second.ready=true;
    }
    void markInvalid(uint64_t key) {
        auto it=targets_.find(key);
        if (it!=targets_.end()) it->second.ready=false;
    }
    void release(uint64_t key) {
        auto it=targets_.find(key);
        if (it==targets_.end()) return;
        retire(it->second);
        targets_.erase(it);
    }
    void collectCompleted() {
        if (!device_) return;
        const uint64_t completed=device_->completedFrameSerial();
        if (!completed) return;
        for(auto it=retired_.begin();it!=retired_.end();) {
            if (it->serial && it->serial<=completed) {
                destroy(it->target);
                it=retired_.erase(it);
            } else ++it;
        }
    }
    // Requires an *externally established* GPU-idle point. Gfx::Device does
    // not provide a portable waitIdle API. Never call while GPU may sample RTs.
    void shutdownAfterGpuIdle() {
        if (!device_) return;
        for(auto& [key,target]:targets_) { (void)key; destroy(target); }
        targets_.clear();
        for(auto& retired:retired_) destroy(retired.target);
        retired_.clear();
        device_=nullptr;
    }
    size_t activeTargets() const { return targets_.size(); }
    size_t retiredTargets() const { return retired_.size(); }
private:
    struct Retired { Target target; uint64_t serial=0; };
    gfx::Device* device_=nullptr;
    std::unordered_map<uint64_t,Target> targets_;
    std::deque<Retired> retired_;
    void destroy(const Target& t) {
        if(t.color.valid()) device_->destroyTexture(t.color);
        if(t.depth.valid()) device_->destroyTexture(t.depth);
    }
    void retire(const Target& target) {
        retired_.push_back({target,device_->frameSerial()});
    }
};

} // namespace aiverse::render
