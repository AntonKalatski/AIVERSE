// SpectraFX opt-in project GPU event routes, authored as fx/gpu/routes.json.
// Safe default: only events belonging to the local viewer are forwarded.
// Presentation routing does not mutate or subscribe to authoritative ECS state.
#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include "../data/Json.h"
#include "SpectraFxHybridPlayback.h"

namespace aiverse::fx {

struct SpectraFxGpuEventRoute {
    uint16_t eventType = 0;
    SpectraFxHybridPlayback::Route playback;
    // A source game must explicitly opt in to its own event types. Never
    // render opponents' hidden events merely because they exist in the sim.
    bool ownPlayerOnly = true;
};

class SpectraFxGpuRouteAsset {
public:
    using Routes = std::unordered_map<uint16_t, SpectraFxGpuEventRoute>;
    static bool read(const Json& doc, Routes& result, std::string* error=nullptr) {
        auto fail=[&](const std::string& reason){
            if(error) *error=reason;
            return false;
        };
        if(!doc.isObject() || doc.getInt("version",-1)!=1)
            return fail("SpectraFX GPU routes require version 1");
        for(const auto& p:doc.members())
            if(p.first!="version" && p.first!="routes")
                return fail("SpectraFX GPU routes: unknown field "+p.first);
        const Json& values=doc["routes"];
        if(!values.isArray() || values.size()>64)
            return fail("SpectraFX GPU routes must contain at most 64 entries");
        Routes next;
        for(const auto& value:values.items()) {
            if(!value.isObject()) return fail("SpectraFX GPU route must be object");
            for(const auto& p:value.members())
                if(p.first!="eventType" && p.first!="asset" &&
                   p.first!="oneShotSeconds" && p.first!="enabled" &&
                   p.first!="ownPlayerOnly")
                    return fail("SpectraFX GPU route: unknown field "+p.first);
            const Json& event=value["eventType"];
            if(!event.isNumber() ||
               event.numberLiteral().find_first_of(".eE-")!=std::string::npos ||
               event.integer(-1)<0 || event.integer(-1)>65535)
                return fail("SpectraFX eventType must be uint16");
            const Json& asset=value["asset"];
            if(!asset.isString() ||
               !SpectraFxGpuEmitterLibrary::validName(asset.str()))
                return fail("SpectraFX invalid GPU emitter asset name");
            SpectraFxGpuEventRoute entry;
            entry.eventType=uint16_t(event.integer());
            entry.playback.asset=asset.str();
            if(value.has("enabled")) {
                if(value["enabled"].type()!=Json::Type::Bool)
                    return fail("SpectraFX enabled must be bool");
                entry.playback.enabled=value["enabled"].boolean();
            }
            if(value.has("ownPlayerOnly")) {
                if(value["ownPlayerOnly"].type()!=Json::Type::Bool)
                    return fail("SpectraFX ownPlayerOnly must be bool");
                entry.ownPlayerOnly=value["ownPlayerOnly"].boolean();
                // Hidden enemy events must not leak through FX.
                if(!entry.ownPlayerOnly)
                    return fail("SpectraFX shared event visibility requires a host visibility adapter");
            }
            if(value.has("oneShotSeconds")) {
                if(!value["oneShotSeconds"].isNumber())
                    return fail("SpectraFX oneShotSeconds must be numeric");
                const double seconds=value["oneShotSeconds"].toDouble();
                if(!(seconds>0 && seconds<=60))
                    return fail("SpectraFX oneShotSeconds must be in (0,60]");
                entry.playback.oneShotSeconds=float(seconds);
            }
            if(!next.emplace(entry.eventType,std::move(entry)).second)
                return fail("SpectraFX duplicate eventType route");
        }
        result=std::move(next);
        if(error) error->clear();
        return true;
    }
    static bool readText(const std::string& text,Routes& routes,
                         std::string* error=nullptr) {
        try {return read(Json::parse(text),routes,error);}
        catch(const std::exception& e) {
            if(error) *error=e.what();
            return false;
        }
    }
};
} // namespace aiverse::fx
