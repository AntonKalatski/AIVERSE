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
};

class SpectraFxGpuEvolution {
public:
    static constexpr uint32_t kMaxParticles = 65536;
    static constexpr uint32_t kTextureWidth = 256;
    SpectraFxGpuEvolution() = default;
    ~SpectraFxGpuEvolution() = default; // shutdown(device) before Device teardown
    SpectraFxGpuEvolution(const SpectraFxGpuEvolution&) = delete;
    SpectraFxGpuEvolution& operator=(const SpectraFxGpuEvolution&) = delete;

    bool init(gfx::Device& device, const std::vector<SpectraFxGpuSeed>& seeds) {
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
               seed.age<0 || seed.lifetime<=0 || seed.age>seed.lifetime) {
                error_="SpectraFX GPU seeds must be finite with valid lifetimes";
                return false;
            }
            positions.push_back({seed.position.x,seed.position.y,seed.position.z,seed.age});
            velocities.push_back({seed.velocity.x,seed.velocity.y,seed.velocity.z,seed.lifetime});
        }
        device_=&device;
        count_=uint32_t(seeds.size());
        shader_=device.createCompute({kSpectraFxComputeMetal,kSpectraFxComputeGLSL,
                                      "SpectraFX GPU evolution"});
        if(!shader_.valid()) {
            error_=device.lastError();
            shutdown();
            return false;
        }
        position_=device.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,
                                       positions.size()*sizeof(render::float4),
                                       positions.data(),"SpectraFX positions"});
        velocity_=device.createBuffer({gfx::BufferType::Storage,gfx::Usage::Immutable,
                                       velocities.size()*sizeof(render::float4),
                                       velocities.data(),"SpectraFX velocities"});
        gfx::TextureDesc texture;
        texture.width=int(kTextureWidth);
        texture.height=int((count_+kTextureWidth-1)/kTextureWidth);
        texture.format=gfx::PixelFormat::RGBA32F;
        texture.filter=gfx::Filter::Nearest;
        texture.storage=true;
        texture.label="SpectraFX GPU position texture";
        output_=device.createTexture(texture);
        if(!position_.valid() || !velocity_.valid() || !output_.valid()) {
            error_="SpectraFX GPU buffer/texture allocation failed: "+device.lastError();
            shutdown();
            return false;
        }
        return true;
    }

    // Within beginFrame/endFrame, outside a render pass; dt is limited so that
    // a paused or stalled game cannot explode visual-only velocity integration.
    bool step(float dt,const SpectraFxGpuEnvironment& env={}) {
        if(!ready()) {error_="SpectraFX GPU stage not initialized";return false;}
        if(!std::isfinite(dt) || dt<0 || dt>0.1f || !finite(env.acceleration) ||
           !std::isfinite(env.damping) || env.damping<0 || env.damping>1) {
            error_="SpectraFX invalid GPU evolution parameters";
            return false;
        }
        struct alignas(16) Parameters {
            uint32_t count;
            float dt;
            float damping;
            float pad;
            render::float4 acceleration;
        };
        static_assert(sizeof(Parameters)==32);
        const Parameters params{count_,dt,env.damping,0,
                                {env.acceleration.x,env.acceleration.y,
                                 env.acceleration.z,0}};
        if(!device_->beginCompute(shader_)) {
            error_="SpectraFX compute must run inside frame, outside render pass";
            return false;
        }
        device_->setComputeBuffer(0,position_);
        device_->setComputeBuffer(1,velocity_);
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
    uint32_t count() const {return count_;}
    bool ready() const {return device_ && shader_.valid() && position_.valid() &&
                               velocity_.valid() && output_.valid();}
    const std::string& lastError() const {return error_;}

    void shutdown() {
        if(device_) {
            if(output_.valid()) device_->destroyTexture(output_);
            if(position_.valid()) device_->destroyBuffer(position_);
            if(velocity_.valid()) device_->destroyBuffer(velocity_);
            if(shader_.valid()) device_->destroyCompute(shader_);
        }
        device_=nullptr;
        shader_={};position_={};velocity_={};output_={};count_=0;
    }

private:
    static bool finite(render::float3 value) {
        return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);
    }
    gfx::Device* device_=nullptr;
    gfx::ComputeHandle shader_;
    gfx::BufferHandle position_,velocity_;
    gfx::TextureHandle output_;
    uint32_t count_=0;
    std::string error_;
};
} // namespace aiverse::fx
