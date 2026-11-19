// SpectraFX GPU emitter authored asset v1.
// GPU is an optional presentation optimization. CPU effect remains the
// complete compatibility fallback; no gameplay save format is affected.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include "../data/Json.h"
#include "SpectraFxHybridEmitter.h"

namespace aiverse::fx {

class SpectraFxGpuEmitterAsset {
public:
    static constexpr uint32_t kVersion=1;
    static bool read(const Json& doc,SpectraFxHybridEmitter::Config& output,
                     std::string* error=nullptr) {
        auto fail=[&](const std::string& why) {
            if(error) *error=why;
            return false;
        };
        if(!doc.isObject() || !doc["version"].isNumber() ||
           doc["version"].numberLiteral()!="1")
            return fail("SpectraFX GPU emitter expects JSON object version 1");
        for(const auto& pair:doc.members())
            if(pair.first!="version" && pair.first!="cpuEffect" &&
               pair.first!="impactEffect" &&
               pair.first!="preference" && pair.first!="particles" &&
               pair.first!="gpuIndirectCompaction" &&
               pair.first!="allowUnsortedAlpha" &&
               pair.first!="gpuSortAlpha" &&
               pair.first!="allowCpuFallback" && pair.first!="quality" &&
               pair.first!="spawn" && pair.first!="appearance" &&
               pair.first!="environment" && pair.first!="lod")
                return fail("SpectraFX unknown GPU emitter field "+pair.first);
        SpectraFxHybridEmitter::Config next;
        if(!doc["cpuEffect"].isString() ||
           doc["cpuEffect"].str().size()>128)
            return fail("cpuEffect must be an effect name string (<=128)");
        next.cpuEffect=doc["cpuEffect"].str();
        if(doc.has("allowUnsortedAlpha")) {
            if(doc["allowUnsortedAlpha"].type()!=Json::Type::Bool)
                return fail("allowUnsortedAlpha must be boolean");
            next.allowUnsortedAlpha=doc["allowUnsortedAlpha"].boolean();
        }
        if(doc.has("gpuSortAlpha")) {
            if(doc["gpuSortAlpha"].type()!=Json::Type::Bool)
                return fail("gpuSortAlpha must be boolean");
            next.gpuSortAlpha=doc["gpuSortAlpha"].boolean();
        }
        if(doc.has("gpuIndirectCompaction")) {
            if(doc["gpuIndirectCompaction"].type()!=Json::Type::Bool)
                return fail("gpuIndirectCompaction must be boolean");
            next.gpuIndirectCompaction=doc["gpuIndirectCompaction"].boolean();
        }
        if(doc.has("impactEffect")) {
            const Json& effect=doc["impactEffect"];
            if(!effect.isString() || effect.str().size()>64)
                return fail("impactEffect must be a safe effect name");
            for(unsigned char ch:effect.str())
                if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
                     (ch>='0'&&ch<='9')||ch=='_'||ch=='-'))
                    return fail("impactEffect contains an unsafe character");
            next.impactEffect=effect.str();
        }
        const auto mode=doc.getStr("preference","auto");
        if(mode=="cpu") next.backend.preference=SpectraFxExecutionPreference::Cpu;
        else if(mode=="gpu") next.backend.preference=SpectraFxExecutionPreference::Gpu;
        else if(mode=="auto") next.backend.preference=SpectraFxExecutionPreference::Auto;
        else return fail("preference must be cpu, gpu or auto");
        const auto q=doc.getStr("quality","high");
        if(q=="off") next.quality=FxQuality::Off;
        else if(q=="low") next.quality=FxQuality::Low;
        else if(q=="medium") next.quality=FxQuality::Medium;
        else if(q=="high") next.quality=FxQuality::High;
        else return fail("unknown SpectraFX quality");
        if(doc.has("particles")) {
            const auto& p=doc["particles"];
            if(!p.isNumber() || p.numberLiteral().find_first_of(".eE-")!=std::string::npos ||
               p.integer(-1)<0 || p.integer(-1)>65536)
                return fail("particles must be 0..65536 integer");
            next.backend.particles=uint32_t(p.integer());
        }
        if(doc.has("allowCpuFallback")) {
            if(doc["allowCpuFallback"].type()!=Json::Type::Bool)
                return fail("allowCpuFallback must be boolean");
            next.backend.allowCpuFallback=doc["allowCpuFallback"].boolean();
        }
        next.backend.required=SpectraFxFeature::Billboard | SpectraFxFeature::Alpha;
        const Json& spawn=doc["spawn"];
        if(!spawn.isNull()) {
            if(!spawn.isObject()) return fail("spawn must be an object");
            for(const auto& p:spawn.members())
                if(p.first!="rate" && p.first!="perMeter" && p.first!="burst" &&
                   p.first!="life" && p.first!="speed" && p.first!="spread" &&
                   p.first!="seed" && p.first!="maxPerFrame")
                    return fail("unknown spawn field "+p.first);
            if(!floatField(spawn,"rate",next.spawning.particlesPerSecond) ||
               !floatField(spawn,"perMeter",next.spawning.particlesPerMeter) ||
               !floatField(spawn,"life",next.spawning.lifetime) ||
               !floatField(spawn,"speed",next.spawning.speed) ||
               !floatField(spawn,"spread",next.spawning.spread) ||
               !uintField(spawn,"burst",next.spawning.startupBurst) ||
               !uintField(spawn,"seed",next.spawning.seed) ||
               !uintField(spawn,"maxPerFrame",next.spawning.maxSpawnsPerFrame))
                return fail("invalid SpectraFX spawn value");
        }
        const Json& appearance=doc["appearance"];
        if(!appearance.isNull()) {
            if(!appearance.isObject()) return fail("appearance must be object");
            for(const auto& p:appearance.members())
                if(p.first!="radius" && p.first!="color" && p.first!="additive")
                    return fail("unknown appearance field "+p.first);
            if(!floatField(appearance,"radius",next.appearance.radius) ||
               !vec3Field(appearance,"color",next.appearance.color))
                return fail("invalid GPU appearance");
            if(appearance.has("additive")) {
                if(appearance["additive"].type()!=Json::Type::Bool)
                    return fail("additive must be boolean");
                next.appearance.additive=appearance["additive"].boolean();
            }
        }
        const Json& environment=doc["environment"];
        if(!environment.isNull()) {
            if(!environment.isObject()) return fail("environment must be object");
            for(const auto& p:environment.members())
                if(p.first!="acceleration" && p.first!="damping" &&
                   p.first!="groundCollision")
                    return fail("unknown environment field "+p.first);
            if(!floatField(environment,"damping",next.environment.damping) ||
               !vec3Field(environment,"acceleration",next.environment.acceleration))
                return fail("invalid environment");
            const Json& ground=environment["groundCollision"];
            if(!ground.isNull()) {
                if(!ground.isObject()) return fail("groundCollision must be object");
                for(const auto& p:ground.members())
                    if(p.first!="enabled" && p.first!="height" &&
                       p.first!="restitution" && p.first!="tangentRetention" &&
                       p.first!="emitImpactEvents")
                        return fail("unknown groundCollision field "+p.first);
                if(ground.has("enabled")) {
                    if(ground["enabled"].type()!=Json::Type::Bool)
                        return fail("groundCollision.enabled must be boolean");
                    next.environment.collideGround=ground["enabled"].boolean();
                }
                if(ground.has("emitImpactEvents")) {
                    if(ground["emitImpactEvents"].type()!=Json::Type::Bool)
                        return fail("groundCollision.emitImpactEvents must be boolean");
                    next.environment.reportImpacts=ground["emitImpactEvents"].boolean();
                }
                if(!floatField(ground,"height",next.environment.groundHeight) ||
                   !floatField(ground,"restitution",next.environment.restitution) ||
                   !floatField(ground,"tangentRetention",next.environment.tangentRetention))
                    return fail("invalid groundCollision configuration");
            }
        }
        const Json& lod=doc["lod"];
        if(!lod.isNull()) {
            if(!lod.isObject()) return fail("lod must be an object");
            for(const auto& p:lod.members())
                if(p.first!="near" && p.first!="far" &&
                   p.first!="farSpawnScale" && p.first!="cullBeyondFar")
                    return fail("unknown lod field "+p.first);
            if(!floatField(lod,"near",next.lod.nearDistance) ||
               !floatField(lod,"far",next.lod.farDistance) ||
               !floatField(lod,"farSpawnScale",next.lod.farSpawnScale))
                return fail("invalid GPU LOD distances/scaling");
            if(lod.has("cullBeyondFar")) {
                if(lod["cullBeyondFar"].type()!=Json::Type::Bool)
                    return fail("lod.cullBeyondFar must be boolean");
                next.lod.cullBeyondFar=lod["cullBeyondFar"].boolean();
            }
            if(!next.lod.validate()) return fail("invalid GPU LOD range");
        }
        if(next.gpuSortAlpha &&
           (next.appearance.additive || next.gpuIndirectCompaction))
            return fail("gpuSortAlpha requires alpha blend and forbids indirect compaction");
        // Perform the same bounds as the backend's public runtime contract.
        SpectraFxGpuSpawnScheduler validation;
        if(!validation.configure(next.spawning) ||
           !std::isfinite(next.appearance.radius) ||
           next.appearance.radius<=0 || next.appearance.radius>1000 ||
           !std::isfinite(next.environment.damping) ||
           next.environment.damping<0 || next.environment.damping>1 ||
           !std::isfinite(next.environment.groundHeight) ||
           !std::isfinite(next.environment.restitution) ||
           next.environment.restitution<0 || next.environment.restitution>1 ||
           !std::isfinite(next.environment.tangentRetention) ||
           next.environment.tangentRetention<0 || next.environment.tangentRetention>1)
            return fail("GPU emitter configuration out of range");
        if(!next.impactEffect.empty() && !next.environment.reportImpacts)
            return fail("impactEffect requires groundCollision.emitImpactEvents");
        if(next.environment.collideGround)
            next.backend.required=next.backend.required |
                                  SpectraFxFeature::GroundPlaneCollision;
        if(next.environment.reportImpacts) {
            if(!next.environment.collideGround)
                return fail("emitImpactEvents requires groundCollision.enabled");
            next.backend.required=next.backend.required |
                                  SpectraFxFeature::GpuImpactFeedback;
        }
        if(next.backend.preference!=SpectraFxExecutionPreference::Cpu &&
           next.backend.particles==0)
            return fail("GPU/auto emitter requires nonzero capacity");
        if(next.cpuEffect.empty() && (next.backend.preference==SpectraFxExecutionPreference::Cpu ||
                                     next.backend.allowCpuFallback))
            return fail("CPU fallback requires an effect");
        output=std::move(next);
        if(error) error->clear();
        return true;
    }
    static bool readText(const std::string& text,SpectraFxHybridEmitter::Config& output,
                         std::string* error=nullptr) {
        try{return read(Json::parse(text),output,error);}
        catch(const std::exception& e) {
            if(error) *error=e.what();
            return false;
        }
    }
