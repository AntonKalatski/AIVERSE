// Experimental ECS query/storage comparison. Does not change production World.
// Build from repository root:
// c++ -std=c++20 -O3 -DNDEBUG -I. tests/ecs_bitmap_query_bench.cpp -o /tmp/aiverse-ecs-bitmap
// /tmp/aiverse-ecs-bitmap [entity_count]
#include "engine/core/ECS.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <utility>

using namespace aiverse;
using Clock = std::chrono::steady_clock;

struct Position { int64_t x=0, y=0, z=0; };
struct Velocity { int64_t x=1, y=2, z=3; };
struct Health { int32_t hp=100; };
static volatile uint64_t observed = 0;

struct Mask {
    std::vector<uint64_t> words;
    void set(uint32_t id) {
        const size_t block = id >> 6;
        if (block >= words.size()) words.resize(block+1, 0);
        words[block] |= uint64_t(1) << (id & 63);
    }
    void unset(uint32_t id) {
        const size_t block = id >> 6;
        if (block < words.size()) words[block] &= ~(uint64_t(1) << (id & 63));
    }
    bool has(uint32_t id) const {
        const size_t block=id>>6;
        return block<words.size() && (words[block] & (uint64_t(1) << (id & 63)))!=0;
    }
    uint64_t block(size_t i) const { return i<words.size() ? words[i] : 0; }
};

class BitmapIndex {
    std::vector<Mask> masks_;
    std::vector<uint32_t> generations_;
public:
    template<class T> Mask& mask() {
        uint32_t id=detail::componentTypeId<T>();
        if (id>=masks_.size()) masks_.resize(size_t(id)+1);
        return masks_[id];
    }
    template<class T> const Mask* find() const {
        uint32_t id=detail::componentTypeId<T>();
        return id<masks_.size()? &masks_[id]:nullptr;
    }
    template<class T> void added(Entity e) {
        if(e.index>=generations_.size()) generations_.resize(size_t(e.index)+1);
        generations_[e.index]=e.gen;
        mask<T>().set(e.index);
    }
    template<class T> void removed(Entity e) { mask<T>().unset(e.index); }
    void destroyed(Entity e) { for (auto& m:masks_) m.unset(e.index); }
    template<class A, class B, class F> void eachAll(World& w,F&& fn) const {
        const Mask* a=find<A>(); const Mask* b=find<B>();
        if(!a || !b) return;
        size_t n=std::min(a->words.size(),b->words.size());
        for(size_t block=0;block<n;++block) {
            uint64_t bits=a->block(block) & b->block(block);
            while(bits) {
                uint32_t id=uint32_t(block*64+std::countr_zero(bits));
                // Preserve generation after recycling an Entity index.
                auto& pa=w.pool<A>(); auto& pb=w.pool<B>();
                Entity e{id,generations_[id]};
                fn(e,pa.get(id),pb.get(id));
                bits &= bits-1;
            }
        }
    }
    template<class A,class B,class C,class F> void eachAllNone(World& w,F&& fn) const {
        const Mask* a=find<A>(); const Mask* b=find<B>(); const Mask* excluded=find<C>();
        if(!a || !b) return;
        size_t n=std::min(a->words.size(),b->words.size());
        for(size_t block=0;block<n;++block) {
            uint64_t bits=a->block(block)&b->block(block)&~(excluded?excluded->block(block):0ull);
            while(bits) {
                uint32_t id=uint32_t(block*64+std::countr_zero(bits));
                fn(id,w.pool<A>().get(id),w.pool<B>().get(id));
                bits &= bits-1;
            }
        }
    }
    template<class A,class B,class F> void eachAny(F&& fn) const {
        const Mask* a=find<A>(); const Mask* b=find<B>();
        size_t n=std::max(a?a->words.size():0,b?b->words.size():0);
        for(size_t block=0;block<n;++block) {
            uint64_t bits=(a?a->block(block):0ull)|(b?b->block(block):0ull);
            while(bits) {
                fn(uint32_t(block*64+std::countr_zero(bits)));
                bits &= bits-1;
            }
        }
    }
    size_t reservedBytes() const {
        size_t bytes=0; for(const auto& m:masks_) bytes+=m.words.capacity()*sizeof(uint64_t);
        return bytes;
    }
};

// The wrapper is the only supported write interface for this prototype:
// the index must be updated atomically with structural changes.
struct IndexedWorld {
    World world;
    BitmapIndex index;
    Entity create() { return world.create(); }
    template<class T,class... Args> T& add(Entity e,Args&&... args) {
        T& result=world.add<T>(e,std::forward<Args>(args)...);
        index.added<T>(e); return result;
    }
    template<class T> void remove(Entity e) {
        world.remove<T>(e); index.removed<T>(e);
    }
    void destroy(Entity e) {
        if(!world.alive(e)) return;
        world.destroy(e); index.destroyed(e);
    }
};

