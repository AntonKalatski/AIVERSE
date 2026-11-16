// aiverse_voxel: headless check and benchmark of the voxel packages on real data.
//   aiverse_voxel [--content examples/libraries/voxel] [--radius 6] [--edits 2000] [--verify]
// Generates whole columns around the origin, lights and meshes them, applies random edits with relight and
// re-mesh, runs fluids, saves regions and NativeState, restores and compares hashes, streams to a mirror.
// Prints one JSON line with timings (ms) and sizes. --verify exits non-zero when a check fails.
#include <aiverselib/systems/voxel/VoxelData.h>
#include <aiverselib/systems/voxel/VoxelPersistence.h>
#include <aiverselib/systems/voxel/VoxelQuery.h>
#include <aiverselib/systems/voxellight/VoxelLight.h>
#include <aiverselib/systems/voxelmesh/VoxelMesh.h>
#include <aiverselib/systems/voxelgen/VoxelGenData.h>
#include <aiverselib/systems/voxelticks/VoxelTicksData.h>
#include <aiverselib/bots/voxelnav/VoxelNav.h>
#include <aiverselib/net/voxel/VoxelReplication.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
using namespace aiverse;using namespace aiverse::lib;using namespace aiverse::lib::systems;
namespace {
double ms(std::chrono::steady_clock::time_point a){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-a).count();}
}
int main(int argc,char** argv){
    std::string content="examples/libraries/voxel";int radius=6,editCount=2000;bool verify=false;
    for(int i=1;i<argc;++i){std::string a=argv[i];
        if(a=="--content"&&i+1<argc)content=argv[++i];else if(a=="--radius"&&i+1<argc)radius=std::atoi(argv[++i]);
        else if(a=="--edits"&&i+1<argc)editCount=std::atoi(argv[++i]);else if(a=="--verify")verify=true;
        else{std::fprintf(stderr,"usage: aiverse_voxel [--content DIR] [--radius N] [--edits N] [--verify]\n");return 2;}}
    try{
        auto r=readBlockRegistry(Json::parseFile(content+"/blocks.json"));
        VoxelGenerator gen(readVoxelGen(Json::parseFile(content+"/worldgen.json"),r));
        auto rules=readVoxelTicks(Json::parseFile(content+"/ticks.json"),r);
        VoxelBounds bounds;bounds.minChunkY=-4;bounds.maxChunkY=7;VoxelWorld w(bounds);
        std::vector<ChunkPos> cols;for(int x=-radius;x<=radius;++x)for(int z=-radius;z<=radius;++z)if(x*x+z*z<=radius*radius)for(int y=bounds.maxChunkY;y>=bounds.minChunkY;--y)cols.push_back({x,y,z});
        auto t=std::chrono::steady_clock::now();for(auto c:cols)w.insert(c,gen.generate(c));double genMs=ms(t);
        t=std::chrono::steady_clock::now();lightPending(w,r,cols.size());double lightMs=ms(t);
        t=std::chrono::steady_clock::now();std::size_t quads=0,vertices=0,nonEmpty=0;
        for(auto c:cols){auto m=meshChunk(w,r,c);for(const auto& L:m.layers){quads+=L.quads();vertices+=L.vertices.size();}nonEmpty+=!m.empty();}
        double meshMs=ms(t);w.renderDirty.clear();
        // Random edits at the surface: dig or place, relight, re-mesh what became dirty.
        Random rng(99);std::size_t changed=0;t=std::chrono::steady_clock::now();
        for(int i=0;i<editCount;++i){int x=rng.range(-radius*12,radius*12),z=rng.range(-radius*12,radius*12);int y=gen.heightAt(x,z)+static_cast<int>(rng.below(3))-1;
            BlockId to=rng.below(4)==0?*r.find("torch"):rng.below(2)?kAir:*r.find("planks");auto old=w.set({x,y,z},to,0,r);if(old){relightBlock(w,r,{x,y,z},*old);++changed;}}
        double editMs=ms(t);auto dirty=w.renderDirty.size();t=std::chrono::steady_clock::now();for(auto c:w.renderDirty)(void)meshChunk(w,r,c);double remeshMs=ms(t);w.renderDirty.clear();
        // Fluids: a lake poured onto the surface.
        VoxelTickState ts;Random trng(5);for(int i=0;i<8;++i){BlockPos p{i*3,gen.heightAt(i*3,0)+1,0};auto old=w.set(p,*r.find("water"),0,r);if(old){relightBlock(w,r,p,*old);notifyVoxelChange(ts,w,r,rules,p,0);}}
        t=std::chrono::steady_clock::now();std::size_t tickUpdates=0;for(std::uint64_t k=1;k<=200;++k){auto rep=stepVoxelTicks(rules,ts,w,r,k,trng);if(!rep)return 3;tickUpdates+=rep->processed;for(auto c:rep->changed)relightBlock(w,r,c.pos,c.oldBlock);}double tickMs=ms(t);
        // Saves: regions and NativeState, then restore and compare.
        t=std::chrono::steady_clock::now();auto regions=w.exportRegions();std::size_t regionBytes=0;for(auto& [p,b]:regions){(void)p;regionBytes+=b.size();}
        game::NativeState st;storeVoxelWorld(st,"voxel",w);auto saved=loadVoxelWorld(st,"voxel",r);bool restoreOk=false;
        if(saved){for(auto c:saved->resident)saved->world.insert(c,saved->world.parked(c)?*saved->world.unpark(c,r):gen.generate(c));restoreOk=saved->world.hash()==w.hash();}
        double saveMs=ms(t);
        // Replication of a 4-chunk view to an empty mirror.
        VoxelWorld mirror(bounds);net::VoxelPeer peer;net::VoxelView view{{0,4,0},4,8,3};net::VoxelPumpConfig cfg;cfg.maxChunks=1<<20;cfg.maxBytes=1u<<30;net::VoxelPumpStats ps;
        t=std::chrono::steady_clock::now();auto msgs=net::pumpVoxelPeer(cfg,w,view,peer,{},&ps);for(const auto& m:msgs)net::applyVoxelMessage(mirror,r,m);double replMs=ms(t);
        bool mirrorOk=true;for(const auto& [c,chunk]:mirror.chunks())mirrorOk&=chunk.blocks==w.chunk(c)->blocks;
        // Mob path across the terrain.
        bots::VoxelAgent mob;mob.maxNodes=20000;
        auto stand=[&](int x,int z){BlockPos p{x,gen.heightAt(x,z)+1,z};for(int k=0;k<12&&!bots::voxelStandable(w,r,mob,p);++k)p.y++;return p;};
        BlockPos a=stand(0,0),b=stand(24,8);
        t=std::chrono::steady_clock::now();auto path=bots::voxelPath(w,r,mob,a,b,2);double pathMs=ms(t);
        std::printf("{\"chunks\":%zu,\"generateMs\":%.1f,\"lightMs\":%.1f,\"meshMs\":%.1f,\"meshMsPerChunk\":%.3f,\"nonEmptyChunks\":%zu,\"quads\":%zu,\"vertexBytes\":%zu,"
            "\"edits\":%zu,\"editRelightMs\":%.1f,\"dirtyChunks\":%zu,\"remeshMs\":%.1f,\"fluidUpdates\":%zu,\"fluidMs\":%.1f,\"regions\":%zu,\"regionBytes\":%zu,\"saveRestoreMs\":%.1f,"
            "\"restoreHashEqual\":%s,\"replicatedChunks\":%zu,\"replicatedBytes\":%zu,\"replicateMs\":%.1f,\"mirrorEqual\":%s,\"path\":%zu,\"pathMs\":%.2f,\"hash\":\"%llu\"}\n",
            cols.size(),genMs,lightMs,meshMs,meshMs/static_cast<double>(cols.size()),nonEmpty,quads,vertices*sizeof(VoxelVertex),changed,editMs,dirty,remeshMs,tickUpdates,tickMs,regions.size(),regionBytes,saveMs,
            restoreOk?"true":"false",ps.chunks,ps.bytes,replMs,mirrorOk?"true":"false",path?path->path.size():0,pathMs,static_cast<unsigned long long>(w.hash()));
        if(verify&&(!restoreOk||!mirrorOk||!path||quads==0||changed==0))return 1;
    }catch(const std::exception& e){std::fprintf(stderr,"aiverse_voxel: %s\n",e.what());return 1;}
    return 0;
}
