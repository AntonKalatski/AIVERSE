#pragma once
// voxel-ticks: scheduled block updates for a VoxelWorld - flowing fluids with levels (meta & 7: 0 source, 1..7
// distance; meta & 8: falling), infinite sources, fluid reactions (lava + water -> obsidian / cobblestone),
// falling blocks (sand), and random ticks (grass spreading, crops). Simulation layer: deterministic order
// (due tick, then scheduling sequence), randomness only from the passed engine Random, bounded work per step.
// Every changed block is reported with its old id so the host can call voxel-light relightBlock.
#include "aiverselib/systems/voxel/VoxelWorld.h"
#include "engine/core/Random.h"
#include <tuple>
namespace aiverse::lib::systems {
struct VoxelFluidReaction{std::uint8_t fluid=0,touching=0;BlockId fromSource=kAir,fromFlowing=kAir;};
struct VoxelTickRules{std::uint32_t randomTicksPerChunk=3,maxUpdatesPerStep=4096,fallTicks=2;std::vector<VoxelFluidReaction> reactions;};
struct VoxelTickState{
    std::set<std::tuple<std::uint64_t,std::uint64_t,BlockPos>> queue; // (due tick, sequence, position)
    std::map<BlockPos,std::uint64_t> due;                              // one pending update per position
    std::uint64_t sequence=0,lastTick=0;bool started=false;
};
struct VoxelChange{BlockPos pos;BlockId oldBlock=kAir;};
struct VoxelTickReport{std::vector<VoxelChange> changed;std::size_t processed=0,randomTicks=0;bool budgetHit=false;};
// Schedules an update of `p` at tick `at`; an earlier pending update of the same block wins.
inline bool scheduleVoxelTick(VoxelTickState& s,BlockPos p,std::uint64_t at){
    auto it=s.due.find(p);
    if(it!=s.due.end()){
        if(it->second<=at)return false;
        for(auto q=s.queue.lower_bound({it->second,0,BlockPos{}});q!=s.queue.end()&&std::get<0>(*q)==it->second;++q)if(std::get<2>(*q)==p){s.queue.erase(q);break;}
    }
    s.due[p]=at;s.queue.insert({at,++s.sequence,p});return true;
}
namespace voxel_ticks_detail{
inline std::uint32_t delayOf(const BlockDef& d,const VoxelTickRules& rules){return d.fluid?d.fluidTicks:d.falls?rules.fallTicks:1;}
inline bool reactive(const BlockDef& d){return d.fluid||d.falls;}
}
// Schedules the changed block and its six neighbours if they react to neighbour changes (fluids, falling).
inline void notifyVoxelChange(VoxelTickState& s,const VoxelWorld& w,const BlockRegistry& r,const VoxelTickRules& rules,BlockPos p,std::uint64_t now){
    using namespace voxel_ticks_detail;
    auto check=[&](BlockPos q){auto b=w.block(q);if(b&&reactive(r[*b]))scheduleVoxelTick(s,q,now+delayOf(r[*b],rules));};
    check(p);for(int f=0;f<6;++f)check(neighbour(p,f));
}
namespace voxel_ticks_detail{
struct Step{
    VoxelWorld& w;const BlockRegistry& r;const VoxelTickRules& rules;VoxelTickState& s;std::uint64_t now;VoxelTickReport& out;
    bool set(BlockPos p,BlockId id,std::uint8_t meta){auto old=w.set(p,id,meta,r);if(!old)return false;out.changed.push_back({p,*old});notifyVoxelChange(s,w,r,rules,p,now);return true;}
    bool sameFluid(BlockPos p,std::uint8_t fluid)const{auto b=w.block(p);return b&&r[*b].fluid==fluid;}
    bool canFlowInto(BlockPos p,std::uint8_t fluid)const{auto b=w.block(p);if(!b)return false;const auto& d=r[*b];return d.replaceable&&d.fluid!=fluid&&!(d.fluid&&fluid);}
    int effLevel(BlockPos p)const{auto m=w.meta(p);return (m&8)?0:(m&7);}
    bool react(BlockPos p,const BlockDef& d,std::uint8_t meta){
        for(const auto& rx:rules.reactions){if(rx.fluid!=d.fluid)continue;
            for(int f=0;f<6;++f){if(f==2)continue;auto n=neighbour(p,f);auto b=w.block(n);if(!b||r[*b].fluid!=rx.touching)continue;
                auto to=(meta&15)==0?rx.fromSource:rx.fromFlowing;if(to==kAir)continue;return set(p,to,0);}}
        return false;
    }
    void fluid(BlockPos p,BlockId id,const BlockDef& d){
        auto meta=w.meta(p);
        if(react(p,d,meta))return;
        bool source=(meta&15)==0;
        if(!source){
            // Recompute support: falling if the same fluid is above, else one more than the best horizontal neighbour.
            std::uint8_t want=0xff;
            if(sameFluid(offset(p,0,1,0),d.fluid))want=8;
            else{int best=99,sources=0;
                for(int f:{0,1,4,5}){auto n=neighbour(p,f);if(!sameFluid(n,d.fluid))continue;int lv=effLevel(n);if((w.meta(n)&15)==0)++sources;best=std::min(best,lv);}
                auto below=offset(p,0,-1,0);auto bb=w.block(below);bool solidBelow=bb&&(r[*bb].solid||((r[*bb].fluid==d.fluid)&&(w.meta(below)&15)==0));
                if(d.infiniteSource&&sources>=2&&solidBelow)want=0;
                else if(best+1<=d.fluidSpread)want=static_cast<std::uint8_t>(best+1);}
            if(want==0xff){set(p,kAir,0);return;}
            if(want!=meta){set(p,id,want);meta=want;}
        }
        auto below=offset(p,0,-1,0);
        if(canFlowInto(below,d.fluid)){set(below,id,8);return;}
        if(sameFluid(below,d.fluid)&&(w.meta(below)&15)!=0)return; // keeps feeding the flow below instead of spreading
        int next=((meta&8)?0:(meta&7))+1;if(next>d.fluidSpread)return;
        for(int f:{0,1,4,5}){auto n=neighbour(p,f);
            if(canFlowInto(n,d.fluid))set(n,id,static_cast<std::uint8_t>(next));
            else if(sameFluid(n,d.fluid)){auto m=w.meta(n);if((m&15)!=0&&!(m&8)&&(m&7)>next)set(n,id,static_cast<std::uint8_t>(next));}}
    }
    void fall(BlockPos p,BlockId id){
        auto below=offset(p,0,-1,0);auto b=w.block(below);if(!b||!r[*b].replaceable)return;
        auto meta=w.meta(p);set(p,kAir,0);set(below,id,meta);
    }
};
}
// Runs updates due at or before `tick` (strictly increasing ticks), then random ticks for `active` chunks
// (all resident chunks when null). Returns nullopt for a non-increasing tick.
inline std::optional<VoxelTickReport> stepVoxelTicks(const VoxelTickRules& rules,VoxelTickState& s,VoxelWorld& w,const BlockRegistry& r,std::uint64_t tick,Random& rng,const std::set<ChunkPos>* active=nullptr){
    using namespace voxel_ticks_detail;
    if(s.started&&tick<=s.lastTick)return {};
    s.started=true;s.lastTick=tick;VoxelTickReport out;Step st{w,r,rules,s,tick,out};
    while(!s.queue.empty()&&std::get<0>(*s.queue.begin())<=tick){
        if(out.processed>=rules.maxUpdatesPerStep){out.budgetHit=true;break;}
        auto [at,seq,p]=*s.queue.begin();(void)at;(void)seq;s.queue.erase(s.queue.begin());s.due.erase(p);++out.processed;
        auto b=w.block(p);if(!b)continue;const auto& d=r[*b];
        if(d.fluid)st.fluid(p,*b,d);else if(d.falls)st.fall(p,*b);
    }
    auto randomTick=[&](ChunkPos c,const VoxelChunk& chunk){
        if(chunk.fullyAir())return;auto o=chunkOrigin(c);
        for(std::uint32_t k=0;k<rules.randomTicksPerChunk;++k){auto i=rng.below(kVoxelChunkVolume);auto roll=rng.below(4096);++out.randomTicks;
            const auto& d=r[chunk.blocks[i]];if(!d.growChance||roll>=d.growChance)continue;
            BlockPos p{o.x+static_cast<int>(i&15),o.y+static_cast<int>(i>>8),o.z+static_cast<int>((i>>4)&15)};
            // Growth needs open space above (grass on dirt, crops); the host can veto via its own rules.
            auto above=w.block(offset(p,0,1,0));if(above&&r[*above].opacity>1)continue;
            st.set(p,d.growInto,0);}
    };
    if(active){for(auto c:*active)if(auto* chunk=w.chunk(c))randomTick(c,*chunk);}
    else{std::vector<ChunkPos> keys;keys.reserve(w.chunks().size());for(const auto& [c,chunk]:w.chunks()){(void)chunk;keys.push_back(c);}for(auto c:keys)if(auto* chunk=w.chunk(c))randomTick(c,*chunk);}
    return out;
}
inline std::uint64_t voxelTicksHash(const VoxelTickState& s){VoxelHasher h;h.u64(s.sequence);h.u64(s.lastTick);h.byte(s.started);
    for(const auto& [at,seq,p]:s.queue){h.u64(at);h.u64(seq);h.i32(p.x);h.i32(p.y);h.i32(p.z);}return h.h;}
}
