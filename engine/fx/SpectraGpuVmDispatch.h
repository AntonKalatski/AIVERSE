// SpectraFX Stage 23 experimental VM pass. Standalone Metal compute utility,
// not an automatic GraphRuntime backend. Caller owns the device and 64-byte
// ParticleState buffer, and owns scheduling before the particle render pass.
#pragma once
#include "SpectraGpuProgram.h"
#include "SpectraGpuProgramShaders.h"
#include "engine/gfx/Gfx.h"
#include <array>
#include <cstring>
#include <string>

namespace aiverse::spectra::gpu_program {

struct DispatchUniforms {
    uint32_t particleCount = 0;
    uint32_t stage = 0;
    float dt = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(DispatchUniforms)==16);

class MetalVmPass final {
public:
    MetalVmPass() = default;
    ~MetalVmPass() { shutdown(); }
    MetalVmPass(const MetalVmPass&)=delete;
    MetalVmPass& operator=(const MetalVmPass&)=delete;

    bool init(gfx::Device* device, const Payload& program) {
        shutdown();
        if(!device || device->info().backend!=gfx::Backend::Metal ||
           !device->supportsCompute()) {
            reason_="SpectraFX VM needs Metal compute";
            return false;
        }
        if(program.abiVersion!=kAbiVersion ||
           program.statementCount>kMaxStatements ||
           program.instructionCount>kMaxInstructions) {
            reason_="invalid SpectraFX GPU VM ABI or budgets";
            return false;
        }
        if(!device->canUpdateSharedBuffers()) {
            reason_="create SpectraFX VM outside render/compute passes";
            return false;
        }
        device_=device;
        compute_=device_->createCompute({
            kMetalVmKernel, "", "SpectraFX experimental GPU particle VM"});
        if(!compute_.valid()) {
            reason_="Metal VM compute creation: "+device_->lastError();
            shutdownResources();
            return false;
        }
        auto create=[&](size_t bytes,const void* initial,const char* label) {
            gfx::BufferDesc desc;
            desc.type=gfx::BufferType::Storage;
            desc.usage=gfx::Usage::Immutable;
            desc.size=bytes;
            desc.data=initial;
            desc.label=label;
            return device_->createBuffer(desc);
        };
        code_=create(sizeof(Payload),&program,"SpectraFX VM bytecode");
        std::array<float,8> zeros{};
        params_=create(sizeof(zeros),zeros.data(),"SpectraFX VM params");
        uint32_t zero=0;
        errors_=create(sizeof(zero),&zero,"SpectraFX VM error count");
        if(!code_.valid()||!params_.valid()||!errors_.valid()) {
            reason_="Metal VM buffer creation: "+device_->lastError();
            shutdownResources();
            return false;
        }
        reason_.clear();
        return true;
    }

    // Requires a buffer of at least particleCount * 64 bytes with the
    // ComputeParticles ParticleState layout. This pass never allocates or
    // retains ECS handles or per-source state. Host must ensure uniqueness
    // and that no overlapping encoder or render pass is active.
    bool dispatch(gfx::BufferHandle particleStates, uint32_t particleCount,
                  program::Stage stage, float dt,
                  const std::array<float,8>& values) {
        if(!ready()||!particleStates.valid()) {
            reason_="Metal VM not initialized or state buffer missing";
            return false;
        }
        if(stage!=program::Stage::Spawn && stage!=program::Stage::Update) {
            reason_="Metal VM accepts only Spawn or Update";
            return false;
        }
        if(!program::number(dt)||dt<0.f) {
            reason_="Metal VM timestep must be finite/nonnegative";
            return false;
        }
        for(float v:values)if(!program::number(v)) {
            reason_="Metal VM parameter is nonfinite or out of range";
            return false;
        }
        if(!device_->gpuHealthy()||!device_->canUpdateSharedBuffers()) {
            reason_="Metal VM dispatch requires a healthy frame outside render/compute passes";
            return false;
        }
        if(particleCount==0)return true;
        const uint32_t zero=0;
        device_->updateBuffer(params_,values.data(),sizeof(values));
        device_->updateBuffer(errors_,&zero,sizeof(zero));
        if(!device_->beginCompute(compute_)) {
            reason_="Metal VM compute encoder unavailable";
            return false;
        }
        device_->setComputeBuffer(0,particleStates);
        device_->setComputeBuffer(1,code_);
        device_->setComputeBuffer(2,params_);
        device_->setComputeBuffer(3,errors_);
        DispatchUniforms u{particleCount,uint32_t(stage),dt,0};
        device_->setComputeUniforms(&u,sizeof(u));
        device_->dispatch(particleCount);
        device_->endCompute();
        reason_.clear();
        return true;
    }

    bool ready() const {return device_&&compute_.valid()&&code_.valid()
                                &&params_.valid()&&errors_.valid();}
    const std::string& reason() const {return reason_;}
    // GPU error counter is device-resident; without an explicit RHI async
    // readback contract there is NO promise of successful VM instructions.
    gfx::BufferHandle errorCounter() const {return errors_;}
    void shutdown() {shutdownResources();reason_.clear();}
private:
    void shutdownResources() {
        if(device_) {
            if(compute_.valid())device_->destroyCompute(compute_);
            if(code_.valid())device_->destroyBuffer(code_);
            if(params_.valid())device_->destroyBuffer(params_);
            if(errors_.valid())device_->destroyBuffer(errors_);
        }
        compute_={};code_={};params_={};errors_={};device_=nullptr;
    }
    gfx::Device* device_=nullptr;
    gfx::ComputeHandle compute_{};
    gfx::BufferHandle code_{},params_{},errors_{};
    std::string reason_;
};
} // namespace aiverse::spectra::gpu_program
