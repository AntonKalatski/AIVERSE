// SpectraFX particle evolution stage: GPU compute or its CPU twin.
//
// GPU mode (default on compute-capable RHIs, Metal today): positions and
// velocities live in storage buffers, the compute kernel writes an RGBA32F
// position texture that the billboard renderer samples directly. Regular
// frames never read particle state back to the CPU.
//
// CPU twin mode (init(..., cpuTwin=true), chosen automatically by
// SpectraFxHybridEmitter when compute is unavailable or the asset asks for
// "cpu"): the SAME seeds are integrated by spectraFxEvolveParticle (the C++
// copy of the compute kernel) and the live particles are uploaded into a
// plain RGBA32F texture ring that the SAME billboard renderer samples. The
// upload is packed (live particles only, back-to-front when a sort view is
// set), so the twin needs neither GPU compaction nor GPU sorting.
// Never execute both simulators for the same emitter in the same frame.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>
#include "../gfx/Gfx.h"
#include "../render/Math3D.h"
#include "SpectraFxComputeShaders.h"
#include "SpectraFxEvolutionKernel.h"

namespace aiverse::fx {

struct SpectraFxGpuSeed {
    render::float3 position{};
    render::float3 velocity{};
    float age = 0;
    float lifetime = 1;
};
struct SpectraFxGpuEnvironment {
    render::float3 acceleration{0,-9.81f,0};
    float damping = 1;
    // World-space horizontal ground plane only; arbitrary terrain, signed
    // distance fields and depth-buffer collision require other modules.
    bool collideGround = false;
    // Best-effort GPU impact notifications (Metal async readback, <=4096
    // slots); the CPU twin reports them directly without readback.
    bool reportImpacts = false;
    float groundHeight = 0;
    float restitution = 0.35f; // vertical bounce [0,1]
    float tangentRetention = 0.8f; // horizontal velocity multiplier [0,1]
};

class SpectraFxGpuEvolution {
public:
    static constexpr uint32_t kMaxParticles = 65536;
    static constexpr uint32_t kTextureWidth = 256;
    // CPU twin texture ring: a texture is rewritten only after the two frames
    // that may still sample it have been submitted (3 frames in flight).
    static constexpr size_t kTwinRing = 3;
    SpectraFxGpuEvolution() = default;
    ~SpectraFxGpuEvolution() = default; // shutdown(device) before Device teardown
    SpectraFxGpuEvolution(const SpectraFxGpuEvolution&) = delete;
    SpectraFxGpuEvolution& operator=(const SpectraFxGpuEvolution&) = delete;

    bool initCapacity(gfx::Device& device,uint32_t capacity,
                      bool enableImpactEvents=false,
                      gfx::ComputeHandle sharedCompute={},
                      bool cpuTwin=false) {
        if(capacity==0 || capacity>kMaxParticles) {
            error_="SpectraFX GPU capacity must be 1..65536";return false;
        }
        std::vector<SpectraFxGpuSeed> dormant(capacity);
        for(auto& seed:dormant) seed.lifetime=0;
        return init(device,dormant,enableImpactEvents,sharedCompute,cpuTwin);
    }

