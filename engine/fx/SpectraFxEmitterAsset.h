// SpectraFX emitter program asset codec v1. Strict JSON and atomic parse.
// Effect names resolve lazily through the existing ParticleSystem loader.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "../data/Json.h"
#include "SpectraFxEmitterRuntime.h"

namespace aiverse::fx {

class SpectraFxEmitterAssetCodec {
public:
    static const char* stageName(SpectraFxStage stage) {
        switch(stage) {
        case SpectraFxStage::Spawn: return "spawn";
        case SpectraFxStage::Initialize: return "initialize";
        case SpectraFxStage::Update: return "update";
        case SpectraFxStage::Render: return "render";
        }
        return "";
    }

    static bool read(const Json& root,SpectraFxEmitterProgram& output,
                     std::string* error=nullptr) {
        auto fail=[&](const std::string& msg) {
            if(error) *error=msg;
            return false;
        };
        if(!root.isObject()) return fail("SpectraFX emitter asset must be object");
        for(const auto& p: root.members())
            if(p.first!="version" && p.first!="stages")
                return fail("SpectraFX unknown emitter asset field: "+p.first);
        if(!root["version"].isNumber() ||
           root["version"].numberLiteral()!="1")
            return fail("SpectraFX emitter asset version must be 1");
        const Json& stages=root["stages"];
        if(!stages.isArray() || stages.size()==0 || stages.size()>4)
            return fail("SpectraFX invalid emitter stages");
        SpectraFxEmitterProgram program;
        for(size_t i=0;i<stages.size();++i) {
            const Json& item=stages[i];
            if(!item.isObject()) return fail("SpectraFX stage must be object");
            for(const auto& p:item.members())
                if(p.first!="stage" && p.first!="enabled" && p.first!="effect" &&
                   p.first!="scale" && p.first!="tint")
                    return fail("SpectraFX unknown stage field: "+p.first);
            if(!item["stage"].isString())
                return fail("SpectraFX stage name missing");
            SpectraFxStageDef def;
            bool matched=false;
            for(auto value : {SpectraFxStage::Spawn,SpectraFxStage::Initialize,
                              SpectraFxStage::Update,SpectraFxStage::Render})
                if(item["stage"].str()==stageName(value)) {
                    def.stage=value;
                    matched=true;
                    break;
                }
            if(!matched) return fail("SpectraFX unknown stage");
            if(item.has("enabled")) {
                if(item["enabled"].type()!=Json::Type::Bool)
                    return fail("SpectraFX enabled must be boolean");
                def.enabled=item["enabled"].boolean();
            }
            if(item.has("effect")) {
                if(!item["effect"].isString())
                    return fail("SpectraFX effect name must be string");
                def.effect=item["effect"].str();
            }
            if(item.has("scale")) {
                if(!item["scale"].isNumber())
                    return fail("SpectraFX scale must be numeric");
                def.scale=static_cast<float>(item["scale"].toDouble());
            }
            if(item.has("tint")) {
                const Json& tint=item["tint"];
                if(!tint.isArray() || tint.size()!=4)
                    return fail("SpectraFX tint must have 4 channels");
                float rgba[4];
                for(size_t j=0;j<4;++j) {
                    if(!tint[j].isNumber())
                        return fail("SpectraFX tint channel must be numeric");
                    rgba[j]=static_cast<float>(tint[j].toDouble());
                }
                def.tint={rgba[0],rgba[1],rgba[2],rgba[3]};
            }
            program.stages.push_back(std::move(def));
        }
        if(!program.validate(error)) return false;
        output=std::move(program);
        if(error) error->clear();
        return true;
    }

    static bool readText(const std::string& json,SpectraFxEmitterProgram& output,
                         std::string* error=nullptr) {
        try { return read(Json::parse(json),output,error); }
        catch(const JsonError& e) {
            if(error) *error=e.what();
            return false;
        }
    }

    static Json write(const SpectraFxEmitterProgram& program) {
        Json root=Json::object();
        root.set("version",Json::number(int64_t(1)));
        Json list=Json::array();
        for(const auto& stage:program.stages) {
            Json item=Json::object();
            item.set("stage",Json::string(stageName(stage.stage)));
            item.set("enabled",Json::fromBool(stage.enabled));
            if(!stage.effect.empty())
                item.set("effect",Json::string(stage.effect));
            if(stage.stage==SpectraFxStage::Initialize) {
                item.set("scale",Json::number(double(stage.scale),6));
                Json tint=Json::array();
                tint.push(Json::number(double(stage.tint.x),6));
                tint.push(Json::number(double(stage.tint.y),6));
                tint.push(Json::number(double(stage.tint.z),6));
                tint.push(Json::number(double(stage.tint.w),6));
                item.set("tint",std::move(tint));
            }
            list.push(std::move(item));
        }
        root.set("stages",std::move(list));
        return root;
    }
};

} // namespace aiverse::fx
