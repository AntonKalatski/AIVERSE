// SpectraFX Metal-only GPU back-to-front alpha sorting.
// Sorts GPU-evolved particle slot indices without synchronous CPU readback.
// Uses separate compute encoders per bitonic merge pass to make storage
// hazards explicit in the current AIVERSE RHI (no per-dispatch barrier API).
// This is deliberately capped at 4096 slots; CPU particle sorting remains
// the default when this opt-in backend is unsupported.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include "../gfx/Gfx.h"
#include "../render/Math3D.h"
#include "SpectraFxGpuEvolution.h"

namespace aiverse::fx {

inline constexpr const char* kSpectraFxAlphaSortMetal=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct SortItem { float depth; uint slot; };
struct Params {
    float4 eye;
    float4 forward;
    uint count;
    uint paddedCount;
    uint width;
    uint stage;
    uint pass;
    uint mode;
    uint pad0;
    uint pad1;
};
kernel void cs_main(
    uint i [[thread_position_in_grid]],
    device SortItem* items [[buffer(0)]],
    constant Params& u [[buffer(8)]],
    texture2d<float,access::read> positions [[texture(0)]],
    texture2d<float,access::write> indices [[texture(1)]]) {
    if(i>=u.paddedCount) return;
    if(u.mode==0u) {
        float depth=-3.402823466e38f; // inactive/padded slots sort last
        if(i<u.count) {
            const float4 p=positions.read(uint2(i%u.width,i/u.width));
            if(p.w>0.0f) depth=dot(p.xyz-u.eye.xyz,u.forward.xyz);
        }
        items[i]={depth,i};
        return;
    }
    if(u.mode==1u) {
        // Dispatch only paddedCount/2 pair threads, not one thread per
        // endpoint. Each pair is written exactly once; no atomic operations.
        if(i>=u.paddedCount/2u || u.pass==0u) return;
        const uint left=(i/u.pass)*(u.pass*2u)+(i%u.pass);
        const uint peer=left+u.pass;
        const SortItem a=items[left];
        const SortItem b=items[peer];
        // Stable ordering for equal depth (lower source slot first).
        const bool aBeforeB=(a.depth>b.depth) ||
                             (a.depth==b.depth && a.slot<b.slot);
        const bool descending=(left & u.stage)==0u;
        if((descending && !aBeforeB) || (!descending && aBeforeB)) {
            items[left]=b;
            items[peer]=a;
        }
        return;
    }
    if(i<u.count) {
        // The first count items always contain all live source slots;
        // inactive/padded entries are transparent and stay at the tail.
        indices.write(float4(float(items[i].slot),0.0f,0.0f,1.0f),
                      uint2(i%u.width,i/u.width));
    }
}
)MSL";

class SpectraFxGpuAlphaSort {
public:
    static constexpr uint32_t kMaxParticles=4096;
    static constexpr size_t kBufferedFrames=3;
    SpectraFxGpuAlphaSort()=default;
    SpectraFxGpuAlphaSort(const SpectraFxGpuAlphaSort&)=delete;
    SpectraFxGpuAlphaSort& operator=(const SpectraFxGpuAlphaSort&)=delete;
    ~SpectraFxGpuAlphaSort()=default; // release before gfx::Device shutdown

    bool init(gfx::Device& device,uint32_t count,
              gfx::ComputeHandle sharedCompute={}) {
        release();
        error_.clear();
        if(device.info().backend!=gfx::Backend::Metal ||
           !device.supportsCompute() || !device.frameSerial() ||
           !device.supports(gfx::PixelFormat::RGBA32F) ||
           count==0 || count>kMaxParticles) {
            error_="SpectraFX GPU alpha sorting requires Metal and <=4096 slots";
            return false;
        }
        count_=count;
        padded_=1;
        while(padded_<count_) padded_<<=1;
        device_=&device;
        ownsShader_=!sharedCompute.valid();
        shader_=sharedCompute.valid()?sharedCompute:
            device.createCompute({kSpectraFxAlphaSortMetal,nullptr,
                                  "SpectraFX bitonic alpha depth sorting"});
        if(!shader_.valid()) {
            error_=device.lastError();release();return false;
        }
        gfx::TextureDesc texture{};
        texture.width=int(SpectraFxGpuEvolution::kTextureWidth);
        texture.height=int((count_+SpectraFxGpuEvolution::kTextureWidth-1)/
                           SpectraFxGpuEvolution::kTextureWidth);
        texture.format=gfx::PixelFormat::RGBA32F;
        texture.filter=gfx::Filter::Nearest;
        texture.storage=true;
        texture.label="SpectraFX GPU sorted alpha indices";
        // Metal permits up to three frames in flight. A single items buffer
        // or sorted-index texture would be overwritten by frame N+1 while
        // frame N might still be consuming it. Own distinct scratch per slot.
        for(auto& frame:frames_) {
            frame.items=device.createBuffer({
                gfx::BufferType::Storage,gfx::Usage::Immutable,
                size_t(padded_)*8u,nullptr,
                "SpectraFX per-frame alpha sort records"});
            frame.sorted=device.createTexture(texture);
            if(!frame.items.valid() || !frame.sorted.valid()) {
                error_="SpectraFX GPU alpha sort frame allocation failed";
                release();return false;
            }
        }
        return true;
    }

