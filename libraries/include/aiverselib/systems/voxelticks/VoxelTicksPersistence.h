#pragma once
// voxel-ticks save record (kind 121) in NativeState arrays: pending updates in queue order plus counters.
#include "VoxelTicks.h"
#include "modules/rts/RtsNative.h"
namespace aiverse::lib::systems {
inline constexpr std::int64_t kVoxelTicksRecord=121;
inline bool storeVoxelTicks(game::NativeState& state,const std::string& key,const VoxelTickState& s){
    if(key.empty()||s.sequence>INT64_MAX||s.lastTick>INT64_MAX)return false;
    std::vector<std::int64_t> out{1,kVoxelTicksRecord,static_cast<std::int64_t>(s.sequence),static_cast<std::int64_t>(s.lastTick),s.started?1:0,static_cast<std::int64_t>(s.queue.size())};
    for(const auto& [at,seq,p]:s.queue){if(at>INT64_MAX)return false;out.insert(out.end(),{static_cast<std::int64_t>(at),static_cast<std::int64_t>(seq),p.x,p.y,p.z});}
    state.arrays[key]=std::move(out);return true;
}
inline std::optional<VoxelTickState> loadVoxelTicks(const game::NativeState& state,const std::string& key,std::size_t maxPending=1<<20){
    auto it=state.arrays.find(key);if(it==state.arrays.end())return {};const auto& d=it->second;
    if(d.size()<6||d[0]!=1||d[1]!=kVoxelTicksRecord||d[2]<0||d[3]<0||(d[4]!=0&&d[4]!=1)||d[5]<0||static_cast<std::uint64_t>(d[5])>maxPending||d.size()!=6+static_cast<std::size_t>(d[5])*5)return {};
    VoxelTickState s;s.sequence=static_cast<std::uint64_t>(d[2]);s.lastTick=static_cast<std::uint64_t>(d[3]);s.started=d[4]==1;
    for(std::size_t i=6;i<d.size();i+=5){if(d[i]<0||d[i+1]<=0||static_cast<std::uint64_t>(d[i+1])>s.sequence)return {};
        BlockPos p{static_cast<std::int32_t>(d[i+2]),static_cast<std::int32_t>(d[i+3]),static_cast<std::int32_t>(d[i+4])};
        if(d[i+2]!=p.x||d[i+3]!=p.y||d[i+4]!=p.z||s.due.count(p))return {};
        s.due[p]=static_cast<std::uint64_t>(d[i]);s.queue.insert({static_cast<std::uint64_t>(d[i]),static_cast<std::uint64_t>(d[i+1]),p});}
    return s;
}
}
