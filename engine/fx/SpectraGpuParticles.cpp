#include "SpectraGpuParticles.h"
#include "SpectraGpuShaders.h"
#include "SpectraGpuFramePlan.h"
#include "SpectraGpuProgram.h"
#include "SpectraGpuMaterial.h"
#include "ParticleShaders.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace aiverse::spectra {
namespace {
bool finite(float x) {return std::isfinite(x);}
bool finite(fx::float3 v) {return finite(v.x)&&finite(v.y)&&finite(v.z);}
bool finite(fx::float4 v) {return finite(v.x)&&finite(v.y)&&finite(v.z)&&finite(v.w);}
gfx::BufferHandle buffer(gfx::Device& d,gfx::BufferType type,size_t size,
                         const void* data,const char* label) {
    gfx::BufferDesc desc;
    desc.type=type;desc.usage=gfx::Usage::Immutable;desc.size=size;
    desc.data=data;desc.label=label;
    return d.createBuffer(desc);
}
uint8_t byte(float x) {
    return uint8_t(std::clamp(int(std::lround(std::clamp(x,0.0f,1.0f)*255.f)),0,255));
}
} // namespace

ComputeBackend chooseComputeBackend(gfx::Backend backend,bool computeAvailable,
                                    ComputePreference preference) {
    if(preference==ComputePreference::CpuOnly)return ComputeBackend::Cpu;
    // Metal is the only backend with a verified RHI compute API at the time
    // of this implementation. Enabling Vulkan requires explicit RHI support.
    return backend==gfx::Backend::Metal&&computeAvailable
        ?ComputeBackend::Metal:ComputeBackend::Cpu;
}
bool validComputeEmitter(const ComputeEmitterDesc& d,std::string* why) {
    const auto fail=[&](const char* msg){if(why)*why=msg;return false;};
    if(d.capacity==0||d.capacity>4096)return fail("GPU emitter capacity must be 1..4096");
    if(d.burst==0||d.burst>64)return fail("GPU emitter burst must be 1..64");
    if(d.lifetime<0.01f||d.lifetime>60.f||!finite(d.lifetime))return fail("invalid lifetime");
    if(d.speed<0.f||d.speed>500.f||!finite(d.speed))return fail("invalid speed");
    if(d.size<=0.f||d.size>100.f||!finite(d.size))return fail("invalid size");
    if(!finite(d.gravityY)||std::abs(d.gravityY)>1000.f)return fail("invalid gravity");
    if(!finite(d.drag)||d.drag<0||d.drag>50.f)return fail("invalid drag");
    if(d.burst>d.capacity)return fail("one burst exceeds available slots");
    if(why)why->clear();
    return true;
}

