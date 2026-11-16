#pragma once
// voxel-mesh (presentation): CPU meshes for one chunk - hidden-face culling, greedy merging of equal faces,
// per-vertex ambient occlusion, smooth sky/block light, and three layers (opaque, cutout, translucent) for the
// renderer's passes. Cross blocks (plants, torches) become two double-sided quads; fluids lower their top by level.
// Positions are local to the chunk origin (0..16): add the chunk origin relative to the camera origin in Fixed
// before converting to float (engine/world relativeTo). UVs are in blocks, so use a texture array with repeat
// (or fract() in the shader for an atlas). Safe on a worker thread over a copied neighbourhood (VoxelMeshInput).
#include "aiverselib/systems/voxel/VoxelWorld.h"
namespace aiverse::lib::systems {
struct VoxelVertex{float x=0,y=0,z=0,u=0,v=0;std::uint16_t texture=0;std::int8_t nx=0,ny=0,nz=0;std::uint8_t ao=3,sky=15,block=0;};
struct VoxelMeshLayer{std::vector<VoxelVertex> vertices;std::vector<std::uint32_t> indices;std::size_t quads()const{return indices.size()/6;}};
enum VoxelLayerIndex{kVoxelOpaque=0,kVoxelCutout=1,kVoxelTranslucent=2};
struct VoxelChunkMesh{ChunkPos chunk;std::uint32_t revision=0;std::array<VoxelMeshLayer,3> layers;bool empty()const{return layers[0].indices.empty()&&layers[1].indices.empty()&&layers[2].indices.empty();}};
struct VoxelMeshOptions{bool greedy=true,ambientOcclusion=true,smoothLight=true,unloadedOpaque=true;};
// A copy of the chunk and a one-block border (18^3): the only input meshing needs. Build it on the owner thread.
struct VoxelMeshInput{
    ChunkPos chunk;std::uint32_t revision=0;
    std::array<BlockId,18*18*18> blocks{};std::array<std::uint8_t,18*18*18> meta{},light{};std::array<bool,18*18*18> loaded{};
    static int at(int x,int y,int z){return ((y+1)*18+(z+1))*18+(x+1);}
};
inline VoxelMeshInput voxelMeshInput(const VoxelWorld& w,ChunkPos c){
    VoxelMeshInput in;in.chunk=c;if(auto* self=w.chunk(c))in.revision=self->revision;
    for(int cy=-1;cy<=1;++cy)for(int cz=-1;cz<=1;++cz)for(int cx=-1;cx<=1;++cx){
        auto* n=w.chunk({c.x+cx,c.y+cy,c.z+cz});if(!n)continue;
        int x0=cx<0?15:0,x1=cx>0?0:15,y0=cy<0?15:0,y1=cy>0?0:15,z0=cz<0?15:0,z1=cz>0?0:15;
        for(int y=y0;y<=y1;++y)for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){
            int i=VoxelMeshInput::at(x+cx*16,y+cy*16,z+cz*16);auto s=static_cast<std::size_t>(voxelIndex(x,y,z));auto d=static_cast<std::size_t>(i);
            in.blocks[d]=n->blocks[s];in.meta[d]=n->meta[s];in.light[d]=n->light[s];in.loaded[d]=true;}}
    return in;
}
namespace voxel_mesh_detail{
struct Face{bool on=false;std::uint16_t texture=0;std::uint8_t layer=0;std::array<std::uint8_t,4> ao{},sky{},block{};
    bool operator==(const Face& o)const{return on==o.on&&texture==o.texture&&layer==o.layer&&ao==o.ao&&sky==o.sky&&block==o.block;}};
struct Corner{float p[3]={0,0,0},uv[2]={0,0};std::uint8_t ao=3,sky=15,block=0;};
inline int layerOf(RenderLayer l){return l==RenderLayer::Opaque?kVoxelOpaque:l==RenderLayer::Cutout?kVoxelCutout:kVoxelTranslucent;}
struct Ctx{
    const VoxelMeshInput& in;const BlockRegistry& r;const VoxelMeshOptions& o;
    std::size_t at(int x,int y,int z)const{return static_cast<std::size_t>(VoxelMeshInput::at(x,y,z));}
    BlockId id(int x,int y,int z)const{return in.blocks[at(x,y,z)];}
    bool loaded(int x,int y,int z)const{return in.loaded[at(x,y,z)];}
    bool solidForAo(int x,int y,int z)const{if(!loaded(x,y,z))return o.unloadedOpaque;return occludes(r[id(x,y,z)]);}
    std::uint8_t lightAt(int x,int y,int z)const{return in.light[at(x,y,z)];}
    bool hides(const BlockDef& self,int x,int y,int z)const{
        if(!loaded(x,y,z))return o.unloadedOpaque;const auto& n=r[id(x,y,z)];
        if(occludes(n))return true;
        if(n.id==self.id&&self.layer==RenderLayer::Translucent)return true;   // water next to water, glass next to glass
        return self.fluid&&n.fluid==self.fluid;}
};
// Emits one quad. Corners are given counter-clockwise for the positive face; `reverse` flips the winding for
// negative faces. The diagonal is chosen by AO so occlusion gradients do not show a seam.
inline void emit(VoxelMeshLayer& L,std::array<Corner,4> cs,bool reverse,std::uint16_t tex,const int* n){
    if(reverse)std::reverse(cs.begin(),cs.end());
    auto base=static_cast<std::uint32_t>(L.vertices.size());
    for(const auto& c:cs){VoxelVertex v;v.x=c.p[0];v.y=c.p[1];v.z=c.p[2];v.u=c.uv[0];v.v=c.uv[1];v.texture=tex;
        v.nx=static_cast<std::int8_t>(n[0]);v.ny=static_cast<std::int8_t>(n[1]);v.nz=static_cast<std::int8_t>(n[2]);v.ao=c.ao;v.sky=c.sky;v.block=c.block;L.vertices.push_back(v);}
    if(cs[0].ao+cs[2].ao<cs[1].ao+cs[3].ao)L.indices.insert(L.indices.end(),{base+1,base+2,base+3,base+1,base+3,base});
    else L.indices.insert(L.indices.end(),{base,base+1,base+2,base,base+2,base+3});
}
// Corner k of a face spanning w x h cells from (i, j) on the slice: k = 0 (0,0), 1 (1,0), 2 (1,1), 3 (0,1) in (u, v).
// On X faces texture u runs along Z and v along Y, so side textures stay upright on every side.
inline std::array<Corner,4> corners(int a,int s,int slice,int i,int j,int w,int h,const Face& f){
    int u=(a+1)%3,v=(a+2)%3;std::array<Corner,4> cs{};
    for(int k=0;k<4;++k){auto& c=cs[static_cast<std::size_t>(k)];int du=(k==1||k==2),dv=(k>=2);
        c.p[a]=static_cast<float>(slice+(s>0?1:0));c.p[u]=static_cast<float>(i+du*w);c.p[v]=static_cast<float>(j+dv*h);
        float eu=static_cast<float>(du*w),ev=static_cast<float>(dv*h);
        if(a==0){c.uv[0]=ev;c.uv[1]=eu;}else{c.uv[0]=eu;c.uv[1]=ev;}
        c.ao=f.ao[static_cast<std::size_t>(k)];c.sky=f.sky[static_cast<std::size_t>(k)];c.block=f.block[static_cast<std::size_t>(k)];}
    return cs;
}
}
inline VoxelChunkMesh meshChunk(const VoxelMeshInput& in,const BlockRegistry& r,const VoxelMeshOptions& o={}){
    using namespace voxel_mesh_detail;VoxelChunkMesh out;out.chunk=in.chunk;out.revision=in.revision;Ctx c{in,r,o};
    // Cubes and fluids: for each face direction and slice, a 16x16 mask of visible faces, merged greedily.
    for(int f=0;f<6;++f){
        int a=f/2,s=(f&1)?1:-1,u=(a+1)%3,v=(a+2)%3;int n[3]={0,0,0};n[a]=s;
        for(int slice=0;slice<16;++slice){
            std::array<Face,256> mask{};
            for(int j=0;j<16;++j)for(int i=0;i<16;++i){
                int p[3];p[a]=slice;p[u]=i;p[v]=j;
                const auto& d=r[c.id(p[0],p[1],p[2])];
                if(d.shape!=BlockShape::Cube&&d.shape!=BlockShape::Fluid)continue;
                int q[3]={p[0]+n[0],p[1]+n[1],p[2]+n[2]};
                if(c.hides(d,q[0],q[1],q[2]))continue;
                Face face;face.on=true;face.texture=d.textures[static_cast<std::size_t>(f)];
                face.layer=static_cast<std::uint8_t>(d.shape==BlockShape::Fluid?kVoxelTranslucent:layerOf(d.layer));
                auto front=c.lightAt(q[0],q[1],q[2]);
                for(int k=0;k<4;++k){
                    int du=(k==1||k==2)?1:-1,dv=(k>=2)?1:-1;
                    int s1[3]={q[0],q[1],q[2]},s2[3]={q[0],q[1],q[2]},cr[3]={q[0],q[1],q[2]};s1[u]+=du;s2[v]+=dv;cr[u]+=du;cr[v]+=dv;
                    bool b1=c.solidForAo(s1[0],s1[1],s1[2]),b2=c.solidForAo(s2[0],s2[1],s2[2]),bc=c.solidForAo(cr[0],cr[1],cr[2]);
                    auto K=static_cast<std::size_t>(k);
                    face.ao[K]=o.ambientOcclusion?static_cast<std::uint8_t>(b1&&b2?0:3-(int(b1)+int(b2)+int(bc))):3;
                    if(o.smoothLight){int sky=skyLight(front),blk=blockLight(front),cnt=1;
                        auto addL=[&](const int* pp,bool solid){if(solid)return;auto l=c.lightAt(pp[0],pp[1],pp[2]);sky+=skyLight(l);blk+=blockLight(l);++cnt;};
                        addL(s1,b1);addL(s2,b2);if(!(b1&&b2))addL(cr,bc);
                        face.sky[K]=static_cast<std::uint8_t>(sky/cnt);face.block[K]=static_cast<std::uint8_t>(blk/cnt);}
                    else{face.sky[K]=skyLight(front);face.block[K]=blockLight(front);}
                }
                if(d.shape==BlockShape::Fluid){
                    // Fluids are never merged: the top height follows the level of each block.
                    auto m=c.in.meta[c.at(p[0],p[1],p[2])];int lvl=m&7;bool full=(m&8)||r[c.id(p[0],p[1]+1,p[2])].fluid==d.fluid;
                    float top=full?1.f:(lvl==0?14.f/16.f:(14.f-1.5f*static_cast<float>(lvl))/16.f);
                    auto cs=corners(a,s,slice,i,j,1,1,face);
                    for(auto& cc:cs)cc.p[1]=static_cast<float>(p[1])+(cc.p[1]-static_cast<float>(p[1]))*top;
                    emit(out.layers[kVoxelTranslucent],cs,s<0,face.texture,n);
                    continue;
                }
                mask[static_cast<std::size_t>(j*16+i)]=face;
            }
            for(int j=0;j<16;++j)for(int i=0;i<16;){
                const auto m=mask[static_cast<std::size_t>(j*16+i)];if(!m.on){++i;continue;}
                int w=1,h=1;
                if(o.greedy){while(i+w<16&&mask[static_cast<std::size_t>(j*16+i+w)]==m)++w;
                    for(bool grow=true;grow&&j+h<16;){for(int k=0;k<w;++k)if(!(mask[static_cast<std::size_t>((j+h)*16+i+k)]==m)){grow=false;break;}if(grow)++h;}}
                for(int y=0;y<h;++y)for(int x=0;x<w;++x)mask[static_cast<std::size_t>((j+y)*16+i+x)].on=false;
                emit(out.layers[m.layer],corners(a,s,slice,i,j,w,h,m),s<0,m.texture,n);
                i+=w;
            }
        }
    }
    // Cross blocks: two diagonal quads, each emitted for both sides.
    for(int y=0;y<16;++y)for(int z=0;z<16;++z)for(int x=0;x<16;++x){
        const auto& d=r[c.id(x,y,z)];if(d.shape!=BlockShape::Cross)continue;
        auto l=c.lightAt(x,y,z);float X=static_cast<float>(x),Y=static_cast<float>(y),Z=static_cast<float>(z);const float e=0.146f;
        const float diag[2][2][2]={{{X+e,Z+e},{X+1-e,Z+1-e}},{{X+e,Z+1-e},{X+1-e,Z+e}}};
        for(int dI=0;dI<2;++dI)for(int side=0;side<2;++side){
            std::array<Corner,4> cs{};const float uvs[4][2]={{0,0},{1,0},{1,1},{0,1}};
            for(int k=0;k<4;++k){auto& cc=cs[static_cast<std::size_t>(k)];int end=(k==1||k==2)?1:0;if(side)end=1-end;
                cc.p[0]=diag[dI][end][0];cc.p[2]=diag[dI][end][1];cc.p[1]=Y+(k>=2?1.f:0.f);cc.uv[0]=uvs[k][0];cc.uv[1]=uvs[k][1];cc.sky=skyLight(l);cc.block=blockLight(l);}
            // Facing normal of this quad (unnormalised direction; the shader normalises).
            float ax=cs[1].p[0]-cs[0].p[0],az=cs[1].p[2]-cs[0].p[2];int nrm[3]={az>0?-1:az<0?1:0,0,ax>0?1:ax<0?-1:0};
            emit(out.layers[static_cast<std::size_t>(layerOf(d.layer))],cs,false,d.textures[0],nrm);
        }
    }
    return out;
}
inline VoxelChunkMesh meshChunk(const VoxelWorld& w,const BlockRegistry& r,ChunkPos c,const VoxelMeshOptions& o={}){return meshChunk(voxelMeshInput(w,c),r,o);}
// Translucent quads sorted back to front for a camera at `eye` (chunk-local units); call when the camera moves.
inline void sortTranslucent(VoxelMeshLayer& L,float ex,float ey,float ez){
    std::size_t n=L.indices.size()/6;std::vector<std::pair<float,std::size_t>> order(n);
    for(std::size_t q=0;q<n;++q){float cx=0,cy=0,cz=0;for(int k=0;k<6;++k){const auto& v=L.vertices[L.indices[q*6+static_cast<std::size_t>(k)]];cx+=v.x;cy+=v.y;cz+=v.z;}
        cx=cx/6-ex;cy=cy/6-ey;cz=cz/6-ez;order[q]={-(cx*cx+cy*cy+cz*cz),q};}
    std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::vector<std::uint32_t> idx;idx.reserve(L.indices.size());for(auto& [d,q]:order){(void)d;for(int k=0;k<6;++k)idx.push_back(L.indices[q*6+static_cast<std::size_t>(k)]);}
    L.indices=std::move(idx);
}
}