template<class A,class B> struct PreparedPair {
    // Immutable type-level filter; current masks are looked up on every execution.
    // Holding raw pointers to masks across component registration is unsafe.
    template<class F> void each(IndexedWorld& w,F&& fn) const {
        w.index.eachAll<A,B>(w.world,std::forward<F>(fn));
    }
};

struct StaticSlots {
    std::vector<Position> position;
    std::vector<Velocity> velocity;
    Mask positions, velocities, health;
    void reserve(size_t n) { position.reserve(n); velocity.reserve(n); }
    void add(uint32_t id,bool mobile,bool hasHealth) {
        position.push_back(Position{int64_t(id),0,0});
        velocity.push_back(Velocity{});
        positions.set(id);
        if(mobile) velocities.set(id);
        if(hasHealth) health.set(id);
    }
    template<class F> void eachAll(F&& fn) {
        size_t n=std::min(positions.words.size(),velocities.words.size());
        for(size_t block=0;block<n;++block) {
            uint64_t bits=positions.block(block)&velocities.block(block);
            while(bits) {
                uint32_t id=uint32_t(block*64+std::countr_zero(bits));
                fn(id,position[id],velocity[id]);
                bits &= bits-1;
            }
        }
    }
    size_t reservedBytes() const {
        return position.capacity()*sizeof(Position)+velocity.capacity()*sizeof(Velocity)
            +positions.words.capacity()*8+velocities.words.capacity()*8+health.words.capacity()*8;
    }
};

static uint64_t mix(uint64_t h,uint64_t v) {
    return (h^v)*1099511628211ull;
}
static uint64_t stateHash(World& world) {
    // Use entity-index order, independent of dense-pool iteration order.
    auto& p=world.pool<Position>();
    std::vector<std::pair<uint32_t,Position>> sorted;
    sorted.reserve(p.size());
    for(size_t i=0;i<p.size();++i) sorted.push_back({p.entityAt(i).index,p.dataAt(i)});
    std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    uint64_t h=1469598103934665603ull;
    for(const auto& [id,v]:sorted) {
        h=mix(h,id); h=mix(h,uint64_t(v.x)); h=mix(h,uint64_t(v.y)); h=mix(h,uint64_t(v.z));
    }
    return h;
}
static uint64_t stateHash(const StaticSlots& w) {
    uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<w.position.size();++i) if(w.positions.has(uint32_t(i))) {
        const auto& v=w.position[i];
        h=mix(h,i);h=mix(h,uint64_t(v.x));h=mix(h,uint64_t(v.y));h=mix(h,uint64_t(v.z));
    }
    return h;
}
static IndexedWorld makeIndexed(size_t n) {
    IndexedWorld w;
    for(size_t i=0;i<n;++i) {
        Entity e=w.create();
        w.add<Position>(e,Position{int64_t(i),0,0});
        if(i%4==0) w.add<Velocity>(e,Velocity{});
        if(i%8==0) w.add<Health>(e,Health{});
    }
    return w;
}
static StaticSlots makeStatic(size_t n) {
    StaticSlots w; w.reserve(n);
    for(size_t i=0;i<n;++i) w.add(uint32_t(i),i%4==0,i%8==0);
    return w;
}
static void apply(Position& p,const Velocity& v) {
    p.x+=v.x; p.y+=v.y; p.z+=v.z;
}
static void update(IndexedWorld& w,int mode) {
    if(mode==0) {
        w.world.each<Position,Velocity>([](Entity,Position& p,Velocity& v){apply(p,v);});
    } else if(mode==1) {
        w.world.each<Velocity,Position>([](Entity,Velocity& v,Position& p){apply(p,v);});
    } else if(mode==2) {
        if(w.world.count<Velocity>()<w.world.count<Position>())
            w.world.each<Velocity,Position>([](Entity,Velocity& v,Position& p){apply(p,v);});
        else w.world.each<Position,Velocity>([](Entity,Position& p,Velocity& v){apply(p,v);});
    } else {
        PreparedPair<Position,Velocity>{}.each(w,[](Entity,Position& p,Velocity& v){apply(p,v);});
    }
}
template<class F> static double elapsed(F&& fn) {
    auto t=Clock::now(); fn();
    return std::chrono::duration<double,std::milli>(Clock::now()-t).count();
}
static double median(std::vector<double> x) {
    std::sort(x.begin(),x.end()); return x[x.size()/2];
}

