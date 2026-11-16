#pragma once
// voxel-world: block positions, block definitions, the block registry and one 16x16x16 chunk.
// Simulation layer: integers only. A chunk stores block ids, per-voxel metadata (fluid level, orientation)
// and derived light (sky << 4 | block, written by voxel-light, never hashed or saved).
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace aiverse::lib::systems {
using BlockId=std::uint16_t;
inline constexpr int kVoxelChunk=16;
inline constexpr int kVoxelChunkVolume=16*16*16;
inline constexpr BlockId kAir=0;
inline constexpr std::size_t kMaxBlockTypes=4096;
// World coordinates are limited so that chunk origins never overflow int32 arithmetic.
inline constexpr std::int32_t kVoxelCoordinateLimit=1<<27;

struct BlockPos{std::int32_t x=0,y=0,z=0;auto operator<=>(const BlockPos&)const=default;};
struct ChunkPos{std::int32_t x=0,y=0,z=0;auto operator<=>(const ChunkPos&)const=default;};
inline constexpr ChunkPos chunkOf(BlockPos p){return {p.x>>4,p.y>>4,p.z>>4};}
inline constexpr int voxelIndex(int x,int y,int z){return (y<<8)|(z<<4)|x;}
inline constexpr int voxelIndex(BlockPos p){return voxelIndex(p.x&15,p.y&15,p.z&15);}
inline constexpr BlockPos chunkOrigin(ChunkPos c){return {c.x*16,c.y*16,c.z*16};}
inline constexpr BlockPos offset(BlockPos p,int dx,int dy,int dz){return {p.x+dx,p.y+dy,p.z+dz};}
// Faces in a fixed order: -X +X -Y +Y -Z +Z. Every loop over neighbours uses this order (determinism).
inline constexpr int kFaceDir[6][3]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
inline constexpr BlockPos neighbour(BlockPos p,int face){return offset(p,kFaceDir[face][0],kFaceDir[face][1],kFaceDir[face][2]);}

enum class BlockShape:std::uint8_t{None,Cube,Cross,Fluid};
enum class RenderLayer:std::uint8_t{None,Opaque,Cutout,Translucent};
struct BlockDef{
    BlockId id=kAir;std::string name;
    bool solid=true;          // collides with bodies and stops rays of "solid" queries
    bool replaceable=false;   // placement and flowing fluids may overwrite it (air, tall grass, fluids)
    BlockShape shape=BlockShape::Cube;RenderLayer layer=RenderLayer::Opaque;
    std::uint8_t opacity=15;  // light reduction 0..15; 15 blocks light completely
    std::uint8_t emission=0;  // block light 0..15
    std::uint32_t breakTicks=20;bool unbreakable=false;
    BlockId drop=kAir;bool dropsSelf=true;std::uint32_t dropCount=1;
    std::uint8_t fluid=0;     // fluid group 1..15, 0 = not a fluid
    std::uint8_t fluidSpread=0;std::uint32_t fluidTicks=5;bool infiniteSource=false;
    bool falls=false;         // sand / gravel
    BlockId growInto=kAir;std::uint32_t growChance=0; // per 4096 random ticks
    std::array<std::uint16_t,6> textures{};            // texture layer per face, -X +X -Y +Y -Z +Z
};
inline BlockDef voxelAirDef(){BlockDef a;a.name="air";a.solid=false;a.replaceable=true;a.shape=BlockShape::None;a.layer=RenderLayer::None;a.opacity=0;a.breakTicks=0;a.dropsSelf=false;a.dropCount=0;return a;}
inline bool occludes(const BlockDef& d){return d.shape==BlockShape::Cube&&d.layer==RenderLayer::Opaque;}

struct BlockRegistry{
    std::vector<BlockDef> defs{voxelAirDef()}; // index == id; id 0 is air
    bool has(BlockId id)const{return id<defs.size();}
    const BlockDef& operator[](BlockId id)const{return id<defs.size()?defs[id]:defs[0];}
    std::optional<BlockId> find(std::string_view name)const{for(const auto& d:defs)if(d.name==name)return d.id;return {};}
    // Appends a definition with the next id; returns that id or nullopt when full or the name repeats.
    std::optional<BlockId> add(BlockDef d){if(defs.size()>=kMaxBlockTypes||d.name.empty()||find(d.name))return {};d.id=static_cast<BlockId>(defs.size());defs.push_back(std::move(d));return defs.back().id;}
};
inline std::optional<std::string> validateRegistry(const BlockRegistry& r){
    if(r.defs.empty()||r.defs.size()>kMaxBlockTypes)return "blocks: 1.."+std::to_string(kMaxBlockTypes)+" types";
    if(r.defs[0].name!="air"||r.defs[0].solid||r.defs[0].shape!=BlockShape::None)return "blocks[0]: must be air";
    for(std::size_t i=0;i<r.defs.size();++i){
        const auto& d=r.defs[i];auto at="blocks["+std::to_string(i)+"]";
        if(d.id!=i)return at+".id: must equal its index";
        if(d.name.empty())return at+".name: required";
        for(std::size_t j=0;j<i;++j)if(r.defs[j].name==d.name)return at+".name: duplicate '"+d.name+"'";
        if(d.opacity>15||d.emission>15)return at+": opacity and emission are 0..15";
        if(d.fluid>15||(d.fluid&&d.fluidSpread>7)||(d.fluid&&d.fluidTicks==0))return at+".fluid: group 0..15, spread 0..7, ticks > 0";
        if(d.fluid&&d.shape!=BlockShape::Fluid)return at+".shape: a fluid must use shape fluid";
        if(!r.has(d.drop)||!r.has(d.growInto))return at+": drop / growInto must name a known block";
        if(d.growChance>4096)return at+".growChance: 0..4096";
    }
    return {};
}

struct VoxelChunk{
    std::array<BlockId,kVoxelChunkVolume> blocks{};
    std::array<std::uint8_t,kVoxelChunkVolume> meta{};
    std::array<std::uint8_t,kVoxelChunkVolume> light{}; // derived: sky << 4 | block
    std::uint32_t revision=0;  // bumped by every simulated edit; replication compares it
    std::uint16_t nonAir=0;
    bool fullyAir()const{return nonAir==0;}
};
inline std::uint16_t countNonAir(const VoxelChunk& c){std::uint16_t n=0;for(auto b:c.blocks)n=static_cast<std::uint16_t>(n+(b!=kAir));return n;}
inline std::uint8_t skyLight(std::uint8_t l){return static_cast<std::uint8_t>(l>>4);}
inline std::uint8_t blockLight(std::uint8_t l){return static_cast<std::uint8_t>(l&15);}

struct BlockEdit{BlockPos pos;BlockId block=kAir;std::uint8_t meta=0;std::uint32_t revision=0;std::uint64_t seq=0;};

// FNV-1a 64 used for hashes and region checksums.
struct VoxelHasher{
    std::uint64_t h=1469598103934665603ull;
    void byte(std::uint8_t b){h^=b;h*=1099511628211ull;}
    void u32(std::uint32_t v){for(int i=0;i<4;++i)byte(static_cast<std::uint8_t>(v>>(8*i)));}
    void i32(std::int32_t v){u32(static_cast<std::uint32_t>(v));}
    void u64(std::uint64_t v){for(int i=0;i<8;++i)byte(static_cast<std::uint8_t>(v>>(8*i)));}
};
}