bool ComputeParticles::init(gfx::Device* dev,const ComputeEmitterDesc& config) {
    shutdown();
    desc_=config;
    if(!validComputeEmitter(config,&reason_))return false;
    programParams_={};
    device_=dev;
    backend_=ComputeBackend::Cpu;
    const bool wantsProgram=!desc_.particleProgram.isNull();
    std::string vmReason;
    bool vmCompatible=true;
    if(wantsProgram) {
        if(!desc_.gpuProgram) {
            vmCompatible=false;
            vmReason="GPU VM is disabled for this source; CPU particle program required";
        } else {
            gpu_program::Payload candidate;
            vmCompatible=gpu_program::prepareForMetal(desc_.particleProgram,
                                                       candidate,vmReason);
        }
    }
    if(!desc_.shaderGraph.isNull() || !desc_.graphInputs.isNull()) {
        if(!desc_.gpuMaterial || desc_.shaderGraph.isNull()) {
            vmCompatible=false;
            vmReason="GPU shaderGraph requires an explicit gpuMaterial opt-in and a supported particle graph";
        } else {
            gpu_material::Material material;
            std::string materialReason;
            if(!gpu_material::compile(desc_.shaderGraph,desc_.graphInputs,
                                      material,materialReason)) {
                vmCompatible=false;
                vmReason=materialReason;
            }
        }
    }
    const bool canGpu=dev&&dev->supportsCompute()&&
                      desc_.quality>=fx::FxQuality::Medium&&vmCompatible;
    const auto selected=dev
        ?chooseComputeBackend(dev->info().backend,canGpu,desc_.backend)
        :ComputeBackend::Cpu;
    if(desc_.backend==ComputePreference::RequireGpu && selected!=ComputeBackend::Metal) {
        reason_=vmReason.empty()
            ?"Metal compute is unavailable; RequireGpu forbids CPU fallback"
            :vmReason+"; RequireGpu forbids CPU fallback";
        device_=nullptr;return false;
    }
    if(selected==ComputeBackend::Metal) {
        if(initGpu()) {
            backend_=ComputeBackend::Metal;initialized_=true;reason_.clear();
            return true;
        }
        const std::string gpuFailure=reason_;
        releaseGpu();
        if(desc_.backend==ComputePreference::RequireGpu) {
            reason_="Metal GPU setup failed: "+gpuFailure;
            device_=nullptr;return false;
        }
        reason_="Metal GPU unavailable, CPU fallback: "+gpuFailure;
    } else if(desc_.backend!=ComputePreference::CpuOnly) {
        reason_=vmReason.empty()?"Metal compute unavailable or low-tier quality; CPU fallback"
                                :vmReason+"; CPU fallback";
    }
    if(!initCpu()) {
        if(reason_.empty())reason_="CPU particle fallback init failed";
        device_=nullptr;return false;
    }
    initialized_=true;return true;
}
bool ComputeParticles::initCpu() {
    cpu_.setQuality(desc_.quality);
    if(!cpu_.init(device_)) {
        reason_+=" CPU init: "+cpu_.lastError();
        return false;
    }
    fx::EffectDef effect;
    fx::EmitterDef emitter;
    emitter.texture="builtin:soft";
    emitter.blend=fx::FxBlend::Additive;
    emitter.burst=int(desc_.burst);
    emitter.life={desc_.lifetime,desc_.lifetime};
    emitter.speed={desc_.speed,desc_.speed};
    emitter.size={desc_.size,desc_.size};
    emitter.gravity={0,desc_.gravityY,0};
    emitter.drag={desc_.drag,desc_.drag};
    emitter.spread=0;
    emitter.color={{0.f,{1,1,1,1}},{1.f,{1,1,1,0}}};
    if(!desc_.particleProgram.isNull())emitter.particleProgram=desc_.particleProgram;
    if(!desc_.shaderGraph.isNull())emitter.shaderGraph=desc_.shaderGraph;
    if(!desc_.graphInputs.isNull())emitter.graphInputs=desc_.graphInputs;
    effect.emitters.push_back(std::move(emitter));
    cpuEffect_=cpu_.define("__spectrafx_compute_cpu_fallback",effect);
    return cpuEffect_>=0;
}
bool ComputeParticles::initGpu() {
    readbackLifetime_=std::make_shared<uint64_t>(0);
    vmFrameRevision_=0;
    checkpointRevision_=0;
    lastCheckpoint_={};
    if(!device_) {reason_="no RHI Device";return false;}
    const uint32_t cap=desc_.capacity;
    gpu_program::Payload vmPayload;
    if(!gpu_program::prepareForMetal(desc_.particleProgram,vmPayload,reason_))
        return false;
    gpu_material::Material graphMaterial;
    const bool materialActive=desc_.gpuMaterial&&!desc_.shaderGraph.isNull();
    if(materialActive && !gpu_material::compile(
        desc_.shaderGraph,desc_.graphInputs,graphMaterial,reason_))return false;
    const std::array<uint32_t,4> graphChannels=graphMaterial.sources;
    gpuIndirectActive_=desc_.gpuIndirect && device_->supportsGpuAuthoredIndirectDraw();
    compute_=device_->createCompute({shaders::kMetal,shaders::kGLSL,"SpectraFX Metal particle simulation"});
    if(!compute_.valid()) {reason_=device_->lastError();return false;}

    std::vector<ParticleState> reset(cap);
    for(auto& s:reset) {s.positionAge={0,0,0,-1};s.velocityLife={0,0,0,1};}
    states_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(ParticleState),
                   reset.data(),"SpectraFX GPU particle states");
    seeds_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(ParticleState),
                  nullptr,"SpectraFX GPU particle spawn queue");
    slots_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(uint32_t),
                  nullptr,"SpectraFX GPU spawn slots");
    programStateCache_.assign(cap,ProgramParams{});
    seedProgramParams_=buffer(*device_,gfx::BufferType::Storage,
                               cap*sizeof(ProgramParams),nullptr,
                               "SpectraFX GPU VM spawn parameters");
    stateProgramParams_=buffer(*device_,gfx::BufferType::Storage,
                                cap*sizeof(ProgramParams),programStateCache_.data(),
                                "SpectraFX GPU VM live parameters");
    gpuVmPayload_=buffer(*device_,gfx::BufferType::Storage,sizeof(vmPayload),
                         &vmPayload,"SpectraFX GPU VM validated program");
    std::vector<ProgramAttributes> initialAttributes(cap);
    stateAttributes_=buffer(*device_,gfx::BufferType::Storage,
                            cap*sizeof(ProgramAttributes),
                            initialAttributes.data(),
                            "SpectraFX GPU VM particle custom attributes");
    liveSlots_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(uint32_t),
                      nullptr,"SpectraFX GPU live-slot gather map");
    graphSources_=buffer(*device_,gfx::BufferType::Storage,sizeof(graphChannels),
                         graphChannels.data(),"SpectraFX GPU material vertex sources");
    const uint32_t zero=0;
    const gfx::DrawIndexedIndirectCommand zeroArgs{0,1,0,0,0};
    aliveCounter_=buffer(*device_,gfx::BufferType::Storage,sizeof(zero),
                         &zero,"SpectraFX GPU atomic alive count");
    vmErrors_=buffer(*device_,gfx::BufferType::Storage,sizeof(zero),
                     &zero,"SpectraFX GPU VM error counter");
    gpuDrawArgs_=buffer(*device_,gfx::BufferType::Storage,sizeof(zeroArgs),
                        &zeroArgs,"SpectraFX GPU authored draw args");
    vertices_=buffer(*device_,gfx::BufferType::Storage,
                     cap*4*sizeof(Vertex),nullptr,"SpectraFX GPU quad vertices");

    std::vector<uint16_t> idx(cap*6);
    for(uint32_t n=0;n<cap;++n) {
        const uint16_t i=uint16_t(n*4);
        const size_t at=size_t(n)*6;
        idx[at]=i;idx[at+1]=uint16_t(i+1);idx[at+2]=uint16_t(i+2);
        idx[at+3]=i;idx[at+4]=uint16_t(i+2);idx[at+5]=uint16_t(i+3);
    }
    indices_=buffer(*device_,gfx::BufferType::Index,idx.size()*sizeof(uint16_t),
                    idx.data(),"SpectraFX GPU billboard indices");

    // One soft-sprite texture shared across all GPU particles in this first
    // backend. Texture atlas/mesh/ribbons remain on the existing CPU backend.
    constexpr int n=32;
    std::array<uint8_t,n*n*4> pixels{};
    for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
        const float dx=(float(x)+0.5f-float(n)*0.5f)/(float(n)*0.5f);
        const float dy=(float(y)+0.5f-float(n)*0.5f)/(float(n)*0.5f);
        const float dist=std::sqrt(dx*dx+dy*dy);
        const float alpha=std::clamp((1.f-dist)*2.f,0.f,1.f);
        const size_t at=size_t(y*n+x)*4;
        pixels[at]=pixels[at+1]=pixels[at+2]=255;
        pixels[at+3]=byte(alpha);
    }
    gfx::TextureDesc texture;
    texture.width=n;texture.height=n;texture.data=pixels.data();
    texture.filter=gfx::Filter::Linear;texture.label="SpectraFX GPU soft sprite";
    texture_=device_->createTexture(texture);

    gfx::ShaderDesc shader;
    // Shader source storage must outlive createShader() on all RHI backends.
    gpu_material::Shaders materialShaders;
    if(materialActive)materialShaders=gpu_material::makeShaders(graphMaterial);
    shader.glslVertex=materialActive?materialShaders.glslVertex.c_str():fx::shaders::kParticleVS;
    shader.glslFragment=materialActive?materialShaders.glslFragment.c_str():fx::shaders::kParticleFS;
    shader.msl=materialActive?materialShaders.msl.c_str():fx::shaders::kParticleMSL;
    shader.textureSlots=1;shader.uniformSlots=1;
    shader.label=materialActive?"SpectraFX GPU particle material":"SpectraFX GPU billboard";
    vertexShader_=device_->createShader(shader);
    if(vertexShader_.valid()) {
        gfx::PipelineDesc pd;
        pd.shader=vertexShader_;
        pd.attrs[0]={0,gfx::VertexFormat::Float3,0};
        pd.attrs[1]={1,gfx::VertexFormat::Float2,12};
        pd.attrs[2]={2,gfx::VertexFormat::UByte4N,20};
        if(materialActive)pd.attrs[3]={3,gfx::VertexFormat::Float4,24};
        pd.attrCount=materialActive?4:3;pd.stride=sizeof(Vertex);
        pd.indexType=gfx::IndexType::UInt16;
        pd.cull=gfx::CullMode::None;pd.blend=gfx::BlendMode::Additive;
        pd.depthTest=true;pd.depthWrite=false;
        pd.label="SpectraFX GPU additive";
        pipeline_=device_->createPipeline(pd);
    }
    if(!states_.valid()||!seeds_.valid()||!slots_.valid()||!liveSlots_.valid()||
       !graphSources_.valid()||
       !aliveCounter_.valid()||!gpuDrawArgs_.valid()||!vmErrors_.valid()||
       !seedProgramParams_.valid()||!stateProgramParams_.valid()||
       !gpuVmPayload_.valid()||!stateAttributes_.valid()||!vertices_.valid()||
       !indices_.valid()||!texture_.valid()||!vertexShader_.valid()||!pipeline_.valid()) {
        reason_="GPU resource/pipeline allocation failed: "+device_->lastError();
        return false;
    }
    ages_.assign(cap,-1.f);
    freeSlots_.clear();freeSlots_.reserve(cap);
    for(uint32_t i=cap;i>0;--i)freeSlots_.push_back(i-1);
    pendingStates_.reserve(cap);
    pendingSlots_.reserve(cap);
    pendingProgramParams_.reserve(cap);
    aliveSlots_.reserve(cap);
    stats_.gpuBytes=size_t(cap)*(sizeof(ParticleState)*2+2*sizeof(uint32_t)+4*sizeof(Vertex)+6*sizeof(uint16_t))
                    +size_t(n*n*4)+2*sizeof(zero)+sizeof(zeroArgs)
                    +size_t(cap)*2*sizeof(ProgramParams)+sizeof(vmPayload)
                    +size_t(cap)*sizeof(ProgramAttributes)+sizeof(graphChannels);
    return true;
}
void ComputeParticles::releaseGpu() {
    readbackLifetime_.reset();
    asyncErrorPending_=false;
    if(device_) {
        if(pipeline_.valid())device_->destroyPipeline(pipeline_);
        if(vertexShader_.valid())device_->destroyShader(vertexShader_);
        if(compute_.valid())device_->destroyCompute(compute_);
        if(states_.valid())device_->destroyBuffer(states_);
        if(seeds_.valid())device_->destroyBuffer(seeds_);
        if(slots_.valid())device_->destroyBuffer(slots_);
        if(liveSlots_.valid())device_->destroyBuffer(liveSlots_);
        if(graphSources_.valid())device_->destroyBuffer(graphSources_);
        if(aliveCounter_.valid())device_->destroyBuffer(aliveCounter_);
        if(gpuDrawArgs_.valid())device_->destroyBuffer(gpuDrawArgs_);
        if(vmErrors_.valid())device_->destroyBuffer(vmErrors_);
        if(seedProgramParams_.valid())device_->destroyBuffer(seedProgramParams_);
        if(stateProgramParams_.valid())device_->destroyBuffer(stateProgramParams_);
        if(gpuVmPayload_.valid())device_->destroyBuffer(gpuVmPayload_);
        if(stateAttributes_.valid())device_->destroyBuffer(stateAttributes_);
        if(vertices_.valid())device_->destroyBuffer(vertices_);
        if(indices_.valid())device_->destroyBuffer(indices_);
        if(texture_.valid())device_->destroyTexture(texture_);
    }
    pipeline_={};vertexShader_={};compute_={};states_={};seeds_={};slots_={};
    vertices_={};indices_={};texture_={};liveSlots_={};
    aliveCounter_={};gpuDrawArgs_={};vmErrors_={};gpuIndirectActive_=false;
    seedProgramParams_={};stateProgramParams_={};gpuVmPayload_={};
    stateAttributes_={};graphSources_={};
    ages_.clear();freeSlots_.clear();pendingStates_.clear();pendingSlots_.clear();
    pendingProgramParams_.clear();programStateCache_.clear();cpuHandles_.clear();
    programParamsDirty_=false;
    aliveSlots_.clear();
    drawCount_=0;
}
void ComputeParticles::shutdown() {
    cpu_.shutdown();cpuEffect_=-1;releaseGpu();device_=nullptr;
    initialized_=false;viewReady_=false;verticesReady_=false;
    drawCount_=0;
    programParams_={};
    lastCheckpoint_={};checkpointRevision_=0;
    backend_=ComputeBackend::Cpu;stats_={};reason_.clear();
}
void ComputeParticles::clear() {
    if(!initialized_)return;
    if(backend_==ComputeBackend::Cpu){
        cpu_.clear();cpuHandles_.clear();stats_.alive=0;return;
    }
    // Storage updates are encoded as blits. Issuing one inside an active
    // render/compute encoder is silently rejected by the Metal RHI, so never
    // reset CPU slot bookkeeping unless the GPU buffer can actually be reset.
    if(!device_->canUpdateSharedBuffers()) {
        reason_="SpectraFX GPU clear must run outside render/compute passes";
        return;
    }
    pendingStates_.clear();pendingSlots_.clear();pendingProgramParams_.clear();
    aliveSlots_.clear();verticesReady_=false;
    const uint32_t cap=desc_.capacity;
    std::vector<ParticleState> empty(cap);
    for(auto& s:empty){s.positionAge={0,0,0,-1};s.velocityLife={0,0,0,1};}
    device_->updateBuffer(states_,empty.data(),empty.size()*sizeof(ParticleState));
    programStateCache_.assign(cap,ProgramParams{});
    device_->updateBuffer(stateProgramParams_,programStateCache_.data(),
                          programStateCache_.size()*sizeof(ProgramParams));
    const std::vector<ProgramAttributes> emptyAttributes(cap);
    device_->updateBuffer(stateAttributes_,emptyAttributes.data(),
                          emptyAttributes.size()*sizeof(ProgramAttributes));
    programParamsDirty_=false;
    ages_.assign(cap,-1.f);
    freeSlots_.clear();
    for(uint32_t i=cap;i>0;--i)freeSlots_.push_back(i-1);
    stats_.alive=0;
    stats_.gpuVmErrorsKnown=false;
    drawCount_=0;stats_.submittedSlots=0;
}
bool ComputeParticles::setProgramParameter(uint32_t slot,float value,
                                            bool affectExisting) {
    if(!initialized_ || slot>=8 || !spectra::program::number(value) ||
       desc_.particleProgram.isNull())return false;
    programParams_[slot]=value;
    if(backend_==ComputeBackend::Cpu) {
        if(affectExisting)
            for(const fx::FxHandle h:cpuHandles_)
                (void)cpu_.setProgramParameter(h,slot,value,true);
        return true;
    }
    if(affectExisting) {
        const auto write=[&](ProgramParams& params) {
            render::float4& p=slot<4?params.p0:params.p1;
            switch(slot%4) {
            case 0:p.x=value;break;
            case 1:p.y=value;break;
            case 2:p.z=value;break;
            default:p.w=value;break;
            }
        };
        for(size_t i=0;i<ages_.size();++i)
            if(ages_[i]>0.f)write(programStateCache_[i]);
        for(ProgramParams& seed:pendingProgramParams_)write(seed);
        programParamsDirty_=true; // uploaded outside active render passes
    }
    return true;
}
bool ComputeParticles::burstAt(fx::float3 position,fx::float3 direction,fx::float4 tint) {
    if(!initialized_||desc_.quality==fx::FxQuality::Off||!finite(position)||!finite(direction)||!finite(tint))return false;
    const float length=render::length(direction);
    const fx::float3 normal=length>1e-5f?direction*(1.0f/length):fx::float3{0,1,0};
    if(backend_==ComputeBackend::Cpu) {
        const fx::FxHandle h=cpu_.playWithProgramParameters(
            cpuEffect_,position,normal,1.f,tint,programParams_);
        if(!h.valid()) {stats_.dropped+=desc_.burst;return false;}
        if(cpuHandles_.size()>=65536) {
            cpu_.kill(cpuHandles_.front());
            cpuHandles_.erase(cpuHandles_.begin());
        }
        cpuHandles_.push_back(h);
        return true;
    }
    if(freeSlots_.size()<desc_.burst) {
        stats_.dropped+=desc_.burst;return false;
    }
    for(uint32_t n=0;n<desc_.burst;++n) {
        const uint32_t slot=freeSlots_.back();freeSlots_.pop_back();
        ages_[slot]=desc_.lifetime;
        ParticleState particle{};
        particle.positionAge={position.x,position.y,position.z,0};
        particle.velocityLife={normal.x*desc_.speed,normal.y*desc_.speed,normal.z*desc_.speed,desc_.lifetime};
        particle.tint={tint.x,tint.y,tint.z,tint.w};
        particle.size={desc_.size,0,0,0};
        pendingStates_.push_back(particle);pendingSlots_.push_back(slot);
        ProgramParams parameters{};
        parameters.p0={programParams_[0],programParams_[1],
                       programParams_[2],programParams_[3]};
        parameters.p1={programParams_[4],programParams_[5],
                       programParams_[6],programParams_[7]};
        pendingProgramParams_.push_back(parameters);
        programStateCache_[slot]=parameters;
    }
    stats_.alive=desc_.capacity-uint32_t(freeSlots_.size());
    return true;
}
void ComputeParticles::setView(const render::RTSCamera& camera,const render::mat4& viewProjection,int w,int h) {
    viewProj_=viewProjection;
    eye_=camera.eye();
    const fx::float3 forward=render::normalize(camera.target-eye_);
    fx::float3 side=render::cross(forward,fx::float3{0,1,0});
    if(render::length(side)<1e-5f)side={1,0,0};
    right_=render::normalize(side);
    up_=render::normalize(render::cross(right_,forward));
    viewReady_=true;
    if(backend_==ComputeBackend::Cpu)cpu_.setView(camera,viewProjection,w,h);
}
bool ComputeParticles::step(float dt) {
    if(!initialized_||!finite(dt)||dt<0)return false;
    dt=std::clamp(dt,0.f,0.1f);
    if(backend_==ComputeBackend::Cpu) {
        cpu_.update(dt);
        stats_.alive=uint32_t(cpu_.stats().particles);
        return true;
    }
    if(!device_->gpuHealthy()) {
        if(desc_.backend==ComputePreference::RequireGpu) {
            // Never claim a CPU backend when the caller explicitly required GPU.
            initialized_=false;verticesReady_=false;
            reason_="Metal GPU unhealthy; RequireGpu forbids CPU fallback";
            return false;
        }
        switchToCpu("Metal GPU reported unhealthy");
        // Any GPU-resident particles were lost: never report a seamless step.
        return false;
    }
    if(!device_->canUpdateSharedBuffers()) {
        reason_="SpectraFX GPU step must be before the render pass";
        return false;
    }
    const uint32_t count=uint32_t(pendingStates_.size());
    stats_.gpuVmErrorsKnown=false;
    // Keep the physical state pool stable, but gather all surviving physical
    // slots into contiguous GPU vertices for a dense indexed draw. The CPU
    // knows lifetimes and slot ownership; it never reads GPU positions.
    uint32_t updatePrefix=0;
    uint32_t compactCount=0; // only a CPU estimate in GPU indirect mode
    aliveSlots_.clear();
    for(uint32_t i=0;i<uint32_t(ages_.size());++i) {
        if(ages_[i]<=0.f)continue;
        updatePrefix=i+1; // integrate every physical slot alive before dt
        if(ages_[i]>dt) {
            ++compactCount;
            // Preserve the existing CPU-authored gather as the fallback.
            if(!gpuIndirectActive_)aliveSlots_.push_back(i);
        }
    }
    if(updatePrefix==0 && count==0) {
        drawCount_=0;
        stats_.submittedSlots=stats_.alive=0;
        verticesReady_=true;
        reason_.clear();
        return true; // no live GPU particles, no compute work or draw
    }
    const uint32_t resetVmErrors=0;
    device_->updateBuffer(vmErrors_,&resetVmErrors,sizeof(resetVmErrors));
    if(programParamsDirty_) {
        device_->updateBuffer(stateProgramParams_,programStateCache_.data(),
                              programStateCache_.size()*sizeof(ProgramParams));
        stats_.uploadedBytes+=programStateCache_.size()*sizeof(ProgramParams);
        programParamsDirty_=false;
    }
    if(count>0) {
        device_->updateBuffer(seeds_,pendingStates_.data(),size_t(count)*sizeof(ParticleState));
        device_->updateBuffer(slots_,pendingSlots_.data(),size_t(count)*sizeof(uint32_t));
        device_->updateBuffer(seedProgramParams_,pendingProgramParams_.data(),
                              size_t(count)*sizeof(ProgramParams));
        stats_.uploadedBytes+=size_t(count)*(sizeof(ParticleState)+
                               sizeof(uint32_t)+sizeof(ProgramParams));
    }
    if(compactCount>0 && !gpuIndirectActive_) {
        device_->updateBuffer(liveSlots_,aliveSlots_.data(),size_t(compactCount)*sizeof(uint32_t));
        stats_.uploadedBytes+=size_t(compactCount)*sizeof(uint32_t);
    }
    if(!device_->beginCompute(compute_)) {
        reason_="Metal compute encoder unavailable: call step between beginFrame/endFrame, before rendering";
        return false; // pending births, age/free lists and draw count remain unchanged
    }
    device_->setComputeBuffer(0,states_);
    device_->setComputeBuffer(1,seeds_);
    device_->setComputeBuffer(2,slots_);
    device_->setComputeBuffer(3,vertices_);
    device_->setComputeBuffer(4,liveSlots_);
    device_->setComputeBuffer(5,aliveCounter_);
    device_->setComputeBuffer(6,gpuDrawArgs_);
    device_->setComputeBuffer(7,seedProgramParams_);
    device_->setComputeBuffer(9,stateProgramParams_);
    device_->setComputeBuffer(10,gpuVmPayload_);
    device_->setComputeBuffer(11,stateAttributes_);
    device_->setComputeBuffer(12,vmErrors_);
    device_->setComputeBuffer(13,graphSources_);
    Uniforms u{};
    u.gravityDt={desc_.gravityY,dt,desc_.drag,0};
    u.rightSize={right_.x,right_.y,right_.z,desc_.size};
    u.up={up_.x,up_.y,up_.z,0};
    u.meta={0,desc_.capacity,count,compactCount};
    // Canonical phase order: Spawn VM runs inside mode 0, Update VM
    // inside mode 1, and vertex publication follows integration. No
    // separate Stage-23 VM pass must run on these same physical particles.
    const auto frame=gpu_pipeline::plan(desc_.capacity,count,updatePrefix,
                                        compactCount,gpuIndirectActive_);
    for(uint32_t i=0;i<frame.count;++i) {
        const auto& phase=frame.dispatches[i];
        u.meta[0]=phase.shaderMode;
        device_->setComputeUniforms(&u,sizeof(u));
        device_->dispatch(phase.threads);
        ++stats_.gpuDispatches;
        if(phase.phase==gpu_pipeline::Phase::Compact)
            ++stats_.gpuCompactDispatches;
    }
    device_->endCompute();
    pendingStates_.clear();pendingSlots_.clear();pendingProgramParams_.clear();
    // Commit CPU lifespan/free-slot changes only after GPU dispatch submission.
    for(uint32_t i=0;i<desc_.capacity;++i) {
        if(ages_[i]<=0.f)continue;
        ages_[i]-=dt;
        if(ages_[i]<=0.f){ages_[i]=-1.f;freeSlots_.push_back(i);}
    }
    stats_.alive=compactCount;
    drawCount_=compactCount;
    stats_.submittedSlots=drawCount_;
    verticesReady_=true;reason_.clear();
    return true;
}
void ComputeParticles::draw(const fx::FxFog& fog) {
    stats_.drawCalls=0;
    if(!initialized_||!viewReady_||desc_.quality==fx::FxQuality::Off)return;
    if(backend_==ComputeBackend::Cpu) {
        cpu_.render(fog);stats_.drawCalls=uint32_t(cpu_.stats().drawCalls);return;
    }
    if(!verticesReady_||!pipeline_.valid()||drawCount_==0)return;
    DrawUniforms u{};
    std::memcpy(u.viewProj,viewProj_.m,sizeof(u.viewProj));
    u.fog[0]=fog.color.x;u.fog[1]=fog.color.y;u.fog[2]=fog.color.z;u.fog[3]=fog.density;
    u.camera[0]=eye_.x;u.camera[1]=eye_.y;u.camera[2]=eye_.z;u.camera[3]=1;
    u.params[0]=1;u.params[1]=0;u.params[2]=0;u.params[3]=0;
    device_->setPipeline(pipeline_);
    device_->setVertexBuffer(vertices_);
    device_->setIndexBuffer(indices_);
    device_->setUniforms(0,&u,sizeof(u));
    device_->setTexture(0,texture_);
    if(gpuIndirectActive_) {
        // No CPU readback, and no unsafe direct-draw fallback when GPU
        // authored arguments cannot be submitted. CPU's count is only an
        // estimate and may not match the actual atomic compacted count.
        if(!device_->drawIndexedGpuIndirect(gpuDrawArgs_,0)) {
            reason_="SpectraFX GPU-authored indirect draw was rejected by RHI";
            return;
        }
        ++stats_.gpuIndirectDraws;
    } else {
        device_->drawIndexed(drawCount_*6);
    }
    stats_.drawCalls=1;
}
bool ComputeParticles::readGpuVmErrors(uint32_t& count) {
    stats_.gpuVmErrorsKnown=false;
    if(!initialized_ || backend_!=ComputeBackend::Metal || !device_ ||
       !vmErrors_.valid() || !device_->gpuHealthy() ||
       !device_->canUpdateSharedBuffers())return false;
    uint32_t observed=0;
    if(!device_->readBuffer(vmErrors_,&observed,sizeof(observed)))return false;
    count=observed;
    stats_.gpuVmErrorsLastRead=observed;
    stats_.gpuVmErrorsKnown=true;
    return true;
}
bool ComputeParticles::requestGpuVmErrors(
    std::function<void(bool,uint32_t)> callback) {
    if(!callback || !initialized_ || backend_!=ComputeBackend::Metal ||
       !device_ || !vmErrors_.valid() || !device_->gpuHealthy() ||
       !device_->canUpdateSharedBuffers() || asyncErrorPending_ ||
       !readbackLifetime_)return false;
    const uint64_t revision=vmFrameRevision_;
    std::weak_ptr<uint64_t> lifetime(readbackLifetime_);
    asyncErrorPending_=true;
    if(!device_->readBufferAsync(vmErrors_,sizeof(uint32_t),
         [this,lifetime,revision,callback=std::move(callback)](
             std::vector<uint8_t> bytes) mutable {
             if(!lifetime.lock())return; // shutdown/device loss
             asyncErrorPending_=false;
             uint32_t errors=0;
             const bool ok=bytes.size()==sizeof(errors);
             if(ok)std::memcpy(&errors,bytes.data(),sizeof(errors));
             // Result describes the requested frame, not a later frame.
             if(ok && revision==vmFrameRevision_ && gpuActive()) {
                 stats_.gpuVmErrorsLastRead=errors;
                 stats_.gpuVmErrorsKnown=true;
             }
             callback(ok,errors);
         })) {
        asyncErrorPending_=false;
        return false;
    }
    return true;
}