static void verifyIndexLifecycle() {
    IndexedWorld w;
    Entity e=w.create();
    w.add<Position>(e); w.add<Velocity>(e);
    if(!w.index.find<Velocity>()->has(e.index)) throw std::runtime_error("bitmap add failed");
    w.remove<Velocity>(e);
    if(w.index.find<Velocity>()->has(e.index)) throw std::runtime_error("bitmap remove failed");
    w.add<Velocity>(e); w.destroy(e);
    if(w.index.find<Velocity>()->has(e.index)) throw std::runtime_error("bitmap destroy failed");
    Entity recycled=w.create();
    if(recycled.index!=e.index || recycled.gen==e.gen) throw std::runtime_error("generation recycle failed");
    w.add<Position>(recycled); w.add<Velocity>(recycled);
    uint32_t matched=0;
    w.index.eachAll<Position,Velocity>(w.world,[&](Entity current,Position&,Velocity&){
        if(current!=recycled) throw std::runtime_error("bitmap generation mismatch");
        ++matched;
    });
    if(matched!=1) throw std::runtime_error("bitmap recycled entity query failed");
    w.add<Health>(recycled);
    uint32_t matchedNone=0,matchedAny=0;
    w.index.eachAllNone<Position,Velocity,Health>(w.world,[&](uint32_t,Position&,Velocity&){++matchedNone;});
    w.index.eachAny<Velocity,Health>([&](uint32_t){++matchedAny;});
    if(matchedNone!=0 || matchedAny!=1) throw std::runtime_error("bitmap filters failed");
}
// Structural mutation cost of maintaining bitmap membership (independent copies).
static void churn(size_t n) {
    constexpr int warmup=2, samples=7;
    auto seed=makeIndexed(n);
    for(int mode=0;mode<2;++mode) {
        std::vector<double> timings;
        for(int rep=-warmup;rep<samples;++rep) {
            auto w=seed;
            auto ms=elapsed([&] {
                for(size_t i=1;i<n;i+=8) {
                    Entity e{uint32_t(i),0};
                    if(mode==0) {
                        w.world.add<Health>(e,Health{});
                        w.world.remove<Health>(e);
                    } else {
                        w.add<Health>(e,Health{});
                        w.remove<Health>(e);
                    }
                }
            });
            if(mode==1) {
                auto* mask=w.index.find<Health>();
                for(size_t i=0;i<n;i+=8) {
                    if(!mask || !mask->has(uint32_t(i)))
                        throw std::runtime_error("bitmap churn damaged existing membership");
                }
                for(size_t i=1;i<n;i+=8) {
                    if(mask && mask->has(uint32_t(i)))
                        throw std::runtime_error("bitmap churn left stale membership");
                }
            }
            if(rep>=0) timings.push_back(ms);
        }
        std::cout<<(mode==0?"sparse-churn":"bitmap-indexed-churn")<<","
                 <<n<<","<<median(timings)<<",0,0\n";
    }
}
static void run(size_t n) {
    constexpr int warmup=2, samples=7, steps=10;
    IndexedWorld seed=makeIndexed(n);
    StaticSlots staticSeed=makeStatic(n);
    uint64_t expected=0;
    std::cout.flush();
    for(int mode=0;mode<5;++mode) {
        std::vector<double> times;
        for(int rep=-warmup;rep<samples;++rep) {
            uint64_t got;
            double ms=0;
            if(mode==4) {
                auto w=staticSeed; // state reset outside timed section
                ms=elapsed([&] {
                    for(int step=0;step<steps;++step)
                        w.eachAll([](uint32_t,Position& p,Velocity& v){apply(p,v);});
                });
                got=stateHash(w);
            } else {
                auto w=seed; // state reset outside timed section
                ms=elapsed([&]{for(int step=0;step<steps;++step) update(w,mode);});
                got=stateHash(w.world);
            }
            if(mode==0 && rep==-warmup) expected=got;
            if(got!=expected) throw std::runtime_error("query results differ");
            observed=observed^got;
            if(rep>=0) times.push_back(ms);
        }
        const char* names[]={"sparse-broad","sparse-narrow","sparse-auto","sparse-bitmap-prepared","static-slot-bitmap"};
        std::cout<<names[mode]<<","<<n<<","<<median(times)<<","<<expected;
        if(mode==3) std::cout<<","<<seed.index.reservedBytes();
        else if(mode==4) std::cout<<","<<staticSeed.reservedBytes();
        else std::cout<<",0";
        std::cout<<"\n";
    }
}
int main(int argc,char** argv) {
    try {
        verifyIndexLifecycle();
        std::cout<<"variant,entities,query_10x_or_churn_median_ms,checksum,extra_capacity_bytes\n";
        if(argc>1) { size_t n=std::strtoull(argv[1],nullptr,10); run(n); churn(n); }
        else for(size_t n:{size_t(10000),size_t(100000),size_t(1000000)}) { run(n); churn(n); }
    } catch(const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1;
    }
}
