#pragma once
// voxel-gen: deterministic chunk generator for infinite block worlds. Integer value noise (16.16) with fractal
// octaves for the height field, biome temperature, 3D caves; ores as random-walk veins; trees and plants on the
// surface. Every chunk is generated on its own from (seed, chunk position): features that cross chunk borders
// (trees, ore veins) are produced by every chunk that overlaps them from the same per-origin random stream, so
// any generation order gives identical chunks on every platform. Only engine Random (PCG32) and integers.
#include "aiverselib/systems/voxel/VoxelTypes.h"
#include "engine/core/Random.h"
#include <algorithm>
namespace aiverse::lib::systems {
inline std::uint32_t voxelHash(std::uint64_t seed,std::int64_t x,std::int64_t y,std::int64_t z,std::uint64_t salt=0){
    std::uint64_t h=seed^(salt*0x9E3779B97F4A7C15ull);
    for(auto v:{static_cast<std::uint64_t>(x),static_cast<std::uint64_t>(y),static_cast<std::uint64_t>(z)}){h^=v+0x9E3779B97F4A7C15ull+(h<<6)+(h>>2);h*=0xBF58476D1CE4E5B9ull;h^=h>>31;}
    return static_cast<std::uint32_t>(h>>32);
}
inline std::int64_t voxelFloorDiv(std::int64_t a,std::int64_t b){auto q=a/b;if((a%b!=0)&&((a<0)!=(b<0)))--q;return q;}
inline std::int64_t voxelFade(std::int64_t t){return t*t/65536*(3*65536-2*t)/65536;} // smoothstep in 16.16
inline std::int64_t voxelLerp(std::int64_t a,std::int64_t b,std::int64_t t){return a+(b-a)*t/65536;}
// Value noise in [0, 65536) at integer coordinates with a cell size of `scale` blocks.
inline std::int64_t voxelNoise2(std::uint64_t seed,std::int64_t x,std::int64_t z,std::int64_t scale,std::uint64_t salt){
    auto cx=voxelFloorDiv(x,scale),cz=voxelFloorDiv(z,scale);auto fx=voxelFade((x-cx*scale)*65536/scale),fz=voxelFade((z-cz*scale)*65536/scale);
    auto v=[&](std::int64_t a,std::int64_t b){return static_cast<std::int64_t>(voxelHash(seed,a,0,b,salt)>>16);};
    return voxelLerp(voxelLerp(v(cx,cz),v(cx+1,cz),fx),voxelLerp(v(cx,cz+1),v(cx+1,cz+1),fx),fz);
}
inline std::int64_t voxelNoise3(std::uint64_t seed,std::int64_t x,std::int64_t y,std::int64_t z,std::int64_t scale,std::uint64_t salt){
    auto cx=voxelFloorDiv(x,scale),cy=voxelFloorDiv(y,scale),cz=voxelFloorDiv(z,scale);
    auto fx=voxelFade((x-cx*scale)*65536/scale),fy=voxelFade((y-cy*scale)*65536/scale),fz=voxelFade((z-cz*scale)*65536/scale);
    auto v=[&](std::int64_t a,std::int64_t b,std::int64_t c){return static_cast<std::int64_t>(voxelHash(seed,a,b,c,salt)>>16);};
    auto plane=[&](std::int64_t yy){return voxelLerp(voxelLerp(v(cx,yy,cz),v(cx+1,yy,cz),fx),voxelLerp(v(cx,yy,cz+1),v(cx+1,yy,cz+1),fx),fz);};
    return voxelLerp(plane(cy),plane(cy+1),fy);
}
// Fractal sum of octaves, normalised back to [0, 65536).
inline std::int64_t voxelFractal2(std::uint64_t seed,std::int64_t x,std::int64_t z,std::int64_t scale,int octaves,std::uint64_t salt){
    std::int64_t sum=0,amp=65536,total=0;for(int o=0;o<octaves&&scale>=1;++o){sum+=voxelNoise2(seed,x,z,scale,salt+static_cast<std::uint64_t>(o)*7919)*amp/65536;total+=amp;amp/=2;scale=std::max<std::int64_t>(1,scale/2);}
    return total?sum*65536/total:0;
}
struct VoxelOre{BlockId block=kAir,replaces=kAir;std::int32_t minY=0,maxY=64;std::uint32_t veinsPerChunk=8,veinSize=8;};
struct VoxelBiome{std::string name;std::uint32_t upTo=1000;BlockId surface=kAir,filler=kAir,shore=kAir;std::uint32_t fillerDepth=3;
    std::uint32_t treesPerChunk=0;BlockId log=kAir,leaves=kAir;std::uint32_t plantsPerChunk=0;BlockId plant=kAir;std::int32_t heightOffset=0;};
struct VoxelGenConfig{
    std::uint64_t seed=1;std::int32_t seaLevel=62,baseHeight=64,heightAmplitude=24,heightScale=128,bedrockY=-64;std::uint32_t octaves=5;
    BlockId stone=kAir,water=kAir,bedrock=kAir;
    bool caves=true;std::int32_t caveScale=24,caveMinY=-60,caveMaxY=120;std::uint32_t caveThreshold=40000; // of 65536
    std::int32_t biomeScale=512;std::vector<VoxelBiome> biomes;std::vector<VoxelOre> ores;
    std::uint32_t treeMinHeight=4,treeMaxHeight=6;
};
inline std::optional<std::string> validateGen(const VoxelGenConfig& c,const BlockRegistry& r){
    if(c.biomes.empty())return "voxelGen.biomes: at least one";
    if(c.heightScale<1||c.caveScale<1||c.biomeScale<1||c.octaves<1||c.octaves>8)return "voxelGen: scales >= 1, octaves 1..8";
    if(c.heightAmplitude<0||c.treeMinHeight<1||c.treeMaxHeight<c.treeMinHeight||c.treeMaxHeight>12)return "voxelGen: heightAmplitude >= 0, tree heights 1..12";
    for(auto b:{c.stone,c.water,c.bedrock})if(!r.has(b))return "voxelGen: unknown block id";
    std::uint32_t last=0;for(std::size_t i=0;i<c.biomes.size();++i){const auto& b=c.biomes[i];if(b.upTo<=last&&i)return "voxelGen.biomes["+std::to_string(i)+"].upTo: increasing";last=b.upTo;
        for(auto id:{b.surface,b.filler,b.shore,b.log,b.leaves,b.plant})if(!r.has(id))return "voxelGen.biomes["+std::to_string(i)+"]: unknown block";
        if(b.treesPerChunk>32||b.plantsPerChunk>256)return "voxelGen.biomes["+std::to_string(i)+"]: treesPerChunk <= 32, plantsPerChunk <= 256";}
    if(c.biomes.back().upTo<1000)return "voxelGen.biomes: the last upTo must be 1000";
    for(std::size_t i=0;i<c.ores.size();++i){const auto& o=c.ores[i];if(!r.has(o.block)||!r.has(o.replaces)||o.minY>o.maxY||o.veinsPerChunk>64||o.veinSize>64)return "voxelGen.ores["+std::to_string(i)+"]: known blocks, minY <= maxY, veins and size <= 64";}
    return {};
}
class VoxelGenerator{
public:
    VoxelGenerator(VoxelGenConfig config):c_(std::move(config)){}
    const VoxelGenConfig& config()const{return c_;}
    const VoxelBiome& biomeAt(std::int64_t x,std::int64_t z)const{
        auto t=voxelFractal2(c_.seed,x,z,c_.biomeScale,2,101)*1000/65536;for(const auto& b:c_.biomes)if(t<b.upTo)return b;return c_.biomes.back();}
    // Surface height (the top solid block) of a column.
    std::int32_t heightAt(std::int64_t x,std::int64_t z)const{
        // Summed value noise clusters around the middle: stretch x3 so heightAmplitude is close to the real range.
        auto n=std::clamp<std::int64_t>((voxelFractal2(c_.seed,x,z,c_.heightScale,static_cast<int>(c_.octaves),1)-32768)*3,-32768,32768);
        return static_cast<std::int32_t>(c_.baseHeight+biomeAt(x,z).heightOffset+n*c_.heightAmplitude/32768);}
    bool caveAt(std::int64_t x,std::int64_t y,std::int64_t z,std::int32_t surface)const{
        if(!c_.caves||y<c_.caveMinY||y>c_.caveMaxY||y>surface-2||y<=c_.bedrockY+1)return false;
        auto a=voxelNoise3(c_.seed,x,y,z,c_.caveScale,201),b=voxelNoise3(c_.seed,x,y*2,z,c_.caveScale,307);
        // Worm-like tunnels: both fields near their middle at once.
        auto da=a>32768?a-32768:32768-a,db=b>32768?b-32768:32768-b;
        return da+db<static_cast<std::int64_t>(65536-c_.caveThreshold)/4; // threshold 40000: about 5% of the rock
    }
    VoxelChunk generate(ChunkPos cp)const{
        VoxelChunk out;auto o=chunkOrigin(cp);
        std::array<std::int32_t,256> heights{};std::array<const VoxelBiome*,256> biome{};
        for(int z=0;z<16;++z)for(int x=0;x<16;++x){heights[static_cast<std::size_t>(z*16+x)]=heightAt(o.x+x,o.z+z);biome[static_cast<std::size_t>(z*16+x)]=&biomeAt(o.x+x,o.z+z);}
        for(int y=0;y<16;++y)for(int z=0;z<16;++z)for(int x=0;x<16;++x){
            std::int64_t wy=o.y+y;auto h=heights[static_cast<std::size_t>(z*16+x)];const auto& b=*biome[static_cast<std::size_t>(z*16+x)];
            BlockId id=kAir;bool underwater=h<c_.seaLevel;
            if(wy<=c_.bedrockY)id=c_.bedrock;
            else if(wy<=h-static_cast<std::int64_t>(b.fillerDepth))id=c_.stone;
            else if(wy<h)id=underwater||h<=c_.seaLevel+1?(b.shore?b.shore:b.filler):b.filler;
            else if(wy==h)id=underwater||h<=c_.seaLevel+1?(b.shore?b.shore:b.filler):b.surface;
            else if(wy<=c_.seaLevel)id=c_.water;
            if(id!=kAir&&id!=c_.water&&id!=c_.bedrock&&caveAt(o.x+x,wy,o.z+z,h))id=kAir;
            out.blocks[static_cast<std::size_t>(voxelIndex(x,y,z))]=id;
        }
        ores(cp,out);trees(cp,out);plants(cp,out,heights,biome);
        out.nonAir=countNonAir(out);return out;
    }
private:
    static bool inside(ChunkPos cp,std::int64_t x,std::int64_t y,std::int64_t z){return (x>>4)==cp.x&&(y>>4)==cp.y&&(z>>4)==cp.z;}
    static BlockId& at(VoxelChunk& c,std::int64_t x,std::int64_t y,std::int64_t z){return c.blocks[static_cast<std::size_t>(voxelIndex(static_cast<int>(x&15),static_cast<int>(y&15),static_cast<int>(z&15)))];}
    void ores(ChunkPos cp,VoxelChunk& out)const{
        for(std::size_t k=0;k<c_.ores.size();++k){const auto& ore=c_.ores[k];
            for(int dy=-1;dy<=1;++dy)for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){
                ChunkPos origin{cp.x+dx,cp.y+dy,cp.z+dz};auto oy=static_cast<std::int64_t>(origin.y)*16;if(oy+15<ore.minY||oy>ore.maxY)continue;
                Random rng(voxelHash(c_.seed,origin.x,origin.y,origin.z,1000+k),k+1);
                for(std::uint32_t v=0;v<ore.veinsPerChunk;++v){
                    std::int64_t x=origin.x*16+rng.below(16),y=oy+rng.below(16),z=origin.z*16+rng.below(16);
                    for(std::uint32_t s=0;s<ore.veinSize;++s){
                        if(y>=ore.minY&&y<=ore.maxY&&inside(cp,x,y,z)&&at(out,x,y,z)==ore.replaces)at(out,x,y,z)=ore.block;
                        switch(rng.below(6)){case 0:++x;break;case 1:--x;break;case 2:++y;break;case 3:--y;break;case 4:++z;break;default:--z;}
                    }}}}
    }
    void trees(ChunkPos cp,VoxelChunk& out)const{
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){
            std::int32_t ox=cp.x+dx,oz=cp.z+dz;Random rng(voxelHash(c_.seed,ox,0,oz,2000),7);
            for(std::uint32_t t=0;t<32;++t){
                std::int64_t x=ox*16+rng.below(16),z=oz*16+rng.below(16);auto height=c_.treeMinHeight+rng.below(c_.treeMaxHeight-c_.treeMinHeight+1);
                const auto& b=biomeAt(x,z);if(t>=b.treesPerChunk||!b.log)continue;
                auto ground=heightAt(x,z);if(ground<=c_.seaLevel+1)continue;
                if(caveAt(x,ground,z,ground))continue;
                std::int64_t top=ground+static_cast<std::int64_t>(height);
                for(std::int64_t y=top-2;y<=top+1;++y){int rad=y>=top?1:2;
                    for(int lz=-rad;lz<=rad;++lz)for(int lx=-rad;lx<=rad;++lx){
                        if(rad==2&&std::abs(lx)==2&&std::abs(lz)==2)continue;
                        auto px=x+lx,pz=z+lz;if(b.leaves&&inside(cp,px,y,pz)&&at(out,px,y,pz)==kAir)at(out,px,y,pz)=b.leaves;}}
                for(std::int64_t y=ground+1;y<=top;++y)if(inside(cp,x,y,z)){auto& cell=at(out,x,y,z);if(cell==kAir||cell==b.leaves)cell=b.log;}
                if(inside(cp,x,ground,z)&&at(out,x,ground,z)==b.surface&&b.filler)at(out,x,ground,z)=b.filler;
            }}
    }
    void plants(ChunkPos cp,VoxelChunk& out,const std::array<std::int32_t,256>& h,const std::array<const VoxelBiome*,256>& biome)const{
        Random rng(voxelHash(c_.seed,cp.x,cp.y,cp.z,3000),11);auto o=chunkOrigin(cp);
        for(std::uint32_t k=0;k<64;++k){int x=static_cast<int>(rng.below(16)),z=static_cast<int>(rng.below(16));const auto& b=*biome[static_cast<std::size_t>(z*16+x)];
            if(k>=b.plantsPerChunk||!b.plant)continue;auto ground=h[static_cast<std::size_t>(z*16+x)];std::int64_t y=ground+1;
            if(ground<=c_.seaLevel||!inside(cp,o.x+x,y,o.z+z)||at(out,o.x+x,y,o.z+z)!=kAir)continue;
            if(ground>=o.y&&ground<o.y+16&&at(out,o.x+x,ground,o.z+z)!=b.surface)continue;
            if(ground<o.y&&caveAt(o.x+x,ground,o.z+z,ground))continue;
            at(out,o.x+x,y,o.z+z)=b.plant;}
    }
    VoxelGenConfig c_;
};
}