    bool init(gfx::Device& device, const std::vector<SpectraFxGpuSeed>& seeds,
              bool enableImpactEvents=false,
              gfx::ComputeHandle sharedCompute={},
              bool cpuTwin=false) {
        shutdown();
        error_.clear();
        if(!cpuTwin && !device.supportsCompute()) {
            error_="SpectraFX GPU compute unsupported; use the CPU twin";
            return false;
        }
        if(seeds.empty() || seeds.size()>kMaxParticles) {
            error_="SpectraFX GPU particle count must be 1..65536";
            return false;
        }
        if(!device.supports(gfx::PixelFormat::RGBA32F)) {
            error_="SpectraFX RGBA32F texture unsupported";
            return false;
        }
        std::vector<render::float4> positions, velocities;
        positions.reserve(seeds.size());
        velocities.reserve(seeds.size());
        for(const auto& seed:seeds) {
            if(!validSeed(seed)) {
                error_="SpectraFX GPU seeds must be finite with valid lifetimes";
                return false;
            }
            positions.push_back({seed.position.x,seed.position.y,seed.position.z,seed.age});
            velocities.push_back({seed.velocity.x,seed.velocity.y,seed.velocity.z,seed.lifetime});
        }
        device_=&device;
        count_=uint32_t(seeds.size());
        feedbackEnabled_=enableImpactEvents;
        twin_=cpuTwin;
        gfx::TextureDesc texture;
        texture.width=int(kTextureWidth);
        texture.height=int((count_+kTextureWidth-1)/kTextureWidth);
        texture.format=gfx::PixelFormat::RGBA32F;
        texture.filter=gfx::Filter::Nearest;
        if(twin_) {
            cpuPosition_=std::move(positions);
            cpuVelocity_=std::move(velocities);
            upload_.assign(size_t(kTextureWidth)*size_t(texture.height),{});
            texture.label="SpectraFX CPU twin position texture";
            for(auto& t:ring_) {
                texture.data=upload_.data();
                t=device.createTexture(texture);
                if(!t.valid()) {
                    error_="SpectraFX CPU twin texture allocation failed: "+device.lastError();
                    shutdown();
                    return false;
                }
            }
            ringIndex_=0;
            output_=ring_[0];
            drawCount_=0;
            return true;
        }
        // Shared compute program is borrowed, not owned by this batch.
        // Its owner must outlive every emitter and release after their
        // storage resources have been destroyed.
        ownsShader_=!sharedCompute.valid();
        shader_=sharedCompute.valid()?sharedCompute:
                 device.createCompute({kSpectraFxComputeMetal,
                                       kSpectraFxComputeGLSL,
                                       "SpectraFX GPU evolution"});
        if(!shader_.valid()) {
            error_=device.lastError();
            shutdown();
            return false;
        }
        position_=device.createBuffer({gfx::BufferType::Storage,gfx::Usage::Dynamic,
                                       positions.size()*sizeof(render::float4),
                                       positions.data(),"SpectraFX positions"});
        velocity_=device.createBuffer({gfx::BufferType::Storage,gfx::Usage::Dynamic,
                                       velocities.size()*sizeof(render::float4),
                                       velocities.data(),"SpectraFX velocities"});
        // xyz=impact position, w=1 if this particle hit the ground this dispatch.
        // Readback is opt-in/asynchronous; evolution never waits on the CPU.
        // Metal validates every declared compute binding, so keep one
        // dummy float4 slot when feedback is off. Shader guards all writes
        // with Params.pad. This saves 16*(capacity-1) GPU buffer bytes.
        impacts_=device.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,
                                      size_t(enableImpactEvents?count_:1) *
                                          sizeof(render::float4),
                                      nullptr,"SpectraFX impact events"});
        texture.storage=true;
        texture.label="SpectraFX GPU position texture";
        output_=device.createTexture(texture);
        if(!position_.valid() || !velocity_.valid() || !impacts_.valid() || !output_.valid()) {
            error_="SpectraFX GPU buffer/texture allocation failed: "+device.lastError();
            shutdown();
            return false;
        }
        drawCount_=count_;
        return true;
    }

    // Write/recycle one slot. GPU mode: at a GPU-buffer update safe point,
    // before compute dispatch and with no render/compute pass open.
    // Lifetime zero marks the slot dormant (billboard alpha zero).
    // Writes are queued and uploaded at the start of the next step(): only
    // the compute kernel reads these buffers (drawing samples the texture it
    // writes), so deferring them is invisible. Runs of consecutive written
    // slots go up as one range per buffer; a range never spans a slot that
    // was not written this frame, so GPU-evolved state is never overwritten.
    bool writeSlot(uint32_t slot,const SpectraFxGpuSeed& seed) {
        if(!ready() || slot>=count_ || !validSeed(seed)) return false;
        const render::float4 a{seed.position.x,seed.position.y,seed.position.z,seed.age};
        const render::float4 v{seed.velocity.x,seed.velocity.y,seed.velocity.z,seed.lifetime};
        if(twin_) {
            cpuPosition_[slot]=a;
            cpuVelocity_[slot]=v;
            return true;
        }
        if(pendingIndex_.size()!=count_) pendingIndex_.assign(count_,kNoPending);
        uint32_t& at=pendingIndex_[slot];
        if(at==kNoPending) {          // last write of a slot in a frame wins
            at=uint32_t(pending_.size());
            pending_.push_back({slot,a,v});
        } else {
            pending_[at].position=a;pending_[at].velocity=v;
        }
        return true;
    }
    // Uploads queued slot writes now (step() does this itself). False when
    // the RHI cannot update shared buffers yet; the queue is kept.
    bool flushWrites() {
        if(pending_.empty()) return true;
        if(!ready() || !device_->canUpdateSharedBuffers()) return false;
        std::sort(pending_.begin(),pending_.end(),
                  [](const PendingWrite& x,const PendingWrite& y){return x.slot<y.slot;});
        for(size_t i=0;i<pending_.size();) {
            size_t j=i+1;
            while(j<pending_.size() && pending_[j].slot==pending_[j-1].slot+1) ++j;
            runPosition_.clear();runVelocity_.clear();
            for(size_t k=i;k<j;++k) {
                runPosition_.push_back(pending_[k].position);
                runVelocity_.push_back(pending_[k].velocity);
            }
            const size_t offset=size_t(pending_[i].slot)*sizeof(render::float4);
            const size_t bytes=runPosition_.size()*sizeof(render::float4);
            device_->updateBuffer(position_,runPosition_.data(),bytes,offset);
            device_->updateBuffer(velocity_,runVelocity_.data(),bytes,offset);
            ++uploadRanges_;
            i=j;
        }
        for(const auto& w:pending_) pendingIndex_[w.slot]=kNoPending;
        pending_.clear();
        return true;
    }
    uint32_t pendingWrites() const {return uint32_t(pending_.size());}
    // Buffer range uploads issued by flushWrites() since init (diagnostics).
    uint64_t uploadRanges() const {return uploadRanges_;}

    // Within beginFrame/endFrame, outside a render pass; dt is limited so that
    // a paused or stalled game cannot explode visual-only velocity integration.
    bool step(float dt,const SpectraFxGpuEnvironment& env={}) {
        if(!ready()) {error_="SpectraFX GPU stage not initialized";return false;}
        if(env.reportImpacts && !feedbackEnabled_) {
            error_="GPU impact feedback was not allocated at initialization";
            return false;
        }
        if(!std::isfinite(dt) || dt<0 || dt>0.1f || !finite(env.acceleration) ||
           !std::isfinite(env.damping) || env.damping<0 || env.damping>1 ||
           !std::isfinite(env.groundHeight) ||
           !std::isfinite(env.restitution) || env.restitution<0 || env.restitution>1 ||
           !std::isfinite(env.tangentRetention) ||
           env.tangentRetention<0 || env.tangentRetention>1) {
            error_="SpectraFX invalid GPU evolution parameters";
            return false;
        }
        if(twin_) return stepTwin(dt,env);
        struct alignas(16) Parameters {
            uint32_t count;
            float dt;
            float damping;
            float pad;
            render::float4 acceleration;
            // x=floorY, y=restitution, z=tangent retention, w=enabled
            render::float4 ground;
        };
        static_assert(sizeof(Parameters)==48);
        const Parameters params{count_,dt,env.damping,
                                env.reportImpacts?1.0f:0.0f,
                                {env.acceleration.x,env.acceleration.y,
                                 env.acceleration.z,0},
                                {env.groundHeight,env.restitution,
                                 env.tangentRetention,env.collideGround?1.0f:0.0f}};
        if(!flushWrites()) {
            error_="SpectraFX GPU slot writes cannot be uploaded yet";
            return false;
        }
        if(!device_->beginCompute(shader_)) {
            error_="SpectraFX compute must run inside frame, outside render pass";
            return false;
        }
        device_->setComputeBuffer(0,position_);
        device_->setComputeBuffer(1,velocity_);
        device_->setComputeBuffer(2,impacts_);
        device_->setComputeTexture(0,output_);
        device_->setComputeUniforms(&params,sizeof(params));
        device_->dispatch(count_);
        device_->endCompute();
        return true;
    }

    // CPU twin: back-to-front order for the next upload (alpha blending).
    // A zero forward vector keeps slot order. GPU mode ignores this; the
    // GPU alpha sort stage (SpectraFxGpuAlphaSort) handles it there.
    void setTwinSortView(render::float3 eye,render::float3 forward) {
        sortEye_=eye;
        sortForward_=forward;
    }
    // Re-pack the CPU twin texture for a new camera without advancing time.
    bool repackTwin() {
        if(!twin_ || !ready()) return false;
        return uploadTwin();
    }

    // Device-owned output is usable by compatible shaders after compute ends.
    // No synchronous CPU readback is required for regular rendering.
    gfx::TextureHandle positionTexture() const {return output_;}
    gfx::BufferHandle positionBuffer() const {return position_;}
    gfx::BufferHandle velocityBuffer() const {return velocity_;}
    // 16 bytes per particle, bounded to the RHI's 64-KiB async readback.
    gfx::BufferHandle impactBuffer() const {return impacts_;}
    bool impactReadbackEligible() const {
        return ready() && !twin_ && feedbackEnabled_ && count_<=4096;
    }
    bool impactFeedbackAllocated() const {return feedbackEnabled_;}
    // CPU twin: ground hits of the last step as (slot, position), no readback.
    struct TwinImpact { uint32_t slot; render::float3 position; };
    const std::vector<TwinImpact>& twinImpacts() const {return twinImpacts_;}
    // Nominal RHI payload bytes only: excludes driver heaps, pipeline
    // caches, alignment, transient staging and GPU allocator overhead.
    uint64_t nominalGpuBytes() const {
        if(!ready()) return 0;
        const uint64_t cells=uint64_t(kTextureWidth)*
            ((uint64_t(count_)+kTextureWidth-1)/kTextureWidth);
        if(twin_) return uint64_t(kTwinRing)*cells*sizeof(render::float4);
        return uint64_t(2)*count_*sizeof(render::float4) +
               uint64_t(feedbackEnabled_?count_:1)*sizeof(render::float4) +
               cells*sizeof(render::float4);
    }
    uint32_t count() const {return count_;}
    // Instances the billboard renderer must draw: the full slot range on the
    // GPU (dead slots have alpha 0), the packed live range for the CPU twin.
    uint32_t drawCount() const {return drawCount_;}
    bool cpuTwin() const {return twin_;}
    // CPU twin state for tests/diagnostics (empty in GPU mode).
    const std::vector<render::float4>& twinPositionAge() const {return cpuPosition_;}
    const std::vector<render::float4>& twinVelocityLife() const {return cpuVelocity_;}
    bool ready() const {
        if(twin_) return device_ && output_.valid() && !cpuPosition_.empty();
        return device_ && shader_.valid() && position_.valid() &&
               velocity_.valid() && impacts_.valid() && output_.valid();
    }
    const std::string& lastError() const {return error_;}

    void shutdown() {
        if(device_) {
            if(impacts_.valid()) device_->destroyBuffer(impacts_);
            if(twin_) {
                for(auto& t:ring_) if(t.valid()) device_->destroyTexture(t);
            } else if(output_.valid()) device_->destroyTexture(output_);
            if(position_.valid()) device_->destroyBuffer(position_);
            if(velocity_.valid()) device_->destroyBuffer(velocity_);
            if(ownsShader_ && shader_.valid()) device_->destroyCompute(shader_);
        }
        device_=nullptr;
        shader_={};position_={};velocity_={};impacts_={};output_={};count_=0;
        ring_={};
        drawCount_=0;
        cpuPosition_.clear();cpuVelocity_.clear();upload_.clear();
        order_.clear();twinImpacts_.clear();
        feedbackEnabled_=false;
        ownsShader_=false;
        twin_=false;
        pending_.clear();pendingIndex_.clear();uploadRanges_=0;
    }