private:
    static bool floatField(const Json& doc,const char* key,float& dst) {
        if(!doc.has(key)) return true;
        const Json& v=doc[key];
        if(!v.isNumber()) return false;
        const double value=v.toDouble();
        if(!std::isfinite(value) || value < -10000000 || value > 10000000)
            return false;
        dst=float(value);
        return true;
    }
    static bool uintField(const Json& doc,const char* key,uint32_t& dst) {
        if(!doc.has(key)) return true;
        const Json& v=doc[key];
        if(!v.isNumber()||v.numberLiteral().find_first_of(".eE-")!=std::string::npos)
            return false;
        const int64_t value=v.integer(-1);
        if(value<0 || uint64_t(value)>uint64_t(UINT32_MAX)) return false;
        dst=uint32_t(value);
        return true;
    }
    static bool vec3Field(const Json& doc,const char* key,render::float3& dst) {
        if(!doc.has(key)) return true;
        const Json& a=doc[key];
        if(!a.isArray()||a.size()!=3) return false;
        float x=0,y=0,z=0;
        Json j=Json::object();
        j.set("x",a[0]);j.set("y",a[1]);j.set("z",a[2]);
        if(!floatField(j,"x",x)||!floatField(j,"y",y)||!floatField(j,"z",z))
            return false;
        dst={x,y,z};return true;
    }
};
} // namespace aiverse::fx
