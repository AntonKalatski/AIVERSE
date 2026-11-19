#include "SpectraGpuPoolWorld.h"
#include "SpectraParticleProgram.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace aiverse::spectra {
namespace {
bool valid(float v) {return program::number(v);}
bool valid(fx::float3 v) {return valid(v.x)&&valid(v.y)&&valid(v.z);}
bool valid(fx::float4 v) {return valid(v.x)&&valid(v.y)&&valid(v.z)&&valid(v.w);}
bool validParams(const std::array<float,8>& params) {
    for(float value:params)if(!valid(value))return false;
    return true;
}
} // namespace

bool GpuPoolWorld::init(gfx::Device* device, PoolWorldBudgets budgets) {
    shutdown();
    budgets.maxGpuGroups=std::min<uint32_t>(budgets.maxGpuGroups,64);
    budgets.maxGroupCapacity=std::clamp<uint32_t>(budgets.maxGroupCapacity,1,4096);
    budgets.maxBurstsPerFrame=std::min<uint32_t>(budgets.maxBurstsPerFrame,4096);
    budgets_=budgets;
    device_=device;
    if(!cpu_.init(device_,budgets_.quality)) {
        reason_="SpectraFX pooled CPU world init failed: "+cpu_.error();
        device_=nullptr;
        return false;
    }
    checkpointBudget_=budgets_.sharedCheckpointBudget
        ?budgets_.sharedCheckpointBudget
        :std::make_shared<CheckpointReadbackBudget>(budgets_.checkpointReadback);
    initialized_=true;
    reason_.clear();
    return true;
}

void GpuPoolWorld::shutdown() {
    // Destroy GPU resources before the caller's graphics Device.
    gpuGroups_.clear();
    bindings_.clear();
    checkpointBudget_.reset();
    cpu_.shutdown();
    device_=nullptr;
    initialized_=frameOpen_=stepped_=drawn_=false;
    stats_={};
    reason_.clear();
}

std::string GpuPoolWorld::groupKey(const fx::EffectDef& effect,
                                  const GraphRuntimeOptions& options,
                                  uint32_t capacity) {
    const std::string serialized=fx::effectToJson(effect).dump(-1);
    // Length prefix prevents ambiguous keys if authored shader graphs contain
    // textual substrings that resemble the policy suffix.
    return std::to_string(serialized.size())+":"+serialized+
        "|capacity="+std::to_string(capacity)+
        "|quality="+std::to_string(unsigned(options.quality))+
        "|vm="+std::to_string(unsigned(options.gpuProgram))+
        "|material="+std::to_string(unsigned(options.gpuMaterial))+
        "|indirect="+std::to_string(unsigned(options.gpuIndirect))+
        "|checkpointCadence="+std::to_string(options.checkpointEveryFrames);
}

void GpuPoolWorld::recountBindings() {
    stats_.gpuGroups=uint32_t(gpuGroups_.size());
    std::set<int> distinctCpu;
    for(const auto& [name,binding]:bindings_) {
        (void)name;
        if(!binding.gpu && binding.cpuId>=0)distinctCpu.insert(binding.cpuId);
    }
    stats_.cpuDefinitions=uint32_t(distinctCpu.size());
}

bool GpuPoolWorld::defineGraph(const std::string& name,const Json& graph,
                              const Json& overrides,
                              const GraphRuntimeOptions& options) {
    if(!initialized_ || frameOpen_) {
        reason_="define pooled SpectraFX graph outside presentation frames";
        return false;
    }
    InstanceParameterTable named;
    std::vector<std::string> errors;
    if(graph.getInt("version")==2 &&
       !loadInstanceParameterTable(graph["instanceParameters"],named,errors)) {
        reason_=errors.empty()?"invalid GPU pool instanceParameters":errors.front();
        return false;
    }
    fx::EffectDef definition;
    if(!fx::compileSpectraGraph(graph,overrides,definition,errors)) {
        reason_=errors.empty()?"SpectraFX graph compilation failed":errors.front();
        return false;
    }
    return defineCompiled(name,definition,options,named);
}

bool GpuPoolWorld::defineEffect(const std::string& name,
                               const fx::EffectDef& effect,
                               const GraphRuntimeOptions& options) {
    if(!initialized_ || frameOpen_) {
        reason_="define pooled SpectraFX effect outside presentation frames";
        return false;
    }
    return defineCompiled(name,effect,options,{});
}