bool ComputeParticles::captureGpuCheckpoint(GpuCheckpoint& out) {
    out={};
    if(!initialized_ || backend_!=ComputeBackend::Metal || !device_ ||
       !device_->gpuHealthy() || !device_->canUpdateSharedBuffers() ||
       !states_.valid() || !stateProgramParams_.valid() ||
       !stateAttributes_.valid() || ages_.empty() ||
       !pendingStates_.empty())return false;
    const size_t n=ages_.size();
    // A checkpoint must include all physical slots, not merely those that
    // survived the previous draw or the CPU-estimated indirect draw count.
    std::vector<ParticleState> state(n);
    std::vector<ProgramParams> params(n);
    std::vector<ProgramAttributes> attributes(n);
    if(!device_->readBuffer(states_,state.data(),n*sizeof(ParticleState)) ||
       !device_->readBuffer(stateProgramParams_,params.data(),
                            n*sizeof(ProgramParams)) ||
       !device_->readBuffer(stateAttributes_,attributes.data(),
                            n*sizeof(ProgramAttributes)))return false;
    GpuCheckpoint next;
    next.slots.resize(n);
    next.remainingLifetime=ages_;
    next.frameSerial=device_->frameSerial();
    for(size_t i=0;i<n;++i) {
        auto& d=next.slots[i];
        d.positionAge=state[i].positionAge;
        d.velocityLife=state[i].velocityLife;
        d.tint=state[i].tint;
        d.size=state[i].size;
        std::memcpy(d.programParams.data(),&params[i],sizeof(ProgramParams));
        std::memcpy(d.attributes.data(),&attributes[i],sizeof(ProgramAttributes));
    }
    next.complete=true;
    out=next;
    lastCheckpoint_=std::move(next);
    checkpointRevision_=vmFrameRevision_;
    return true;
}

