// SpectraFX opt-in GPU evolution stage.
// Uses the existing gfx::Device compute/storage API. Caller owns CPU fallback
// via ParticleSystem when init/step fails; never execute both simulators for
// the same emitter in the same frame.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "../gfx/Gfx.h"
#include "../render/Math3D.h"
#include "SpectraFxComputeShaders.h"

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
    // Best-effort, nonblocking GPU impact notifications (Metal, <=4096 slots).
    bool reportImpacts = false;
    float groundHeight = 0;
    float restitution = 0.35f; // vertical bounce [0,1]
    float tangentRetention = 0.8f; // horizontal velocity multiplier [0,1]
};

class SpectraFxGpuEvolution {
public:
    static constexpr uint32_t kMaxParticles = 65536;
    static constexpr uint32_t kTextureWidth = 256;
    SpectraFxGpuEvolution() = default;
    ~SpectraFxGpuEvolution() = default; // shutdown(device) before Device teardown
    SpectraFxGpuEvolution(const SpectraFxGpuEvolution&) = delete;
    SpectraFxGpuEvolution& operator=(const SpectraFxGpuEvolution&) = delete;

    bool initCapacity(gfx::Device& device,uint32_t capacity,
                      bool enableImpactEvents=false) {
        if(capacity==0 || capacity>kMaxParticles) {
            error_="SpectraFX GPU capacity must be 1..65536";return false;
        }
        std::vector<SpectraFxGpuSeed> dormant(capacity);
        for(auto& seed:dormant) seed.lifetime=0;
        return init(device,dormant,enableImpactEvents);
    }

    bool init(gfx::Device& device, const std::vector<SpectraFxGpuSeed>& seeds,
              bool enableImpactEvents=false) {
        shutdown();
        error_.clear();
        if(!device.supportsCompute()) {
            error_="SpectraFX GPU compute unsupported; use existing CPU ParticleSystem";
            return false;
        }
        if(seeds.empty() || seeds.size()>kMaxParticles) {
            error_="SpectraFX GPU particle count must be 1..65536";
            return false;
        }
        if(!device.supports(gfx::PixelFormat::RGBA32F)) {
            error_="SpectraFX RGBA32F storage texture unsupported";
            return false;
        }
        std::vector<render::float4> positions, velocities;
        positions.reserve(seeds.size());
        velocities.reserve(seeds.size());
        for(const auto& seed:seeds) {
            if(!finite(seed.position) || !finite(seed.velocity) ||
               !std::isfinite(seed.age) || !std::isfinite(seed.lifetime) ||
               seed.age<0 || seed.lifetime<0 || seed.age>seed.lifetime) {
                error_="SpectraFX GPU seeds must be finite with valid lifetimes";
                return false;
            }
            positions.push_back({seed.position.x,seed.position.y,seed.position.z,seed.age});
            velocities.push_back({seed.velocity.x,seed.velocity.y,seed.velocity.z,seed.lifetime});
        }
        device_=&device;
        count_=uint32_t(seeds.size());
        feedbackEnabled_=enableImpactEvents;
        shader_=device.createCompute({kSpectraFxComputeMetal,kSpectraFxComputeGLSL,
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
        gfx::TextureDesc texture;
        texture.width=int(kTextureWidth);
        texture.height=int((count_+kTextureWidth-1)/kTextureWidth);
        texture.format=gfx::PixelFormat::RGBA32F;
        texture.filter=gfx::Filter::Nearest;
        texture.storage=true;
        texture.label="SpectraFX GPU position texture";
        output_=device.createTexture(texture);
        if(!position_.valid() || !velocity_.valid() || !impacts_.valid() || !output_.valid()) {
            error_="SpectraFX GPU buffer/texture allocation failed: "+device.lastError();
            shutdown();
            return false;
        }
        return true;
    }

    // Write/recycle one slot. Caller must perform this at a GPU-buffer update
    // safe point, before compute dispatch and with no render/compute pass open.
    // Lifetime zero marks the slot dormant (billboard alpha zero).
    bool writeSlot(uint32_t slot,const SpectraFxGpuSeed& seed) {
        if(!ready() || slot>=count_ || !device_->canUpdateSharedBuffers())
            return false;
        if(!finite(seed.position) || !finite(seed.velocity) ||
           !std::isfinite(seed.age)||!std::isfinite(seed.lifetime)||
           seed.age<0||seed.lifetime<0||seed.age>seed.lifetime)
            return false;
        const render::float4 a{seed.position.x,seed.position.y,seed.position.z,seed.age};
        const render::float4 v{seed.velocity.x,seed.velocity.y,seed.velocity.z,seed.lifetime};
        device_->updateBuffer(position_,&a,sizeof(a),size_t(slot)*sizeof(a));
        device_->updateBuffer(velocity_,&v,sizeof(v),size_t(slot)*sizeof(v));
        return true;
    }

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

    // Device-owned output is usable by compatible shaders after compute ends.
    // No synchronous CPU readback is required for regular rendering.
    gfx::TextureHandle positionTexture() const {return output_;}
    gfx::BufferHandle positionBuffer() const {return position_;}
    gfx::BufferHandle velocityBuffer() const {return velocity_;}
    // 16 bytes per particle, bounded to the RHI's 64-KiB async readback.
    gfx::BufferHandle impactBuffer() const {return impacts_;}
    bool impactReadbackEligible() const {
        return ready() && feedbackEnabled_ && count_<=4096;
    }
    bool impactFeedbackAllocated() const {return feedbackEnabled_;}
    // Nominal RHI payload bytes only: excludes driver heaps, pipeline
    // caches, alignment, transient staging and GPU allocator overhead.
    uint64_t nominalGpuBytes() const {
        if(!ready()) return 0;
        const uint64_t cells=uint64_t(kTextureWidth)*
            ((uint64_t(count_)+kTextureWidth-1)/kTextureWidth);
        return uint64_t(2)*count_*sizeof(render::float4) +
               uint64_t(feedbackEnabled_?count_:1)*sizeof(render::float4) +
               cells*sizeof(render::float4);
    }
    uint32_t count() const {return count_;}
    bool ready() const {return device_ && shader_.valid() && position_.valid() &&
                               velocity_.valid() && impacts_.valid() && output_.valid();}
    const std::string& lastError() const {return error_;}

    void shutdown() {
        if(device_) {
            if(impacts_.valid()) device_->destroyBuffer(impacts_);
            if(output_.valid()) device_->destroyTexture(output_);
            if(position_.valid()) device_->destroyBuffer(position_);
            if(velocity_.valid()) device_->destroyBuffer(velocity_);
            if(shader_.valid()) device_->destroyCompute(shader_);
        }
        device_=nullptr;
        shader_={};position_={};velocity_={};impacts_={};output_={};count_=0;
        feedbackEnabled_=false;
    }

private:
    static bool finite(render::float3 value) {
        return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);
    }
    gfx::Device* device_=nullptr;
    gfx::ComputeHandle shader_;
    gfx::BufferHandle position_,velocity_,impacts_;
    gfx::TextureHandle output_;
    uint32_t count_=0;
    bool feedbackEnabled_=false;
    std::string error_;
};
} // namespace aiverse::fx