bool GpuPoolWorld::defineCompiled(const std::string& name,
                                 const fx::EffectDef& effect,
                                 const GraphRuntimeOptions& options,
                                 const InstanceParameterTable& named) {
    if(name.empty()||name.size()>128) {
        reason_="SpectraFX pool effect name must be 1..128 bytes";
        return false;
    }
    GraphRuntimeOptions actual=options;
    // Never exceed the global per-group memory/quality budget even when a
    // caller requests a larger emitter allocation or higher quality tier.
    actual.capacity=std::min(options.capacity,budgets_.maxGroupCapacity);
    actual.quality=std::min(options.quality,budgets_.quality);
    // A pooled Device must use ONE limiter for all its GPU groups, including
    // groups with different checkpoint cadences.
    actual.checkpointBudget=checkpointBudget_;
    const GraphGpuEligibility eligible=inspectGraphGpuEligibility(effect,actual);
    const bool wantsGpu=options.preference!=ComputePreference::CpuOnly;
    const bool metalAvailable=device_&&device_->supportsCompute()&&
        device_->info().backend==gfx::Backend::Metal &&
        actual.quality>=fx::FxQuality::Medium;
    Binding next;
    for(const auto& [paramName,definition]:named) {
        (void)paramName;
        next.defaults[definition.slot]=definition.initial;
    }
    next.named=named;
    next.usesRuntimeParams=effect.emitters.size()==1 &&
        (!effect.emitters[0].particleProgram.isNull() ||
         !effect.emitters[0].shaderGraph.isNull());

    if(wantsGpu && eligible.eligible && metalAvailable) {
        const std::string key=groupKey(effect,actual,actual.capacity);
        for(size_t i=0;i<gpuGroups_.size();++i)
            if(gpuGroups_[i].key==key) {
                next.gpu=true;next.gpuIndex=i;
                bindings_[name]=std::move(next);
                recountBindings();
                reason_.clear();
                return true; // share GPU storage, shader and draw for this effect
            }
        if(gpuGroups_.size()<budgets_.maxGpuGroups) {
            auto particles=std::make_unique<ComputeParticles>();
            ComputeEmitterDesc desc=eligible.gpu;
            desc.backend=ComputePreference::RequireGpu;
            if(particles->init(device_,desc) && particles->gpuActive()) {
                next.gpu=true;next.gpuIndex=gpuGroups_.size();
                gpuGroups_.push_back({key,std::move(particles)});
                bindings_[name]=std::move(next);
                recountBindings();
                reason_.clear();
                return true;
            }
            reason_="GPU pool group initialization failed: "+particles->reason();
        } else reason_="SpectraFX max unique GPU resource groups reached";
    } else if(wantsGpu) {
        reason_=!eligible.eligible?eligible.reason:
            "Metal compute or required quality unavailable for pooled GPU emitter";
    }

    if(options.preference==ComputePreference::RequireGpu) {
        if(reason_.empty())reason_="GPU resource group required but unsupported";
        return false; // no fallback and no false group assignment
    }

    const int id=cpu_.defineEffect(effect); // retains entire original EffectDef
    if(id<0) {
        reason_+="; shared CPU fallback rejected full effect: "+cpu_.error();
        return false;
    }
    next.cpuId=id;
    bindings_[name]=std::move(next);
    recountBindings();
    reason_.clear();
    return true;
}

bool GpuPoolWorld::setNamedParameter(const std::string& effectName,
                                     const std::string& parameterName,float value) {
    auto it=bindings_.find(effectName);
    if(it==bindings_.end()) {
        reason_="unknown SpectraFX pool effect '"+effectName+"'";
        return false;
    }
    const auto param=it->second.named.find(parameterName);
    if(param==it->second.named.end()||
       !checkInstanceParameterValue(param->second,value)) {
        reason_="invalid or out-of-range pooled instance parameter '"+parameterName+"'";
        return false;
    }
    it->second.defaults[param->second.slot]=value;
    reason_.clear();
    return true;
}

bool GpuPoolWorld::burst(const std::string& effectName,fx::float3 position,
                         fx::float3 direction,fx::float4 tint,
                         const std::array<float,8>* sourceParams) {
    const auto it=bindings_.find(effectName);
    if(!initialized_||!frameOpen_||stepped_||it==bindings_.end()||
       stats_.frameBursts>=budgets_.maxBurstsPerFrame||
       !valid(position)||!valid(direction)||!valid(tint)) {
        ++stats_.rejectedBursts;
        reason_="invalid SpectraFX pool burst, missing frame/effect or frame budget reached";
        return false;
    }
    const Binding& binding=it->second;
    const auto& params=sourceParams?*sourceParams:binding.defaults;
    if(!validParams(params)) {
        ++stats_.rejectedBursts;
        reason_="nonfinite or unbounded SpectraFX pool instance parameters";
        return false;
    }
    for(const auto& [paramName,definition]:binding.named) {
        if(!checkInstanceParameterValue(definition,params[definition.slot])) {
            ++stats_.rejectedBursts;
            reason_="out-of-range SpectraFX pooled runtime parameter '"+paramName+"'";
            return false;
        }
    }
    bool ok=false;
    if(binding.gpu) {
        ComputeParticles& particles=*gpuGroups_[binding.gpuIndex].particles;
        ok=true;
        if(binding.usesRuntimeParams) {
            // Only affect future births: GPU custom attribute and parameter
            // snapshots already residing in this group remain unchanged.
            for(uint32_t i=0;i<8;++i)
                if(!particles.setProgramParameter(i,params[i],false)) {
                    ok=false;
                    reason_="failed to set pooled GPU per-source parameter";
                    break;
                }
        }
        if(ok)ok=particles.burstAt(position,direction,tint);
        if(!ok && reason_.empty())reason_=particles.reason();
    } else {
        const fx::FxHandle handle=cpu_.playWithProgramParameters(
            binding.cpuId,position,direction,1.f,tint,params);
        ok=handle.valid();
        if(!ok)reason_="shared CPU pooled one-shot was rejected";
    }
    if(ok) {
        ++stats_.submittedBursts;
        ++stats_.frameBursts;
        reason_.clear();
    } else ++stats_.rejectedBursts;
    return ok;
}