void ComputeParticles::switchToCpu(const std::string& why) {
    ++stats_.gpuFallbacks;
    stats_.gpuVmErrorsKnown=false;
    // Restore only a complete checkpoint captured after the LAST submitted
    // GPU frame, with no pending unsent births. A stale checkpoint must never
    // resurrect particles that have died or silently omit recent births.
    const bool canRestore=lastCheckpoint_.complete &&
        checkpointRevision_==vmFrameRevision_ && pendingStates_.empty() &&
        lastCheckpoint_.slots.size()==ages_.size() &&
        lastCheckpoint_.remainingLifetime.size()==ages_.size();
    std::vector<fx::FxImportedParticle> restored;
    if(canRestore) {
        restored.reserve(lastCheckpoint_.slots.size());
        for(size_t i=0;i<lastCheckpoint_.slots.size();++i) {
            const auto& src=lastCheckpoint_.slots[i];
            if(src.positionAge.w<0.f || lastCheckpoint_.remainingLifetime[i]<=0.f)
                continue;
            fx::FxImportedParticle p;
            p.pos={src.positionAge.x,src.positionAge.y,src.positionAge.z};
            p.vel={src.velocityLife.x,src.velocityLife.y,src.velocityLife.z};
            p.age=src.positionAge.w;
            p.lifetime=src.velocityLife.w;
            p.size=src.size.x;
            p.tint={src.tint.x,src.tint.y,src.tint.z,src.tint.w};
            p.programParams=src.programParams;
            p.customAttributes=src.attributes;
            restored.push_back(p);
        }
    }
    const uint32_t owned=uint32_t(ages_.size()-freeSlots_.size());
    releaseGpu();backend_=ComputeBackend::Cpu;
    const bool ok=initCpu();
    bool imported=false;
    if(ok && canRestore)
        imported=cpu_.importLiveParticles(cpuEffect_,restored);
    const uint32_t recovered=imported?uint32_t(restored.size()):0u;
    stats_.recoveredOnFallback+=recovered;
    stats_.lostOnFallback+=owned>=recovered?owned-recovered:0u;
    reason_=why+(ok
        ?(imported?"; restored particles from last complete GPU checkpoint"
                  :"; CPU fallback active; latest GPU state unavailable/incompatible")
        :"; CPU fallback init failed");
    initialized_=ok;stats_.alive=recovered;stats_.gpuBytes=0;
    stats_.submittedSlots=0;
    lastCheckpoint_={};checkpointRevision_=0;
}

} // namespace aiverse::spectra
