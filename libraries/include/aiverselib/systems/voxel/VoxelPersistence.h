#pragma once
// voxel-world save record (kind 120) in NativeState arrays. Saved: bounds, editSeq, the list of resident chunks
// and every edited chunk (resident or parked) as canonical chunk bytes. Not saved: light, meshes, dirty sets
// and pending edits (derived or transient). After loadVoxelWorld the world is empty with everything parked:
// the host must make the same chunks resident again (unpark edited ones, regenerate the rest) before the next
// tick; then hash() equals the saved world's hash.
#include "VoxelWorld.h"
#include "modules/rts/RtsNative.h"
namespace aiverse::lib::systems {
inline constexpr std::int64_t kVoxelWorldRecord=120;
inline void voxelPackBytes(std::vector<std::int64_t>& out,const std::vector<std::uint8_t>& b){
    out.push_back(static_cast<std::int64_t>(b.size()));
    for(std::size_t i=0;i<b.size();i+=8){std::uint64_t w=0;for(std::size_t k=0;k<8&&i+k<b.size();++k)w|=static_cast<std::uint64_t>(b[i+k])<<(8*k);out.push_back(static_cast<std::int64_t>(w));}
}
inline bool voxelUnpackBytes(const std::vector<std::int64_t>& in,std::size_t& i,std::vector<std::uint8_t>& b){
    if(i>=in.size()||in[i]<0)return false;auto n=static_cast<std::uint64_t>(in[i++]);auto words=(n+7)/8;if(words>in.size()-i)return false;
    b.assign(static_cast<std::size_t>(n),0);for(std::size_t k=0;k<n;++k)b[k]=static_cast<std::uint8_t>(static_cast<std::uint64_t>(in[i+k/8])>>(8*(k%8)));
    i+=static_cast<std::size_t>(words);return true;
}
inline bool storeVoxelWorld(game::NativeState& state,const std::string& key,const VoxelWorld& w){
    if(key.empty())return false;
    std::vector<std::int64_t> out{1,kVoxelWorldRecord,w.bounds().minChunkY,w.bounds().maxChunkY,w.bounds().horizontalChunks,static_cast<std::int64_t>(w.editSeq)};
    out.push_back(static_cast<std::int64_t>(w.chunks().size()));for(const auto& [c,chunk]:w.chunks())out.insert(out.end(),{c.x,c.y,c.z});
    auto saved=w.parkedChunks();for(auto c:w.edited)if(auto* chunk=w.chunk(c))saved[c]=encodeChunk(*chunk);
    out.push_back(static_cast<std::int64_t>(saved.size()));
    for(const auto& [c,bytes]:saved){out.insert(out.end(),{c.x,c.y,c.z});voxelPackBytes(out,bytes);}
    state.arrays[key]=std::move(out);return true;
}
struct VoxelWorldSave{VoxelWorld world;std::vector<ChunkPos> resident;};
inline std::optional<VoxelWorldSave> loadVoxelWorld(const game::NativeState& state,const std::string& key,const BlockRegistry& r,std::size_t maxChunks=1<<20){
    auto it=state.arrays.find(key);if(it==state.arrays.end())return {};const auto& d=it->second;
    if(d.size()<7||d[0]!=1||d[1]!=kVoxelWorldRecord||d[5]<0)return {};
    VoxelBounds b;b.minChunkY=static_cast<std::int32_t>(d[2]);b.maxChunkY=static_cast<std::int32_t>(d[3]);b.horizontalChunks=static_cast<std::int32_t>(d[4]);
    if(d[2]>d[3]||d[2]< -(1<<22)||d[3]>(1<<22)||d[4]<1||d[4]>(1<<22))return {};
    VoxelWorldSave out{VoxelWorld(b),{}};out.world.editSeq=static_cast<std::uint64_t>(d[5]);
    std::size_t i=6;auto n=static_cast<std::uint64_t>(d[i++]);if(d[6]<0||n>maxChunks||n*3>d.size()-i)return {};
    for(std::uint64_t k=0;k<n;++k){ChunkPos c{static_cast<std::int32_t>(d[i]),static_cast<std::int32_t>(d[i+1]),static_cast<std::int32_t>(d[i+2])};i+=3;
        if(!out.world.inBounds(c)||(!out.resident.empty()&&!(out.resident.back()<c)))return {};out.resident.push_back(c);}
    if(i>=d.size()||d[i]<0)return {};auto m=static_cast<std::uint64_t>(d[i++]);if(m>maxChunks)return {};
    std::map<ChunkPos,std::vector<std::uint8_t>> parked;
    for(std::uint64_t k=0;k<m;++k){if(d.size()-i<3)return {};ChunkPos c{static_cast<std::int32_t>(d[i]),static_cast<std::int32_t>(d[i+1]),static_cast<std::int32_t>(d[i+2])};i+=3;
        std::vector<std::uint8_t> bytes;if(!out.world.inBounds(c)||!voxelUnpackBytes(d,i,bytes)||!decodeChunk(bytes,r.defs.size())||!parked.emplace(c,std::move(bytes)).second)return {};}
    if(i!=d.size())return {};
    out.world.restoreParked(std::move(parked));return out;
}
}
