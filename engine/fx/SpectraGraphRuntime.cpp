#include "SpectraGraphRuntime.h"
#include <utility>

namespace aiverse::spectra {

GraphGpuEligibility inspectGraphGpuEligibility(
    const fx::EffectDef& effect, const GraphRuntimeOptions& options) {
    GraphGpuEligibility result;
    result.gpu.capacity=options.capacity;
    result.gpu.quality=options.quality;
    result.gpu.backend=options.preference;

    const auto reject=[&](const char* why) {
        result.reason=why;
        return result;
    };
    if(effect.emitters.size()!=1)
        return reject("GPU v1 needs exactly one sprite emitter");
    if(!effect.forceFields.empty())
        return reject("GPU v1 has no effect force fields");

    const fx::EmitterDef& e=effect.emitters[0];
    // Nothing is inferred from a node's name or visual layout. We require a
    // materially identical EffectDef, not just a compatible-looking output.
    if(e.burst<=0||e.burst>64)
        return reject("GPU v1 supports only one-shot bursts of 1..64 particles");
    if(!e.life.constant()||!e.speed.constant()||!e.size.constant()||!e.drag.constant())
        return reject("GPU v1 cannot execute random ranges or curves");
    if(e.texture!="builtin:soft"||e.blend!=fx::FxBlend::Additive||
       e.align!=fx::FxAlign::Camera||e.shape!=fx::FxShape::Point)
        return reject("GPU v1 supports only additive soft camera-facing point sprites");
    if(!e.shaderGraph.isNull() || e.hidden || e.noise.enabled() ||
       e.collision.enabled || e.trail.enabled || e.light.enabled ||
       e.trigger.enabled || !e.subEmitters.empty() || e.force.enabled ||
       e.velocityOverLife.enabled || e.inheritVelocity.enabled ||
       !e.meshes.empty() || !e.sprites.empty())
        return reject("GPU v1 cannot execute custom shaders, modules, trails or sub-effects");
    if(!e.life.curveMin.empty() || !e.speed.curveMin.empty() ||
       !e.size.curveMin.empty() || !e.drag.curveMin.empty())
        return reject("GPU v1 cannot execute MinMaxCurve variants");
    if(e.spread!=0.0f)
        return reject("GPU v1 has no spread/random direction");
    if(e.gravity.x!=0.0f||e.gravity.z!=0.0f)
        return reject("GPU v1 implements only world Y gravity");

    // Compare the COMPLETE normalized effect schema with a fresh emitter
    // containing only fields actually implemented by the GPU kernel. This
    // rejects nested modules, texture animation, custom colors, spawn delay,
    // local-space motion, trails, lights, custom shaders, etc. rather than
    // dropping them silently. We also check all controlling numeric fields
    // before conversion to the GPU config, not just their JSON rendering.
    fx::EmitterDef supported;
    supported.burst=e.burst;
    supported.life=e.life;
    supported.speed=e.speed;
    supported.size=e.size;
    supported.spread=0;
    supported.gravity=e.gravity;
    supported.drag=e.drag;
    fx::EffectDef limited;
    limited.emitters.push_back(std::move(supported));
    if(!(fx::effectToJson(effect)==fx::effectToJson(limited)))
        return reject("GPU v1 cannot preserve every emitter module; use the full CPU effect");

    result.gpu.burst=uint32_t(e.burst);
    result.gpu.lifetime=e.life.min;
    result.gpu.speed=e.speed.min;
    result.gpu.size=e.size.min;
    result.gpu.gravityY=e.gravity.y;
    result.gpu.drag=e.drag.min;
    std::string why;
    if(!validComputeEmitter(result.gpu,&why)) {
        result.reason="GPU v1 configuration: "+why;
        return result;
    }
    result.eligible=true;
    return result;
}

bool GraphRuntime::initGraph(gfx::Device* dev,const Json& graph,
                             const Json& overrides,const GraphRuntimeOptions& options) {
    shutdown();
    fx::EffectDef compiled;
    if(!fx::compileSpectraGraph(graph,overrides,compiled,errors_)) {
        reason_="SpectraFX graph compilation failed";
        return false;
    }
    return initEffect(dev,compiled,options);
}

bool GraphRuntime::initEffect(gfx::Device* dev,const fx::EffectDef& effect,
                              const GraphRuntimeOptions& options) {
    shutdown();
    const GraphGpuEligibility eligibility=inspectGraphGpuEligibility(effect,options);
    const bool requestedGpu=options.preference!=ComputePreference::CpuOnly;
    const bool canMetal=dev &&
        chooseComputeBackend(dev->info().backend,dev->supportsCompute(),
                             options.preference)==ComputeBackend::Metal;
    if(requestedGpu && eligibility.eligible && canMetal &&
       options.quality>=fx::FxQuality::Medium) {
        if(gpu_.init(dev,eligibility.gpu) && gpu_.gpuActive()) {
            useComputeWrapper_=true;
            backend_=ComputeBackend::Metal;
            initialized_=true;
            reason_.clear();
            return true;
        }
        reason_="SpectraFX Metal initialization failed: "+gpu_.reason();
        gpu_.shutdown();
    } else if(requestedGpu && !eligibility.eligible) {
        reason_=eligibility.reason;
    } else if(requestedGpu) {
        reason_="Metal compute unavailable or GPU quality is below Medium";
    } else {
        reason_="CPU explicitly selected";
    }
    if(options.preference==ComputePreference::RequireGpu) {
        if(reason_.empty())reason_="GPU required but not eligible";
        return false;
    }
    // Exact original EffectDef is used here. Do not pass it through
    // ComputeParticles' simplified fallback emitter, which would discard
    // unsupported modules.
    if(!initCpu(dev,effect,options.quality))return false;
    initialized_=true;backend_=ComputeBackend::Cpu;
    return true;
}

bool GraphRuntime::initCpu(gfx::Device* device,const fx::EffectDef& effect,
                           fx::FxQuality quality) {
    cpu_.setQuality(quality);
    if(!cpu_.init(device)) {
        reason_+="; CPU ParticleSystem::init: "+cpu_.lastError();
        return false;
    }
    cpuEffect_=cpu_.define("__spectrafx_graph_instance",effect);
    if(cpuEffect_<0) {
        reason_+="; CPU ParticleSystem rejected compiled effect";
        cpu_.shutdown();
        return false;
    }
    return true;
}

void GraphRuntime::shutdown() {
    gpu_.shutdown();
    cpu_.shutdown();
    cpuEffect_=-1;
    backend_=ComputeBackend::Cpu;
    useComputeWrapper_=false;
    initialized_=false;
    reason_.clear();
    errors_.clear();
}
bool GraphRuntime::burstAt(fx::float3 position,fx::float3 dir,fx::float4 tint) {
    if(!initialized_)return false;
    if(useComputeWrapper_)return gpu_.burstAt(position,dir,tint);
    return cpu_.play(cpuEffect_,position,dir,1.f,tint).valid();
}
void GraphRuntime::setView(const render::RTSCamera& camera,const render::mat4& viewProj,
                            int width,int height) {
    if(!initialized_)return;
    if(useComputeWrapper_)gpu_.setView(camera,viewProj,width,height);
    else cpu_.setView(camera,viewProj,width,height);
}
bool GraphRuntime::step(float dt) {
    if(!initialized_)return false;
    if(!useComputeWrapper_) {cpu_.update(dt);return true;}
    const bool success=gpu_.step(dt);
    // The internal simple-emitter wrapper may itself fall back to CPU after
    // a GPU health error. Retain its ownership and report its actual backend.
    backend_=gpu_.backend();
    return success;
}
void GraphRuntime::draw(const fx::FxFog& fog) {
    if(!initialized_)return;
    if(useComputeWrapper_)gpu_.draw(fog);
    else cpu_.render(fog);
}
void GraphRuntime::clear() {
    if(!initialized_)return;
    if(useComputeWrapper_)gpu_.clear();
    else cpu_.clear();
}

} // namespace aiverse::spectra
