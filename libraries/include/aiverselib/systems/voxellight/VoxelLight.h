#pragma once
// voxel-light: Minecraft-style sky and block light (0..15 each) in VoxelChunk::light, by breadth-first flood fill.
// Sky light enters at full strength from the top of the world (or from the top of the highest resident chunk of a
// column when the chunk above is not resident: stream whole columns), falls straight down through opacity-0 blocks
// without loss and spreads sideways losing max(1, opacity) per block. Block light starts at emission and loses the
// same. Updates are incremental (removal pass + refill), bounded by the 15-block reach of light.
// Light is derived state: deterministic (fixed neighbour order, FIFO queues), never hashed or saved; recompute it
// after loading. Changing light marks the meshes that sample it dirty (VoxelWorld::renderDirty).
#include "aiverselib/systems/voxel/VoxelWorld.h"
#include <deque>
namespace aiverse::lib::systems {
namespace voxel_light_detail {
enum Channel{Sky=0,Block=1};
// Cached chunk access: the flood fill touches the same chunk thousands of times in a row. Writes go straight
// into the light array; meshes to rebuild are collected once per chunk (and per neighbour for border voxels).
struct Access{
    VoxelWorld& w;const BlockRegistry& r;
    ChunkPos lastPos{};VoxelChunk* last=nullptr;bool cached=false;
    std::set<ChunkPos> touched;ChunkPos lastTouched{};bool anyTouched=false;
    Access(VoxelWorld& world,const BlockRegistry& reg):w(world),r(reg){}
    ~Access(){for(auto c:touched)if(w.loaded(c))w.renderDirty.insert(c);}
    VoxelChunk* chunkAt(BlockPos p){if(!w.inBounds(p))return nullptr;auto c=chunkOf(p);if(!cached||!(c==lastPos)){last=w.chunk(c);lastPos=c;cached=true;}return last;}
    bool loaded(BlockPos p){return chunkAt(p)!=nullptr;}
    std::uint8_t get(BlockPos p,int ch){auto* c=chunkAt(p);if(!c)return 0;auto l=c->light[static_cast<std::size_t>(voxelIndex(p))];return ch==Sky?skyLight(l):blockLight(l);}
    std::uint8_t opacity(BlockPos p){auto* c=chunkAt(p);return c?r[c->blocks[static_cast<std::size_t>(voxelIndex(p))]].opacity:0;}
    std::uint8_t emission(BlockPos p){auto* c=chunkAt(p);return c?r[c->blocks[static_cast<std::size_t>(voxelIndex(p))]].emission:0;}
    void put(BlockPos p,int ch,std::uint8_t v){auto* c=chunkAt(p);if(!c)return;auto& l=c->light[static_cast<std::size_t>(voxelIndex(p))];
        auto nl=static_cast<std::uint8_t>(ch==Sky?((v<<4)|(l&15)):((l&0xf0)|v));if(nl==l)return;l=nl;
        auto cp=chunkOf(p);if(!anyTouched||!(cp==lastTouched)){touched.insert(cp);lastTouched=cp;anyTouched=true;}
        int lx=p.x&15,ly=p.y&15,lz=p.z&15;
        if(lx==0||lx==15||ly==0||ly==15||lz==0||lz==15)
            for(int dx=(lx==0?-1:0);dx<=(lx==15?1:0);++dx)for(int dy=(ly==0?-1:0);dy<=(ly==15?1:0);++dy)for(int dz=(lz==0?-1:0);dz<=(lz==15?1:0);++dz)
                if(dx||dy||dz)touched.insert({cp.x+dx,cp.y+dy,cp.z+dz});}
};
inline int spread(int level,int face,int ch,std::uint8_t opacity){
    if(ch==Sky&&face==2&&level==15&&opacity==0)return 15; // straight down, no loss
    return level-(opacity>1?opacity:1);
}
struct Stats{std::size_t visited=0;};
inline void fill(Access& A,std::deque<BlockPos>& q,int ch,Stats& s){
    while(!q.empty()){auto p=q.front();q.pop_front();++s.visited;int level=A.get(p,ch);if(level<=1)continue;
        for(int f=0;f<6;++f){auto n=neighbour(p,f);if(!A.loaded(n))continue;
            int next=spread(level,f,ch,A.opacity(n));if(next>A.get(n,ch)){A.put(n,ch,static_cast<std::uint8_t>(next));q.push_back(n);}}}
}
// Removes light that depended on `start` (its old level `old`), then refills from the remaining sources.
inline void remove(Access& A,BlockPos start,int old,int ch,Stats& s){
    std::deque<std::pair<BlockPos,int>> rem{{start,old}};std::deque<BlockPos> add;
    A.put(start,ch,0);
    while(!rem.empty()){auto [p,level]=rem.front();rem.pop_front();++s.visited;
        for(int f=0;f<6;++f){auto n=neighbour(p,f);if(!A.loaded(n))continue;int nl=A.get(n,ch);if(nl==0)continue;
            bool dependent=nl<level||(ch==Sky&&f==2&&level==15&&nl==15);
            if(dependent){A.put(n,ch,0);rem.push_back({n,nl});
                if(ch==Block){auto e=A.emission(n);if(e){A.put(n,ch,e);add.push_back(n);}}}
            else add.push_back(n);}}
    fill(A,add,ch,s);
}
inline std::uint8_t skyFromAbove(Access& A,BlockPos p){
    auto above=offset(p,0,1,0);if(chunkOf(above).y>A.w.bounds().maxChunkY||!A.loaded(above))return 15;return A.get(above,Sky);
}
}
struct VoxelLightStats{std::size_t visited=0;};
// Computes light for a newly resident chunk (and spills into resident neighbours). Call for every chunk in
// VoxelWorld::lightDirty after inserting it; the call removes it from that set.
inline VoxelLightStats lightChunk(VoxelWorld& w,const BlockRegistry& r,ChunkPos c){
    using namespace voxel_light_detail;Stats s;VoxelLightStats out;
    auto* chunk=w.chunk(c);if(!chunk)return out;
    chunk->light.fill(0);w.lightDirty.erase(c);w.renderDirty.insert(c);
    Access A(w,r);auto o=chunkOrigin(c);std::deque<BlockPos> sky,block;
    // Seed straight full-strength sky columns; everything dimmer arrives by the flood fill below. A voxel whose
    // six neighbours are all full sky cannot raise anything, so only column edges and bottoms are queued.
    std::array<std::int8_t,256> lowest{};lowest.fill(16); // lowest y of full sky per column (16 = none)
    for(int x=0;x<16;++x)for(int z=0;z<16;++z){
        if(skyFromAbove(A,BlockPos{o.x+x,o.y+15,o.z+z})!=15)continue;
        int y=15;for(;y>=0;--y){if(r[chunk->blocks[static_cast<std::size_t>(voxelIndex(x,y,z))]].opacity!=0)break;
            chunk->light[static_cast<std::size_t>(voxelIndex(x,y,z))]=0xf0;}
        lowest[static_cast<std::size_t>(z*16+x)]=static_cast<std::int8_t>(y+1);
    }
    for(int x=0;x<16;++x)for(int z=0;z<16;++z){int low=lowest[static_cast<std::size_t>(z*16+x)];if(low>15)continue;
        for(int y=low;y<=15;++y){bool edge=y==low||x==0||x==15||z==0||z==15;
            if(!edge)for(int f:{0,1,4,5}){int nx=x+kFaceDir[f][0],nz=z+kFaceDir[f][2];if(lowest[static_cast<std::size_t>(nz*16+nx)]>y){edge=true;break;}}
            if(edge)sky.push_back({o.x+x,o.y+y,o.z+z});}}
    A.touched.insert(c);for(int f=0;f<6;++f)A.touched.insert({c.x+kFaceDir[f][0],c.y+kFaceDir[f][1],c.z+kFaceDir[f][2]});
    for(int i=0;i<kVoxelChunkVolume;++i){auto e=r[chunk->blocks[static_cast<std::size_t>(i)]].emission;if(e){BlockPos p{o.x+(i&15),o.y+(i>>8),o.z+((i>>4)&15)};A.put(p,Block,e);block.push_back(p);}}
    // Light already in resident neighbours flows in through the shared faces.
    for(int f=0;f<6;++f){ChunkPos n{c.x+kFaceDir[f][0],c.y+kFaceDir[f][1],c.z+kFaceDir[f][2]};if(!w.loaded(n))continue;
        for(int a=0;a<16;++a)for(int b=0;b<16;++b){int lx=f==0?-1:f==1?16:a,ly=f==2?-1:f==3?16:(f<2?a:b),lz=f==4?-1:f==5?16:b;
            BlockPos p{o.x+lx,o.y+ly,o.z+lz};if(A.get(p,Sky)>1)sky.push_back(p);if(A.get(p,Block)>1)block.push_back(p);}}
    fill(A,sky,Sky,s);fill(A,block,Block,s);
    // The chunk below may have assumed open sky; columns this chunk now shades lose their full-strength light.
    ChunkPos below{c.x,c.y-1,c.z};
    if(w.loaded(below))for(int x=0;x<16;++x)for(int z=0;z<16;++z){BlockPos p{o.x+x,o.y-1,o.z+z};
        if(A.get(p,Sky)==15&&A.get(offset(p,0,1,0),Sky)<15)remove(A,p,15,Sky,s);}
    out.visited=s.visited;return out;
}
// Updates light after VoxelWorld::set changed `p` from `oldBlock` (call once per changed block).
inline VoxelLightStats relightBlock(VoxelWorld& w,const BlockRegistry& r,BlockPos p,BlockId oldBlock){
    using namespace voxel_light_detail;Stats s;VoxelLightStats out;(void)oldBlock;
    auto now=w.block(p);if(!now)return out;const auto& after=r[*now];Access A(w,r);
    for(int ch:{Sky,Block}){
        int cur=A.get(p,ch);
        if(cur>0)remove(A,p,cur,ch,s);
        std::deque<BlockPos> add;
        if(ch==Block&&after.emission){A.put(p,ch,after.emission);add.push_back(p);}
        for(int f=0;f<6;++f){auto n=neighbour(p,f);if(A.loaded(n)&&A.get(n,ch))add.push_back(n);}
        if(ch==Sky&&after.opacity==0&&skyFromAbove(A,p)==15){A.put(p,ch,15);add.push_back(p);}
        fill(A,add,ch,s);
    }
    out.visited=s.visited;return out;
}
// Lights every chunk waiting in VoxelWorld::lightDirty (in map order); bounded by maxChunks per call.
inline std::size_t lightPending(VoxelWorld& w,const BlockRegistry& r,std::size_t maxChunks=64){
    std::size_t n=0;while(!w.lightDirty.empty()&&n<maxChunks){lightChunk(w,r,*w.lightDirty.begin());++n;}return n;}
// Light level used by spawning and plants: max(sky - skyDarkening, block).
inline std::uint8_t voxelLightLevel(const VoxelWorld& w,BlockPos p,std::uint8_t skyDarkening=0){
    auto l=w.light(p);int sky=skyLight(l)-skyDarkening;int b=blockLight(l);return static_cast<std::uint8_t>(std::max({0,sky,b}));}
}