    // Must be called after GPU evolution and before an open render pass,
    // inside the device frame. GPU sorting is ordered back-to-front by the
    // projection of the particle onto the camera's forward direction.
    bool prepare(const SpectraFxGpuEvolution& particles,
                 render::float3 cameraEye,render::float3 cameraForward) {
        if(!ready()||!particles.ready()||particles.count()!=count_||
           !device_->frameSerial()||!device_->canUpdateSharedBuffers()||
           !finite(cameraEye)||!finite(cameraForward)) return false;
        const uint64_t serial=device_->frameSerial();
        Frame& frame=frames_[serial%kBufferedFrames];
        frame.preparedSerial=0;
        frame.sourceTexture={};
        const float forwardLength=render::length(cameraForward);
        if(!(forwardLength>1e-6f) || !std::isfinite(forwardLength)) {
            error_="SpectraFX GPU alpha sort requires a valid view direction";
            return false;
        }
        cameraForward=cameraForward*(1.0f/forwardLength);
        struct alignas(16) Params {
            render::float4 eye,forward;
            uint32_t count,paddedCount,width,stage;
            uint32_t pass,mode,pad0,pad1;
        };
        static_assert(sizeof(Params)==64);
        Params params{
            {cameraEye.x,cameraEye.y,cameraEye.z,0},
            {cameraForward.x,cameraForward.y,cameraForward.z,0},
            count_,padded_,SpectraFxGpuEvolution::kTextureWidth,0,
            0,0,0,0
        };
        auto dispatch=[&](uint32_t mode,uint32_t stage,uint32_t pass,
                          uint32_t threads)->bool {
            params.mode=mode;params.stage=stage;params.pass=pass;
            if(!device_->beginCompute(shader_)) {
                error_="SpectraFX GPU alpha sort compute encoder unavailable";
                return false;
            }
            device_->setComputeBuffer(0,frame.items);
            device_->setComputeTexture(0,particles.positionTexture());
            device_->setComputeTexture(1,frame.sorted);
            device_->setComputeUniforms(&params,sizeof(params));
            device_->dispatch(threads);
            device_->endCompute();
            return true;
        };
        if(!dispatch(0,0,0,padded_)) return false;
        for(uint32_t stage=2;stage<=padded_;stage<<=1) {
            for(uint32_t pass=stage>>1;pass;pass>>=1)
                if(!dispatch(1,stage,pass,padded_/2u)) return false;
        }
        if(!dispatch(2,0,0,count_)) return false;
        frame.preparedSerial=serial;
        frame.sourceTexture=particles.positionTexture();
        error_.clear();
        return true;
    }

    bool preparedFor(const SpectraFxGpuEvolution& particles) const {
        if(!ready() || !particles.ready() || particles.count()!=count_)
            return false;
        const uint64_t serial=device_->frameSerial();
        if(!serial) return false;
        const Frame& frame=frames_[serial%kBufferedFrames];
        return frame.preparedSerial==serial &&
               frame.sourceTexture==particles.positionTexture();
    }
    gfx::TextureHandle sortedIndexTexture() const {
        if(!ready()) return {};
        const uint64_t serial=device_->frameSerial();
        if(!serial) return {};
        return frames_[serial%kBufferedFrames].sorted;
    }
    bool ready() const {
        if(!device_ || !shader_.valid()) return false;
        for(const auto& frame:frames_)
            if(!frame.items.valid() || !frame.sorted.valid()) return false;
        return true;
    }
    const std::string& lastError() const {return error_;}
    uint64_t nominalGpuBytes() const {
        if(!ready()) return 0;
        const uint64_t cells=uint64_t(SpectraFxGpuEvolution::kTextureWidth)*
            ((uint64_t(count_)+SpectraFxGpuEvolution::kTextureWidth-1)/
             SpectraFxGpuEvolution::kTextureWidth);
        return kBufferedFrames*(uint64_t(padded_)*8u +
                                cells*sizeof(render::float4));
    }
    uint32_t capacity() const {return count_;}

    void release() {
        if(device_) {
            for(auto& frame:frames_) {
                if(frame.sorted.valid()) device_->destroyTexture(frame.sorted);
                if(frame.items.valid()) device_->destroyBuffer(frame.items);
            }
            if(ownsShader_ && shader_.valid()) device_->destroyCompute(shader_);
        }
        frames_={};
        device_=nullptr;shader_={};
        count_=padded_=0;
        ownsShader_=false;
    }
private:
    static bool finite(render::float3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    }
    struct Frame {
        gfx::BufferHandle items{};
        gfx::TextureHandle sorted{},sourceTexture{};
        uint64_t preparedSerial=0;
    };
    gfx::Device* device_=nullptr;
    gfx::ComputeHandle shader_{};
    bool ownsShader_=false;
    std::array<Frame,kBufferedFrames> frames_{};
    uint32_t count_=0,padded_=0;
    std::string error_;
};
} // namespace aiverse::fx
