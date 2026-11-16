#pragma once
// voxel-ticks JSON: {"voxelTicks":{"randomTicksPerChunk":3,"maxUpdatesPerStep":4096,"fallTicks":2,
//   "reactions":[{"fluid":"lava","touching":"water","source":"obsidian","flowing":"cobblestone"}]}}
// "fluid" / "touching" name fluid blocks (their fluid groups are used). Errors: JsonError("voxelTicks.reactions[0]...").
#include "VoxelTicks.h"
#include "engine/data/Json.h"
namespace aiverse::lib::systems {
inline VoxelTickRules readVoxelTicks(const Json& root,const BlockRegistry& r){
    const auto& j=root.has("voxelTicks")?root["voxelTicks"]:root;VoxelTickRules out;if(j.isNull())return out;
    if(!j.isObject())throw JsonError("voxelTicks: expected an object");
    auto num=[&](const Json& o,const char* key,std::int64_t def,std::int64_t lo,std::int64_t hi,const std::string& at){auto v=o.getInt(key,def);if(v<lo||v>hi)throw JsonError(at+"."+key+": "+std::to_string(lo)+".."+std::to_string(hi));return v;};
    out.randomTicksPerChunk=static_cast<std::uint32_t>(num(j,"randomTicksPerChunk",3,0,4096,"voxelTicks"));
    out.maxUpdatesPerStep=static_cast<std::uint32_t>(num(j,"maxUpdatesPerStep",4096,1,1<<20,"voxelTicks"));
    out.fallTicks=static_cast<std::uint32_t>(num(j,"fallTicks",2,1,1200,"voxelTicks"));
    const auto& rs=j["reactions"];
    for(std::size_t i=0;i<rs.size();++i){const auto& x=rs[i];auto at="voxelTicks.reactions["+std::to_string(i)+"]";
        auto fluidOf=[&](const char* key){auto n=x.getStr(key);auto id=r.find(n);if(!id||!r[*id].fluid)throw JsonError(at+"."+key+": unknown fluid block '"+n+"'");return r[*id].fluid;};
        auto blockOf=[&](const char* key){if(!x.has(key))return kAir;auto n=x.getStr(key);auto id=r.find(n);if(!id)throw JsonError(at+"."+key+": unknown block '"+n+"'");return *id;};
        VoxelFluidReaction rx;rx.fluid=fluidOf("fluid");rx.touching=fluidOf("touching");rx.fromSource=blockOf("source");rx.fromFlowing=blockOf("flowing");
        if(rx.fluid==rx.touching)throw JsonError(at+": a fluid cannot react with itself");out.reactions.push_back(rx);}
    return out;
}
}
