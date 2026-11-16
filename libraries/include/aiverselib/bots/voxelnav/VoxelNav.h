#pragma once
// voxel-nav: A* over block cells for mobs in a mutable VoxelWorld - walk, step up `maxStep` blocks, drop up to
// `maxDrop`, swim through fluids, optional diagonals without corner cutting - and spawn spot search by light
// level. Deterministic (integer costs, ties by position), bounded by a node budget; no prebuilt navmesh, so
// edits to the world are seen by the next query. A path is a list of feet cells from start to goal.
#include "aiverselib/systems/voxel/VoxelWorld.h"
#include "engine/core/Random.h"
#include <tuple>
namespace aiverse::lib::bots {
using systems::BlockPos;using systems::VoxelWorld;using systems::BlockRegistry;using systems::BlockId;
struct VoxelAgent{int height=2,maxStep=1,maxDrop=3;bool swim=true,diagonal=false,avoidFluidSurface=false;std::uint32_t maxNodes=4096;std::uint32_t avoidDamage=0;};
inline bool voxelPassable(const VoxelWorld& w,const BlockRegistry& r,BlockPos p){auto b=w.block(p);return b&&!r[*b].solid;}
inline bool voxelFluid(const VoxelWorld& w,const BlockRegistry& r,BlockPos p){auto b=w.block(p);return b&&r[*b].fluid;}
// Feet at p: the body (height blocks) is free, and there is ground below or the feet are in a swimmable fluid.
inline bool voxelStandable(const VoxelWorld& w,const BlockRegistry& r,const VoxelAgent& a,BlockPos p){
    for(int y=0;y<a.height;++y)if(!voxelPassable(w,r,systems::offset(p,0,y,0)))return false;
    if(a.swim&&voxelFluid(w,r,p))return true;
    auto below=w.block(systems::offset(p,0,-1,0));return below&&r[*below].solid;
}
struct VoxelPathResult{std::vector<BlockPos> path;std::uint32_t expanded=0;bool budgetHit=false;};
// Finds a path from feet cell `start` to `goal` (or to any standable cell within `goalRadius` in Manhattan blocks).
inline std::optional<VoxelPathResult> voxelPath(const VoxelWorld& w,const BlockRegistry& r,const VoxelAgent& a,BlockPos start,BlockPos goal,int goalRadius=0){
    if(!voxelStandable(w,r,a,start))return {};
    auto h=[&](BlockPos p){return 10*(std::abs(p.x-goal.x)+std::abs(p.z-goal.z))+8*std::abs(p.y-goal.y);};
    auto reached=[&](BlockPos p){return std::abs(p.x-goal.x)+std::abs(p.y-goal.y)+std::abs(p.z-goal.z)<=goalRadius;};
    std::set<std::tuple<int,int,BlockPos>> open;std::map<BlockPos,std::pair<int,BlockPos>> best; // g, parent
    open.insert({h(start),0,start});best[start]={0,start};VoxelPathResult out;
    auto finish=[&](BlockPos p){std::vector<BlockPos> path{p};while(!(path.back()==start))path.push_back(best[path.back()].second);std::reverse(path.begin(),path.end());out.path=std::move(path);return out;};
    while(!open.empty()){
        auto [f,g,p]=*open.begin();open.erase(open.begin());(void)f;
        if(best[p].first<g)continue;
        if(reached(p))return finish(p);
        if(++out.expanded>a.maxNodes){out.budgetHit=true;return {};}
        static constexpr int dirs[8][2]={{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1}};
        bool inFluid=voxelFluid(w,r,p);
        auto relax=[&](BlockPos n,int cost){int ng=g+cost;auto it=best.find(n);if(it!=best.end()&&it->second.first<=ng)return;best[n]={ng,p};open.insert({ng+h(n),ng,n});};
        for(int d=0;d<(a.diagonal?8:4);++d){int dx=dirs[d][0],dz=dirs[d][1];
            if(d>=4&&(!voxelPassable(w,r,systems::offset(p,dx,0,0))||!voxelPassable(w,r,systems::offset(p,0,0,dz))||!voxelPassable(w,r,systems::offset(p,dx,1,0))||!voxelPassable(w,r,systems::offset(p,0,1,dz))))continue;
            int step=d>=4?14:10;
            for(int dy=a.maxStep;dy>=-a.maxDrop;--dy){BlockPos n=systems::offset(p,dx,dy,dz);
                if(!voxelStandable(w,r,a,n))continue;
                // Climbing needs head room above the current cell; dropping needs the column in front free.
                bool clear=true;if(dy>0){for(int y=a.height;y<a.height+dy&&clear;++y)clear=voxelPassable(w,r,systems::offset(p,0,y,0));}
                else for(int y=0;y>dy&&clear;--y)clear=voxelPassable(w,r,systems::offset(p,dx,y,dz))&&voxelPassable(w,r,systems::offset(p,dx,y+a.height-1,dz));
                if(!clear)continue;
                int cost=step+(dy>0?5*dy:dy<0?2*(-dy):0)+(voxelFluid(w,r,n)?10:0);relax(n,cost);break;}
        }
        if(a.swim&&inFluid){for(int dy:{1,-1}){auto n=systems::offset(p,0,dy,0);if(voxelStandable(w,r,a,n))relax(n,12);}}
    }
    return {};
}
// Spawn spots near `center`: standable, not in fluid, light level <= maxLight (sky reduced by skyDarkening,
// e.g. 11 at night), solid ground of an allowed block, at least minDistance (blocks, squared test) from center.
struct VoxelSpawnRule{std::uint8_t maxLight=7,skyDarkening=0;int radius=24,minDistance=8,verticalRange=16;std::uint32_t attempts=64;std::vector<BlockId> ground;};
inline std::vector<BlockPos> voxelSpawnSpots(const VoxelWorld& w,const BlockRegistry& r,const VoxelAgent& a,const VoxelSpawnRule& rule,BlockPos center,std::size_t count,Random& rng){
    std::vector<BlockPos> out;
    for(std::uint32_t k=0;k<rule.attempts&&out.size()<count;++k){
        int dx=rng.range(-rule.radius,rule.radius),dz=rng.range(-rule.radius,rule.radius),dy=rng.range(-rule.verticalRange,rule.verticalRange);
        if(dx*dx+dz*dz<rule.minDistance*rule.minDistance)continue;
        BlockPos p=systems::offset(center,dx,dy,dz);
        for(int s=0;s<rule.verticalRange&&!voxelStandable(w,r,a,p);++s)p=systems::offset(p,0,-1,0);
        if(!voxelStandable(w,r,a,p)||voxelFluid(w,r,p))continue;
        auto ground=w.block(systems::offset(p,0,-1,0));if(!ground)continue;
        if(!rule.ground.empty()&&std::find(rule.ground.begin(),rule.ground.end(),*ground)==rule.ground.end())continue;
        auto l=w.light(p);int sky=systems::skyLight(l)-rule.skyDarkening;int level=std::max({0,sky,int(systems::blockLight(l))});
        if(level>rule.maxLight)continue;
        if(std::find(out.begin(),out.end(),p)!=out.end())continue;
        out.push_back(p);
    }
    return out;
}
}
