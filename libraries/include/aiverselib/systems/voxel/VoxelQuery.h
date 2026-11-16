#pragma once
// voxel-world: deterministic queries in Fixed world units (1 unit = 1 block): ray to the first matching block
// (picking, line of sight) and axis-separated box movement against solid blocks (players, mobs, items).
// Unloaded chunks are solid by default, so nothing falls through a world that is still streaming in.
#include "VoxelWorld.h"
#include "engine/core/Fixed.h"
#include <cstdlib>
namespace aiverse::lib::systems {
inline std::int64_t voxelCeil(Fixed f){return -((-f).floorInt());}
inline Fixed voxelAxis(Vec3 v,int a){return a==0?v.x:a==1?v.y:v.z;}
inline void voxelSetAxis(Vec3& v,int a,Fixed f){(a==0?v.x:a==1?v.y:v.z)=f;}
inline std::int32_t voxelCell(Fixed f){return static_cast<std::int32_t>(f.floorInt());}

struct VoxelRayHit{bool hit=false,unloaded=false;BlockPos block;BlockPos before;int face=-1;Fixed t;};
// Walks cells along start..start+delta (t in [0,1]) and stops at the first block for which stop(id, def) is true.
// face is the entered face of the hit block (index into kFaceDir), -1 when the start cell already matches.
template<class Stop>
VoxelRayHit voxelRaycast(const VoxelWorld& w,const BlockRegistry& r,Vec3 start,Vec3 delta,Stop stop,int maxSteps=1024){
    VoxelRayHit out;BlockPos cell{voxelCell(start.x),voxelCell(start.y),voxelCell(start.z)};
    auto test=[&](BlockPos p)->bool{auto b=w.block(p);if(!b){out.unloaded=true;return true;}return stop(*b,r[*b]);};
    if(test(cell)){out.hit=!out.unloaded;out.block=cell;out.before=cell;return out;}
    const Fixed huge=Fixed::fromRaw(std::int64_t(1)<<46);
    int step[3];Fixed tMax[3],tDelta[3];
    for(int a=0;a<3;++a){auto d=voxelAxis(delta,a);auto s=voxelAxis(start,a);auto c=a==0?cell.x:a==1?cell.y:cell.z;
        if(d.raw==0){step[a]=0;tMax[a]=huge;tDelta[a]=huge;continue;}
        step[a]=d.raw>0?1:-1;auto ad=fxAbs(d);tDelta[a]=1_fx/ad;
        auto boundary=d.raw>0?Fixed::fromInt(c+1)-s:s-Fixed::fromInt(c);tMax[a]=boundary/ad;}
    for(int i=0;i<maxSteps;++i){
        int a=tMax[0]<=tMax[1]?(tMax[0]<=tMax[2]?0:2):(tMax[1]<=tMax[2]?1:2);
        if(tMax[a]>1_fx)return out;
        auto before=cell;(a==0?cell.x:a==1?cell.y:cell.z)+=step[a];out.t=tMax[a];tMax[a]+=tDelta[a];
        if(test(cell)){if(out.unloaded)return out;out.hit=true;out.block=cell;out.before=before;out.face=a*2+(step[a]>0?0:1);return out;}
    }
    return out;
}
inline VoxelRayHit voxelRaycastSolid(const VoxelWorld& w,const BlockRegistry& r,Vec3 start,Vec3 delta){
    return voxelRaycast(w,r,start,delta,[](BlockId id,const BlockDef& d){return id!=kAir&&(d.solid||d.shape==BlockShape::Cross);});}

struct VoxelBox{Vec3 min,max;};
struct VoxelMove{Vec3 delta;bool hit[3]{false,false,false};bool grounded=false,ceiling=false,tooFar=false;};
inline bool voxelSolidAt(const VoxelWorld& w,const BlockRegistry& r,BlockPos p,bool unloadedSolid){auto b=w.block(p);return b?r[*b].solid:unloadedSolid;}
inline bool voxelOverlapsSolid(const VoxelWorld& w,const BlockRegistry& r,VoxelBox b,bool unloadedSolid=true){
    for(auto x=b.min.x.floorInt();x<voxelCeil(b.max.x);++x)for(auto y=b.min.y.floorInt();y<voxelCeil(b.max.y);++y)for(auto z=b.min.z.floorInt();z<voxelCeil(b.max.z);++z)
        if(voxelSolidAt(w,r,{static_cast<std::int32_t>(x),static_cast<std::int32_t>(y),static_cast<std::int32_t>(z)},unloadedSolid))return true;
    return false;
}
// Moves a box by delta: Y first, then X, then Z, clipping each axis against solid unit blocks it would enter.
// A box already overlapping a block can leave it. Components above maxStep blocks are rejected (tooFar): sub-step.
inline VoxelMove voxelMoveBox(const VoxelWorld& w,const BlockRegistry& r,VoxelBox box,Vec3 delta,bool unloadedSolid=true,Fixed maxStep=16_fx){
    VoxelMove out;out.delta={};
    if(fxAbs(delta.x)>maxStep||fxAbs(delta.y)>maxStep||fxAbs(delta.z)>maxStep){out.tooFar=true;return out;}
    for(int a:{1,0,2}){
        auto d=voxelAxis(delta,a);if(d.raw==0)continue;
        int u=(a+1)%3,v=(a+2)%3;
        auto u0=voxelAxis(box.min,u).floorInt(),u1=voxelCeil(voxelAxis(box.max,u)),v0=voxelAxis(box.min,v).floorInt(),v1=voxelCeil(voxelAxis(box.max,v));
        std::int64_t c0,c1;
        if(d.raw>0){c0=voxelCeil(voxelAxis(box.max,a));c1=(voxelAxis(box.max,a)+d).floorInt();}
        else{c0=(voxelAxis(box.min,a)+d).floorInt();c1=voxelCeil(voxelAxis(box.min,a))-1;}
        auto clipped=d;
        for(auto c=c0;c<=c1;++c)for(auto i=u0;i<u1;++i)for(auto j=v0;j<v1;++j){
            std::int64_t xyz[3];xyz[a]=c;xyz[u]=i;xyz[v]=j;
            if(!voxelSolidAt(w,r,{static_cast<std::int32_t>(xyz[0]),static_cast<std::int32_t>(xyz[1]),static_cast<std::int32_t>(xyz[2])},unloadedSolid))continue;
            if(d.raw>0){auto limit=Fixed::fromInt(c)-voxelAxis(box.max,a);if(limit<clipped)clipped=limit<Fixed{}?Fixed{}:limit;}
            else{auto limit=Fixed::fromInt(c+1)-voxelAxis(box.min,a);if(limit>clipped)clipped=limit>Fixed{}?Fixed{}:limit;}
        }
        if(clipped!=d){out.hit[a]=true;if(a==1){out.grounded=d.raw<0;out.ceiling=d.raw>0;}}
        voxelSetAxis(box.min,a,voxelAxis(box.min,a)+clipped);voxelSetAxis(box.max,a,voxelAxis(box.max,a)+clipped);voxelSetAxis(out.delta,a,clipped);
    }
    return out;
}
// Standing check: is there solid ground within `probe` below the box.
inline bool voxelGrounded(const VoxelWorld& w,const BlockRegistry& r,VoxelBox box,Fixed probe=Fixed::fromRaw(64),bool unloadedSolid=true){
    return voxelMoveBox(w,r,box,{Fixed{},-probe,Fixed{}},unloadedSolid).grounded;}
}
