#pragma once
// voxel-world: the mutable block world. Chunks live in a std::map (deterministic order). Only chunks changed by
// simulated edits are state worth saving: an untouched chunk is regenerated from the seed, an edited chunk that
// is unloaded is "parked" as encoded bytes and comes back on the next load. The host owns streaming, generation,
// light (voxel-light), meshes (voxel-mesh) and replication (voxel-replication); this class owns the blocks.
#include "VoxelCodec.h"
#include <set>
namespace aiverse::lib::systems {
struct VoxelBounds{std::int32_t minChunkY=-4,maxChunkY=19;std::int32_t horizontalChunks=1<<22;std::size_t maxPendingEdits=1<<16;};
class VoxelWorld{
public:
    explicit VoxelWorld(VoxelBounds bounds={}):bounds_(bounds){}
    const VoxelBounds& bounds()const{return bounds_;}
    bool inBounds(ChunkPos c)const{return c.y>=bounds_.minChunkY&&c.y<=bounds_.maxChunkY&&c.x> -bounds_.horizontalChunks&&c.x<bounds_.horizontalChunks&&c.z> -bounds_.horizontalChunks&&c.z<bounds_.horizontalChunks;}
    bool inBounds(BlockPos p)const{return p.x> -kVoxelCoordinateLimit&&p.x<kVoxelCoordinateLimit&&p.y> -kVoxelCoordinateLimit&&p.y<kVoxelCoordinateLimit&&p.z> -kVoxelCoordinateLimit&&p.z<kVoxelCoordinateLimit&&inBounds(chunkOf(p));}
    const std::map<ChunkPos,VoxelChunk>& chunks()const{return chunks_;}
    bool loaded(ChunkPos c)const{return chunks_.count(c)!=0;}
    VoxelChunk* chunk(ChunkPos c){auto it=chunks_.find(c);return it==chunks_.end()?nullptr:&it->second;}
    const VoxelChunk* chunk(ChunkPos c)const{auto it=chunks_.find(c);return it==chunks_.end()?nullptr:&it->second;}
    // Publishes a generated or decoded chunk (replacing a resident one). Marks it and its neighbours for meshing.
    bool insert(ChunkPos c,VoxelChunk data){if(!inBounds(c))return false;data.nonAir=countNonAir(data);chunks_[c]=std::move(data);markRender(c,true);lightDirty.insert(c);return true;}
    // True when an edited copy is parked; load it with unpark() instead of generating.
    bool parked(ChunkPos c)const{return parked_.count(c)!=0;}
    std::optional<VoxelChunk> unpark(ChunkPos c,const BlockRegistry& r){
        auto it=parked_.find(c);if(it==parked_.end())return {};auto chunk=decodeChunk(it->second,r.defs.size());if(!chunk)return {};parked_.erase(it);edited.insert(c);return chunk;}
    // Removes a resident chunk; an edited one is parked. Returns false if it was not resident.
    bool unload(ChunkPos c){auto it=chunks_.find(c);if(it==chunks_.end())return false;
        if(edited.count(c)){parked_[c]=encodeChunk(it->second);edited.erase(c);}
        chunks_.erase(it);renderDirty.erase(c);lightDirty.erase(c);markRender(c,false);return true;}
    std::optional<BlockId> block(BlockPos p)const{if(!inBounds(p))return {};auto* c=chunk(chunkOf(p));if(!c)return {};return c->blocks[static_cast<std::size_t>(voxelIndex(p))];}
    BlockId blockOr(BlockPos p,BlockId fallback)const{auto b=block(p);return b?*b:fallback;}
    std::uint8_t meta(BlockPos p)const{auto* c=inBounds(p)?chunk(chunkOf(p)):nullptr;return c?c->meta[static_cast<std::size_t>(voxelIndex(p))]:0;}
    std::uint8_t light(BlockPos p)const{auto* c=inBounds(p)?chunk(chunkOf(p)):nullptr;return c?c->light[static_cast<std::size_t>(voxelIndex(p))]:0;}
    // A simulated edit: bumps the chunk revision, records the edit for replication, marks the chunk edited.
    // Returns the previous block, or nullopt when unloaded / out of bounds / unknown id.
    std::optional<BlockId> set(BlockPos p,BlockId id,std::uint8_t meta,const BlockRegistry& r){
        if(!r.has(id)||!inBounds(p))return {};auto cp=chunkOf(p);auto* c=chunk(cp);if(!c)return {};
        auto i=static_cast<std::size_t>(voxelIndex(p));auto old=c->blocks[i];
        if(old==id&&c->meta[i]==meta)return old;
        c->nonAir=static_cast<std::uint16_t>(c->nonAir-(old!=kAir)+(id!=kAir));c->blocks[i]=id;c->meta[i]=meta;++c->revision;
        edited.insert(cp);markRender(p);
        if(edits.size()>=bounds_.maxPendingEdits){edits.clear();editOverflow=true;}
        edits.push_back({p,id,meta,c->revision,++editSeq});
        return old;
    }
    // Mirror write for a replicated edit or a generator: sets the revision, keeps no edit record.
    bool overwrite(BlockPos p,BlockId id,std::uint8_t meta,std::uint32_t revision,const BlockRegistry& r){
        if(!r.has(id)||!inBounds(p))return false;auto* c=chunk(chunkOf(p));if(!c)return false;auto i=static_cast<std::size_t>(voxelIndex(p));
        c->nonAir=static_cast<std::uint16_t>(c->nonAir-(c->blocks[i]!=kAir)+(id!=kAir));c->blocks[i]=id;c->meta[i]=meta;c->revision=revision;markRender(p);return true;}
    bool setLight(BlockPos p,std::uint8_t value){auto* c=inBounds(p)?chunk(chunkOf(p)):nullptr;if(!c)return false;auto& l=c->light[static_cast<std::size_t>(voxelIndex(p))];if(l!=value){l=value;markRender(p);}return true;}
    // Regions for disk: every edited chunk (resident or parked) grouped by region, encoded.
    std::map<RegionPos,std::vector<std::uint8_t>> exportRegions()const{
        std::map<RegionPos,std::map<ChunkPos,std::vector<std::uint8_t>>> groups;
        for(const auto& [c,bytes]:parked_)groups[regionOf(c)][c]=bytes;
        for(auto c:edited)if(auto* chunk=this->chunk(c))groups[regionOf(c)][c]=encodeChunk(*chunk);
        std::map<RegionPos,std::vector<std::uint8_t>> out;for(const auto& [r,g]:groups)out[r]=encodeRegion(g);return out;
    }
    // Loads a region file into the parked set (resident chunks are not replaced). False on a damaged file.
    bool importRegion(const std::vector<std::uint8_t>& data,const BlockRegistry& r){
        auto chunks=decodeRegion(data,r.defs.size());if(!chunks)return false;
        for(auto& [c,bytes]:*chunks){if(!inBounds(c))return false;}
        for(auto& [c,bytes]:*chunks)if(!loaded(c))parked_[c]=std::move(bytes);
        return true;
    }
    const std::map<ChunkPos,std::vector<std::uint8_t>>& parkedChunks()const{return parked_;}
    void restoreParked(std::map<ChunkPos,std::vector<std::uint8_t>> p){parked_=std::move(p);}
    // State hash: resident blocks + meta + revisions, edited set and parked bytes. Light is derived and excluded.
    std::uint64_t hash()const{
        VoxelHasher h;h.i32(bounds_.minChunkY);h.i32(bounds_.maxChunkY);
        for(const auto& [c,chunk]:chunks_){h.i32(c.x);h.i32(c.y);h.i32(c.z);h.u32(chunk.revision);h.byte(edited.count(c)?1:0);
            for(auto b:chunk.blocks){h.byte(static_cast<std::uint8_t>(b));h.byte(static_cast<std::uint8_t>(b>>8));}for(auto m:chunk.meta)h.byte(m);}
        for(const auto& [c,bytes]:parked_){h.i32(c.x);h.i32(c.y);h.i32(c.z);h.u64(bytes.size());for(auto b:bytes)h.byte(b);}
        return h.h;
    }
    std::set<ChunkPos> renderDirty;  // presentation: chunks whose mesh must be rebuilt (host drains)
    std::set<ChunkPos> lightDirty;   // chunks inserted since their light was computed (voxel-light drains)
    std::set<ChunkPos> edited;       // resident chunks that differ from generation (saved)
    std::vector<BlockEdit> edits;    // simulated edits since the host last drained them (replication)
    bool editOverflow=false;         // edits were dropped: replication must resend changed chunks
    std::uint64_t editSeq=0;
private:
    void markRender(ChunkPos c,bool self){if(self&&loaded(c))renderDirty.insert(c);for(int f=0;f<6;++f){ChunkPos n{c.x+kFaceDir[f][0],c.y+kFaceDir[f][1],c.z+kFaceDir[f][2]};if(loaded(n))renderDirty.insert(n);}}
    // A voxel change re-meshes its chunk and the neighbours it touches (AO and light sample across borders).
    void markRender(BlockPos p){auto c=chunkOf(p);renderDirty.insert(c);
        int lx=p.x&15,ly=p.y&15,lz=p.z&15;
        for(int dx=(lx==0?-1:0);dx<=(lx==15?1:0);++dx)for(int dy=(ly==0?-1:0);dy<=(ly==15?1:0);++dy)for(int dz=(lz==0?-1:0);dz<=(lz==15?1:0);++dz){
            ChunkPos n{c.x+dx,c.y+dy,c.z+dz};if(loaded(n))renderDirty.insert(n);}}
    VoxelBounds bounds_;
    std::map<ChunkPos,VoxelChunk> chunks_;
    std::map<ChunkPos,std::vector<std::uint8_t>> parked_;
};
}
