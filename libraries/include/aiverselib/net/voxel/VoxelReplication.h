#pragma once
// voxel-replication: server-authoritative streaming of a VoxelWorld to clients. Per client the server keeps
// which chunk revisions it has; each pump sends (1) unload notices for chunks that left the view, (2) block
// edits for chunks the client has, (3) full chunk snapshots nearest-first within a byte budget. A gap in
// revisions (dropped edits, overflow) triggers a full resend, so the client mirror converges without acks of
// individual edits. Messages are canonical bytes for any reliable ordered channel (engine/net Reliable, WebSocket).
// Host-side: never call from simulation ticks; the server world is the authority, the mirror is presentation.
#include "aiverselib/systems/voxel/VoxelWorld.h"
namespace aiverse::lib::net {
using systems::ChunkPos;using systems::BlockPos;using systems::BlockEdit;using systems::VoxelWorld;using systems::BlockRegistry;
enum class VoxelMessageType:std::uint8_t{Chunk=1,Unload=2,Edits=3};
struct VoxelMessage{VoxelMessageType type=VoxelMessageType::Chunk;ChunkPos chunk;std::vector<std::uint8_t> bytes;std::vector<BlockEdit> edits;};
inline std::vector<std::uint8_t> encodeVoxelMessage(const VoxelMessage& m){
    systems::VoxelBytes out;out.byte('V');out.byte('M');out.byte(static_cast<std::uint8_t>(m.type));
    if(m.type!=VoxelMessageType::Edits){out.svarint(m.chunk.x);out.svarint(m.chunk.y);out.svarint(m.chunk.z);}
    if(m.type==VoxelMessageType::Chunk){out.varint(m.bytes.size());out.append(m.bytes);}
    if(m.type==VoxelMessageType::Edits){out.varint(m.edits.size());for(const auto& e:m.edits){out.svarint(e.pos.x);out.svarint(e.pos.y);out.svarint(e.pos.z);out.varint(e.block);out.byte(e.meta);out.varint(e.revision);}}
    return std::move(out.data);
}
inline std::optional<VoxelMessage> decodeVoxelMessage(const std::vector<std::uint8_t>& b,std::size_t blockTypes,std::size_t maxEdits=1<<16){
    systems::VoxelReader in(b);if(in.byte()!='V'||in.byte()!='M')return {};VoxelMessage m;auto t=in.byte();if(!in.ok||t<1||t>3)return {};m.type=static_cast<VoxelMessageType>(t);
    auto coord=[&](std::int32_t& v){auto x=in.svarint();if(x< -systems::kVoxelCoordinateLimit||x>systems::kVoxelCoordinateLimit)in.ok=false;v=static_cast<std::int32_t>(x);};
    if(m.type!=VoxelMessageType::Edits){coord(m.chunk.x);coord(m.chunk.y);coord(m.chunk.z);}
    if(m.type==VoxelMessageType::Chunk){auto n=in.varint();if(!in.ok||n>in.n-in.i)return {};m.bytes.assign(in.p+in.i,in.p+in.i+n);in.i+=n;if(!systems::decodeChunk(m.bytes,blockTypes))return {};}
    if(m.type==VoxelMessageType::Edits){auto n=in.varint();if(!in.ok||n>maxEdits)return {};
        for(std::uint64_t k=0;k<n;++k){BlockEdit e;coord(e.pos.x);coord(e.pos.y);coord(e.pos.z);auto id=in.varint();e.meta=in.byte();auto rev=in.varint();
            if(!in.ok||id>=blockTypes||rev>0xffffffffull)return {};e.block=static_cast<systems::BlockId>(id);e.revision=static_cast<std::uint32_t>(rev);m.edits.push_back(e);}}
    if(!in.done())return {};return m;
}
struct VoxelView{ChunkPos center;int radius=8,below=4,above=4;}; // horizontal circle radius and vertical range, in chunks
struct VoxelPeer{std::map<ChunkPos,std::uint32_t> sent;};       // revision the client holds for each chunk
struct VoxelPumpConfig{std::size_t maxBytes=256*1024,maxChunks=32;};
struct VoxelPumpStats{std::size_t chunks=0,unloads=0,edits=0,resends=0,bytes=0,pendingChunks=0;};
inline bool voxelInView(const VoxelView& v,ChunkPos c){auto dx=static_cast<std::int64_t>(c.x-v.center.x),dz=static_cast<std::int64_t>(c.z-v.center.z);
    return dx*dx+dz*dz<=static_cast<std::int64_t>(v.radius)*v.radius&&c.y>=v.center.y-v.below&&c.y<=v.center.y+v.above;}
// `edits` are the world's edits since the previous pump of every peer (VoxelWorld::edits; clear them after
// pumping all peers). Returns encoded messages in send order.
inline std::vector<std::vector<std::uint8_t>> pumpVoxelPeer(const VoxelPumpConfig& cfg,const VoxelWorld& w,const VoxelView& view,VoxelPeer& peer,const std::vector<BlockEdit>& edits,VoxelPumpStats* stats=nullptr){
    VoxelPumpStats st;std::vector<std::vector<std::uint8_t>> out;
    for(auto it=peer.sent.begin();it!=peer.sent.end();){
        if(voxelInView(view,it->first)&&w.loaded(it->first)){++it;continue;}
        VoxelMessage m;m.type=VoxelMessageType::Unload;m.chunk=it->first;out.push_back(encodeVoxelMessage(m));++st.unloads;it=peer.sent.erase(it);}
    VoxelMessage em;em.type=VoxelMessageType::Edits;
    for(const auto& e:edits){auto c=systems::chunkOf(e.pos);auto it=peer.sent.find(c);if(it==peer.sent.end()||e.revision<=it->second)continue;
        if(e.revision!=it->second+1){peer.sent.erase(it);++st.resends;continue;}  // gap: resend the chunk below
        em.edits.push_back(e);it->second=e.revision;}
    // Anything still behind the world (overflowed edits, mirror writes) is resent whole.
    for(auto it=peer.sent.begin();it!=peer.sent.end();){auto* c=w.chunk(it->first);if(c&&c->revision!=it->second){it=peer.sent.erase(it);++st.resends;}else ++it;}
    if(!em.edits.empty()){
        std::vector<BlockEdit> kept;for(const auto& e:em.edits)if(peer.sent.count(systems::chunkOf(e.pos)))kept.push_back(e);em.edits=std::move(kept);
        if(!em.edits.empty()){st.edits=em.edits.size();auto bytes=encodeVoxelMessage(em);st.bytes+=bytes.size();out.push_back(std::move(bytes));}}
    std::vector<std::pair<std::int64_t,ChunkPos>> wanted;
    for(const auto& [c,chunk]:w.chunks()){(void)chunk;if(!peer.sent.count(c)&&voxelInView(view,c)){auto dx=static_cast<std::int64_t>(c.x-view.center.x),dy=static_cast<std::int64_t>(c.y-view.center.y),dz=static_cast<std::int64_t>(c.z-view.center.z);wanted.push_back({dx*dx+dy*dy*4+dz*dz,c});}}
    std::sort(wanted.begin(),wanted.end());
    for(const auto& [d,c]:wanted){(void)d;
        if(st.chunks>=cfg.maxChunks||st.bytes>=cfg.maxBytes){++st.pendingChunks;continue;}
        VoxelMessage m;m.type=VoxelMessageType::Chunk;m.chunk=c;m.bytes=systems::encodeChunk(*w.chunk(c));auto bytes=encodeVoxelMessage(m);
        st.bytes+=bytes.size();++st.chunks;peer.sent[c]=w.chunk(c)->revision;out.push_back(std::move(bytes));}
    if(stats)*stats=st;return out;
}
struct VoxelMirrorResult{bool ok=false;std::vector<ChunkPos> inserted,removed;std::vector<BlockEdit> applied;};
// Applies one server message to the client's mirror world. New chunks need light (voxel-light lightChunk via
// VoxelWorld::lightDirty) and every applied edit needs relightBlock with the old block before meshing.
inline VoxelMirrorResult applyVoxelMessage(VoxelWorld& mirror,const BlockRegistry& r,const std::vector<std::uint8_t>& bytes,std::vector<systems::BlockId>* oldBlocks=nullptr){
    VoxelMirrorResult out;auto m=decodeVoxelMessage(bytes,r.defs.size());if(!m)return out;out.ok=true;
    if(m->type==VoxelMessageType::Chunk){auto c=systems::decodeChunk(m->bytes,r.defs.size());if(!c||!mirror.insert(m->chunk,*c)){out.ok=false;return out;}out.inserted.push_back(m->chunk);}
    else if(m->type==VoxelMessageType::Unload){mirror.edited.erase(m->chunk);if(mirror.unload(m->chunk))out.removed.push_back(m->chunk);}
    else for(const auto& e:m->edits){auto* c=mirror.chunk(systems::chunkOf(e.pos));if(!c||e.revision!=c->revision+1)continue;
        auto old=mirror.blockOr(e.pos,systems::kAir);if(mirror.overwrite(e.pos,e.block,e.meta,e.revision,r)){out.applied.push_back(e);if(oldBlocks)oldBlocks->push_back(old);}}
    return out;
}
}
