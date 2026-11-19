// NEREID Stage 44: optional static JSON authoring of visual confluences.
// Validates references against explicit water source IDs before WorldScene
// construction; no ECS entity creation or water-state mutation.
#pragma once
#include "engine/world/NereidConfluenceScene.h"
#include "engine/world/NereidWaterManifest.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aiverse::world {
struct NereidConfluenceManifest {
    std::vector<NereidConfluenceSource> sources;
};

namespace nereid_confluence_manifest_detail {
inline uint64_t waterId(const Json& value) {
    if(!value.isNumber())
        throw JsonError("nereidConfluences: water ID must be an integer");
    const Fixed number=value.fx();
    if(number<1_fx || number>Fixed::fromInt(2147483647) ||
       number.raw % Fixed::kOne!=0)
        throw JsonError("nereidConfluences: water ID out of range");
    return uint64_t(number.toInt());
}
inline const NereidSceneSurface& linkedWater(
        const std::map<uint64_t,const NereidSceneSurface*>& bodies,
        uint64_t id,Vec3 mouth) {
    const auto found=bodies.find(id);
    if(found==bodies.end())
        throw JsonError(
            "nereidConfluences: unknown water ID (use explicit nereidWater.surfaces.id)");
    const auto& source=*found->second;
    if(!lib::nereid::coversWaterBody(
            source.body,source.useGeometry?&source.geometry:nullptr,mouth))
        throw JsonError("nereidConfluences: mouth lies outside linked water");
    return source;
}
} // namespace nereid_confluence_manifest_detail

// Manifest-only style. This is intentionally separate from the ECS snapshot
// adapter: JSON numbers are scene IDs, not generation-safe ECS Entity IDs.
inline NereidConfluenceManifest parseNereidConfluenceManifest(
        const Json& root,const std::vector<NereidSceneSurface>& waterBodies) {
    NereidConfluenceManifest out;
    if(!root.has("nereidConfluences"))return out;
    const auto& settings=root["nereidConfluences"];
    if(!settings.isObject())
        throw JsonError("nereidConfluences: expected object");
    const auto& sources=settings["sources"];
    if(!sources.isArray() ||
       sources.size()>NereidConfluenceScene::kMaxSources)
        throw JsonError("nereidConfluences.sources: expected at most 128 entries");

    std::map<uint64_t,const NereidSceneSurface*> byId;
    for(const auto& water:waterBodies) {
        // Auto-assigned IDs are deliberately not linkable by JSON: they
        // change when an author reorders the water-surface array.
        if(water.id && !byId.emplace(water.id,&water).second)
            throw JsonError("nereidConfluences: duplicate water source ID");
    }
    std::map<uint64_t,bool> usedIds;
    out.sources.reserve(sources.size());
    for(const auto& item:sources.items()) {
        if(!item.isObject())
            throw JsonError("nereidConfluences: expected source object");
        NereidConfluenceSource source;
        source.id=uint64_t(worldInteger(item,"id",0,1,2147483647));
        if(!usedIds.emplace(source.id,true).second)
            throw JsonError("nereidConfluences: duplicate confluence ID");
        if(item.has("enabled") &&
           item["enabled"].type()!=Json::Type::Bool)
            throw JsonError("nereidConfluences.enabled: expected boolean");
        auto& flow=source.junction;
        flow.enabled=item.getBool("enabled",true);

        const auto& inlets=item["inletWaterIds"];
        if(!inlets.isArray() ||
           inlets.size()<2 ||
           inlets.size()>lib::nereid::kMaxWaterConfluenceInlets)
            throw JsonError("nereidConfluences.inletWaterIds: expected 2..4 IDs");
        flow.inletCount=uint8_t(inlets.size());
        source.outletWaterId=uint64_t(
            worldInteger(item,"outletWaterId",0,1,2147483647));
        for(size_t i=0;i<inlets.size();++i) {
            const uint64_t id=nereid_confluence_manifest_detail::waterId(inlets[i]);
            if(id==source.outletWaterId)
                throw JsonError("nereidConfluences: outlet cannot be an inlet");
            for(size_t j=0;j<i;++j)
                if(source.inletWaterIds[j]==id)
                    throw JsonError("nereidConfluences: duplicate inlet water ID");
            source.inletWaterIds[i]=id;
        }
        // A scene linkage is not a persisted ECS component; the Entity
        // link fields deliberately stay at kNullEntity.
        if(!item["mouth"].isArray() || item["mouth"].size()!=3)
            throw JsonError("nereidConfluences.mouth: expected [x,y,z]");
        flow.mouth=worldVector(item["mouth"],100000_fx);
        if(item.has("radius"))
            flow.radius=nereid_manifest_detail::positive(
                item["radius"],"confluence.radius",1000_fx);
        if(item.has("blend"))
            flow.blend=nereid_manifest_detail::coordinate(
                item["blend"],"confluence.blend",1_fx);
        if(item.has("speedLimit"))
            flow.speedLimit=nereid_manifest_detail::positive(
                item["speedLimit"],"confluence.speedLimit",1000_fx);
        if(flow.inletCount<2 || flow.inletCount>lib::nereid::kMaxWaterConfluenceInlets ||
           flow.radius<Fixed::fromRatio(1,10) || flow.radius>1000_fx ||
           flow.blend<0_fx || flow.blend>1_fx ||
           flow.speedLimit<=0_fx || flow.speedLimit>1000_fx)
            throw JsonError("nereidConfluences: invalid flow policy");

        // Explicit authoring mistakes are rejected before creating any
        // GPU residency, even if the source is currently disabled.
        nereid_confluence_manifest_detail::linkedWater(
            byId,source.outletWaterId,flow.mouth);
        for(size_t i=0;i<flow.inletCount;++i)
            nereid_confluence_manifest_detail::linkedWater(
                byId,source.inletWaterIds[i],flow.mouth);
        // Disabled definitions are valid authoring entries but omitted from
        // the active read-only scene. They can be enabled without ECS edits.
        if(flow.enabled)out.sources.push_back(std::move(source));
    }
    return out;
}
} // namespace aiverse::world