bool GpuPoolWorld::beginPresentationFrame(const render::RTSCamera& camera,
                                          const render::mat4& viewProj,
                                          int width,int height) {
    if(!initialized_||frameOpen_||width<=0||height<=0) {
        reason_="SpectraFX pool begin frame requires a ready world and viewport";
        return false;
    }
    frameOpen_=true;stepped_=drawn_=false;
    stats_.frameBursts=stats_.gpuDrawCalls=stats_.cpuDrawCalls=0;
    cpu_.setView(camera,viewProj,width,height);
    for(auto& group:gpuGroups_)
        group.particles->setView(camera,viewProj,width,height);
    reason_.clear();
    return true;
}

bool GpuPoolWorld::step(float dt) {
    if(!initialized_||!frameOpen_||stepped_||!valid(dt)||dt<0.f||dt>1.f) {
        reason_="SpectraFX pool step must run once before render with dt in [0,1]";
        return false;
    }
    stepped_=true;
    cpu_.update(dt);
    bool allOk=true;
    for(auto& group:gpuGroups_) {
        if(!group.particles->step(dt)) {
            allOk=false;
            reason_="GPU pool group step: "+group.particles->reason();
        }
    }
    stats_.gpuParticlesEstimated=0;
    stats_.gpuBytes=0;
    for(const auto& group:gpuGroups_) {
        const auto& stats=group.particles->stats();
        stats_.gpuParticlesEstimated+=stats.alive;
        stats_.gpuBytes+=stats.gpuBytes;
    }
    stats_.cpuParticles=uint32_t(cpu_.stats().particles);
    return allOk;
}

void GpuPoolWorld::draw(const fx::FxFog& fog) {
    if(!initialized_||!frameOpen_||!stepped_||drawn_)return;
    drawn_=true;
    cpu_.draw(fog);
    stats_.cpuDrawCalls=uint32_t(cpu_.stats().drawCalls);
    stats_.gpuDrawCalls=0;
    for(auto& group:gpuGroups_) {
        group.particles->draw(fog);
        stats_.gpuDrawCalls+=group.particles->stats().drawCalls;
    }
}

void GpuPoolWorld::endPresentationFrame() {
    frameOpen_=stepped_=drawn_=false;
}

bool GpuPoolWorld::resetForRollback() {
    if(!initialized_||frameOpen_||
       (device_&&(!device_->gpuHealthy() ||
                  !device_->canUpdateSharedBuffers()))) {
        reason_="SpectraFX pool reset must run outside GPU render/compute frame";
        return false;
    }
    cpu_.clear();
    for(auto& group:gpuGroups_)group.particles->clear();
    stats_.frameBursts=stats_.gpuDrawCalls=stats_.cpuDrawCalls=0;
    stats_.cpuParticles=stats_.gpuParticlesEstimated=0;
    reason_.clear();
    return true;
}

PoolCheckpointStats GpuPoolWorld::checkpointStats() const {
    PoolCheckpointStats result;
    if(checkpointBudget_)result.budget=checkpointBudget_->stats();
    for(const auto& group:gpuGroups_) {
        const GpuCheckpointStatus state=group.particles->checkpointStatus();
        const ComputeStats& stats=group.particles->stats();
        result.requests+=stats.checkpointRequests;
        result.completed+=stats.checkpointCompleted;
        result.failed+=stats.checkpointFailed;
        result.budgetDenied+=stats.checkpointBudgetDenied;
        result.bytesReturned+=stats.checkpointBytesReturned;
        if(!state.gpuActive)continue;
        ++result.activeGroups;
        if(state.pending)++result.pendingGroups;
        if(!state.complete)++result.missingGroups;
        else if(state.fresh)++result.freshGroups;
        else ++result.staleGroups;
        if(state.restoreEligible)++result.restoreEligibleGroups;
    }
    return result;
}

bool GpuPoolWorld::isGpuEffect(const std::string& name) const {
    const auto it=bindings_.find(name);
    return it!=bindings_.end() && it->second.gpu &&
        it->second.gpuIndex<gpuGroups_.size() &&
        gpuGroups_[it->second.gpuIndex].particles->gpuActive();
}

} // namespace aiverse::spectra
