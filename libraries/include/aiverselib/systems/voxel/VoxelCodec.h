#pragma once
// voxel-world: byte codec for chunks (palette + run lengths) and region files (several chunks + checksum).
// The same bytes are used for saves, region files on disk and network snapshots; encoding is canonical
// (sorted palette, fixed field order), so equal chunks give equal bytes on every platform.
#include "VoxelTypes.h"
#include <algorithm>
#include <map>
namespace aiverse::lib::systems {
struct VoxelBytes{
    std::vector<std::uint8_t> data;
    void byte(std::uint8_t b){data.push_back(b);}
    void varint(std::uint64_t v){while(v>=0x80){data.push_back(static_cast<std::uint8_t>(v|0x80));v>>=7;}data.push_back(static_cast<std::uint8_t>(v));}
    void svarint(std::int64_t v){varint((static_cast<std::uint64_t>(v)<<1)^static_cast<std::uint64_t>(v>>63));}
    void append(const std::vector<std::uint8_t>& b){data.insert(data.end(),b.begin(),b.end());}
};
struct VoxelReader{
    const std::uint8_t* p=nullptr;std::size_t n=0,i=0;bool ok=true;
    VoxelReader(const std::uint8_t* data,std::size_t size):p(data),n(size){}
    explicit VoxelReader(const std::vector<std::uint8_t>& v):p(v.data()),n(v.size()){}
    std::uint8_t byte(){if(i>=n){ok=false;return 0;}return p[i++];}
    std::uint64_t varint(){std::uint64_t v=0;for(int s=0;s<64;s+=7){auto b=byte();if(!ok)return 0;v|=static_cast<std::uint64_t>(b&0x7f)<<s;if(!(b&0x80))return v;}ok=false;return 0;}
    std::int64_t svarint(){auto u=varint();return static_cast<std::int64_t>(u>>1)^-static_cast<std::int64_t>(u&1);}
    bool done()const{return ok&&i==n;}
};
inline constexpr std::uint8_t kVoxelChunkFormat=1;
inline std::vector<std::uint8_t> encodeChunk(const VoxelChunk& c){
    VoxelBytes out;out.byte('V');out.byte('C');out.byte(kVoxelChunkFormat);
    std::vector<BlockId> palette(c.blocks.begin(),c.blocks.end());std::sort(palette.begin(),palette.end());palette.erase(std::unique(palette.begin(),palette.end()),palette.end());
    out.varint(palette.size());for(auto id:palette)out.varint(id);
    for(int i=0;i<kVoxelChunkVolume;){int j=i;while(j<kVoxelChunkVolume&&c.blocks[j]==c.blocks[i])++j;
        out.varint(static_cast<std::uint64_t>(std::lower_bound(palette.begin(),palette.end(),c.blocks[i])-palette.begin()));out.varint(static_cast<std::uint64_t>(j-i));i=j;}
    for(int i=0;i<kVoxelChunkVolume;){int j=i;while(j<kVoxelChunkVolume&&c.meta[j]==c.meta[i])++j;out.byte(c.meta[i]);out.varint(static_cast<std::uint64_t>(j-i));i=j;}
    out.varint(c.revision);
    return std::move(out.data);
}
// Rejects malformed input and ids outside `blockTypes`; light is left zero (derived, recompute with voxel-light).
inline std::optional<VoxelChunk> decodeChunk(const std::uint8_t* data,std::size_t size,std::size_t blockTypes){
    VoxelReader in(data,size);
    if(in.byte()!='V'||in.byte()!='C'||in.byte()!=kVoxelChunkFormat||!in.ok)return {};
    auto count=in.varint();if(!in.ok||count==0||count>kVoxelChunkVolume)return {};
    std::vector<BlockId> palette;palette.reserve(count);
    for(std::uint64_t k=0;k<count;++k){auto id=in.varint();if(!in.ok||id>=blockTypes||(!palette.empty()&&id<=palette.back()))return {};palette.push_back(static_cast<BlockId>(id));}
    VoxelChunk c;
    for(int i=0;i<kVoxelChunkVolume;){auto idx=in.varint(),run=in.varint();if(!in.ok||idx>=palette.size()||run==0||run>static_cast<std::uint64_t>(kVoxelChunkVolume-i))return {};
        std::fill(c.blocks.begin()+i,c.blocks.begin()+i+static_cast<int>(run),palette[idx]);i+=static_cast<int>(run);}
    for(int i=0;i<kVoxelChunkVolume;){auto v=in.byte();auto run=in.varint();if(!in.ok||run==0||run>static_cast<std::uint64_t>(kVoxelChunkVolume-i))return {};
        std::fill(c.meta.begin()+i,c.meta.begin()+i+static_cast<int>(run),v);i+=static_cast<int>(run);}
    auto rev=in.varint();if(!in.done()||rev>0xffffffffull)return {};
    c.revision=static_cast<std::uint32_t>(rev);c.nonAir=countNonAir(c);return c;
}
inline std::optional<VoxelChunk> decodeChunk(const std::vector<std::uint8_t>& b,std::size_t blockTypes){return decodeChunk(b.data(),b.size(),blockTypes);}

// Regions group whole chunk columns: kVoxelRegionChunks x kVoxelRegionChunks columns, every height.
inline constexpr std::int32_t kVoxelRegionChunks=32;
struct RegionPos{std::int32_t x=0,z=0;auto operator<=>(const RegionPos&)const=default;};
inline constexpr RegionPos regionOf(ChunkPos c){return {c.x>>5,c.z>>5};}
inline constexpr std::uint8_t kVoxelRegionFormat=1;
inline std::vector<std::uint8_t> encodeRegion(const std::map<ChunkPos,std::vector<std::uint8_t>>& chunks){
    VoxelBytes out;out.byte('V');out.byte('R');out.byte(kVoxelRegionFormat);out.varint(chunks.size());
    for(const auto& [pos,bytes]:chunks){out.svarint(pos.x);out.svarint(pos.y);out.svarint(pos.z);out.varint(bytes.size());out.append(bytes);}
    VoxelHasher h;for(auto b:out.data)h.byte(b);for(int i=0;i<8;++i)out.byte(static_cast<std::uint8_t>(h.h>>(8*i)));
    return std::move(out.data);
}
// Returns encoded chunks (still compressed); every chunk is validated with decodeChunk.
inline std::optional<std::map<ChunkPos,std::vector<std::uint8_t>>> decodeRegion(const std::vector<std::uint8_t>& data,std::size_t blockTypes,std::size_t maxChunks=1<<16){
    if(data.size()<11)return {};
    VoxelHasher h;for(std::size_t i=0;i<data.size()-8;++i)h.byte(data[i]);
    std::uint64_t stored=0;for(int i=0;i<8;++i)stored|=static_cast<std::uint64_t>(data[data.size()-8+static_cast<std::size_t>(i)])<<(8*i);
    if(stored!=h.h)return {};
    VoxelReader in(data.data(),data.size()-8);
    if(in.byte()!='V'||in.byte()!='R'||in.byte()!=kVoxelRegionFormat)return {};
    auto count=in.varint();if(!in.ok||count>maxChunks)return {};
    std::map<ChunkPos,std::vector<std::uint8_t>> out;
    for(std::uint64_t k=0;k<count;++k){
        auto x=in.svarint(),y=in.svarint(),z=in.svarint();auto len=in.varint();
        if(!in.ok||len>in.n-in.i||x< -kVoxelCoordinateLimit||x>kVoxelCoordinateLimit||y< -kVoxelCoordinateLimit||y>kVoxelCoordinateLimit||z< -kVoxelCoordinateLimit||z>kVoxelCoordinateLimit)return {};
        std::vector<std::uint8_t> bytes(in.p+in.i,in.p+in.i+len);in.i+=len;
        if(!decodeChunk(bytes,blockTypes))return {};
        ChunkPos pos{static_cast<std::int32_t>(x),static_cast<std::int32_t>(y),static_cast<std::int32_t>(z)};
        if(!out.emplace(pos,std::move(bytes)).second)return {};
    }
    if(!in.done())return {};
    return out;
}
}