private:
    static bool finite(render::float3 value) {
        return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);
    }
    static bool validSeed(const SpectraFxGpuSeed& seed) {
        return finite(seed.position) && finite(seed.velocity) &&
               std::isfinite(seed.age) && std::isfinite(seed.lifetime) &&
               seed.age>=0 && seed.lifetime>=0 && seed.age<=seed.lifetime;
    }
    bool stepTwin(float dt,const SpectraFxGpuEnvironment& env) {
        SpectraFxEvolutionParams p;
        p.dt=dt;p.damping=env.damping;p.acceleration=env.acceleration;
        p.collideGround=env.collideGround;p.groundHeight=env.groundHeight;
        p.restitution=env.restitution;p.tangentRetention=env.tangentRetention;
        twinImpacts_.clear();
        remaining_.resize(count_);
        for(uint32_t i=0;i<count_;++i) {
            float remaining=0;
            const bool hit=spectraFxEvolveParticle(cpuPosition_[i],cpuVelocity_[i],
                                                   p,remaining);
            remaining_[i]=remaining;
            if(hit && env.reportImpacts && twinImpacts_.size()<512)
                twinImpacts_.push_back({i,{cpuPosition_[i].x,cpuPosition_[i].y,
                                           cpuPosition_[i].z}});
        }
        return uploadTwin();
    }
    bool uploadTwin() {
        // Pack live particles (alpha > 0) first; dormant/expired slots are
        // not drawn at all, unlike the GPU full-range draw.
        order_.clear();
        if(remaining_.size()!=count_) remaining_.assign(count_,0.0f);
        for(uint32_t i=0;i<count_;++i) {
            const auto& v=cpuVelocity_[i];
            const auto& a=cpuPosition_[i];
            if(v.w>0.0f && a.w<v.w) {
                remaining_[i]=std::clamp((v.w-a.w)/v.w,0.0f,1.0f);
                if(remaining_[i]>0.0f) order_.push_back(i);
            }
        }
        const float fl=render::length(sortForward_);
        if(fl>1e-6f && order_.size()>1) {
            const render::float3 f=sortForward_*(1.0f/fl);
            auto depth=[&](uint32_t i) {
                const auto& a=cpuPosition_[i];
                return (a.x-sortEye_.x)*f.x+(a.y-sortEye_.y)*f.y+(a.z-sortEye_.z)*f.z;
            };
            std::stable_sort(order_.begin(),order_.end(),[&](uint32_t l,uint32_t r) {
                return depth(l)>depth(r); // farthest first
            });
        }
        drawCount_=uint32_t(order_.size());
        if(drawCount_==0) return true;
        for(uint32_t k=0;k<drawCount_;++k) {
            const uint32_t i=order_[k];
            const auto& a=cpuPosition_[i];
            upload_[k]={a.x,a.y,a.z,remaining_[i]};
        }
        ringIndex_=(ringIndex_+1)%kTwinRing;
        output_=ring_[ringIndex_];
        const int rows=int((drawCount_+kTextureWidth-1)/kTextureWidth);
        device_->updateTexture(output_,0,0,int(kTextureWidth),rows,upload_.data(),
                               int(kTextureWidth*sizeof(render::float4)));
        return true;
    }
    gfx::Device* device_=nullptr;
    gfx::ComputeHandle shader_;
    gfx::BufferHandle position_,velocity_,impacts_;
    gfx::TextureHandle output_;
    std::array<gfx::TextureHandle,kTwinRing> ring_{};
    size_t ringIndex_=0;
    uint32_t count_=0;
    uint32_t drawCount_=0;
    bool feedbackEnabled_=false;
    bool ownsShader_=false;
    bool twin_=false;
    std::vector<render::float4> cpuPosition_,cpuVelocity_,upload_;
    std::vector<float> remaining_;
    std::vector<uint32_t> order_;
    std::vector<TwinImpact> twinImpacts_;
    render::float3 sortEye_{},sortForward_{};
    std::string error_;
    struct PendingWrite {uint32_t slot;render::float4 position,velocity;};
    static constexpr uint32_t kNoPending=~0u;
    std::vector<PendingWrite> pending_;
    std::vector<uint32_t> pendingIndex_;      // slot -> index in pending_
    std::vector<render::float4> runPosition_,runVelocity_;
    uint64_t uploadRanges_=0;
};
} // namespace aiverse::fx
