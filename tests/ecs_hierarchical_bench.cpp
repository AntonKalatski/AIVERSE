// AIVERSE ECS architecture microbench: hierarchical bitmap + sparse vs static-slot.
// Standalone C++20: c++ -std=c++20 -O3 -DNDEBUG -pthread tests/ecs_hierarchical_bench.cpp -o /tmp/aiverse-ecs-hier
// /tmp/aiverse-ecs-hier [count] (default: 10000, 100000, 1000000)
// Does not modify production ECS. Capacity estimates are allocations for this prototype only.
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
struct Position { int64_t x=0,y=0,z=0; };
struct Velocity { int64_t x=1,y=2,z=3; };
static volatile uint64_t observed=0;

struct HierMask {
    // 64-bit blocks, 256-entity segments (4 blocks), 4096-entity chunks (16 segments).
    // Summary bit 1 means a child has at least one set entity bit.
    std::vector<uint64_t> blocks, segments, chunks;
    void reserveIds(size_t n) {
        blocks.resize((n+63)/64);
        segments.resize((blocks.size()+3)/4);
        chunks.resize((segments.size()+15)/16);
    }
    void set(uint32_t id) {
        const size_t b=id>>6, s=b>>2, c=s>>4;
        if(c>=chunks.size()) reserveIds(size_t(id)+1);
        blocks[b] |= uint64_t(1)<<(id&63);
        segments[s] |= uint64_t(1)<<(b&3);
        chunks[c] |= uint64_t(1)<<(s&15);
    }
    void clear(uint32_t id) {
        const size_t b=id>>6, s=b>>2, c=s>>4;
        if(b>=blocks.size()) return;
        blocks[b] &= ~(uint64_t(1)<<(id&63));
        if(blocks[b]==0) {
            segments[s] &= ~(uint64_t(1)<<(b&3));
            if(segments[s]==0) chunks[c] &= ~(uint64_t(1)<<(s&15));
        }
    }
    bool has(uint32_t id) const {
        const size_t b=id>>6;
        return b<blocks.size() && (blocks[b]&(uint64_t(1)<<(id&63)))!=0;
    }
    size_t bytes() const {
        return (blocks.capacity()+segments.capacity()+chunks.capacity())*sizeof(uint64_t);
    }
    template<class F> static void eachAll(const HierMask& a,const HierMask& b,F&& fn) {
        // Summary levels skip groups where neither component can match.
        const size_t count=std::min(a.chunks.size(),b.chunks.size());
        for(size_t c=0;c<count;++c) {
            uint64_t segs=a.chunks[c]&b.chunks[c];
            while(segs) {
                const size_t s=c*16+std::countr_zero(segs);
                uint64_t blks=a.segments[s]&b.segments[s];
                while(blks) {
                    const size_t block=s*4+std::countr_zero(blks);
                    uint64_t bits=a.blocks[block]&b.blocks[block];
                    while(bits) {
                        fn(uint32_t(block*64+std::countr_zero(bits)));
                        bits &= bits-1;
                    }
                    blks &= blks-1;
                }
                segs &= segs-1;
            }
        }
    }
    template<class F> static void eachAllNone(const HierMask& a,const HierMask& b,const HierMask& excluded,F&& fn) {
        eachAll(a,b,[&](uint32_t id){if(!excluded.has(id)) fn(id);});
    }
};

struct SparsePool {
    static constexpr uint32_t kMissing=UINT32_MAX;
    std::vector<uint32_t> sparse,dense;
    std::vector<Velocity> data;
    void reserveIds(size_t n) { sparse.assign(n,kMissing); }
    bool has(uint32_t id) const {return id<sparse.size()&&sparse[id]!=kMissing;}
    void add(uint32_t id,Velocity v={}) {
        if(has(id)){data[sparse[id]]=v; return;}
        if(id>=sparse.size()) sparse.resize(size_t(id)+1,kMissing);
        sparse[id]=uint32_t(dense.size()); dense.push_back(id);data.push_back(v);
    }
    void remove(uint32_t id) {
        if(!has(id))return;
        const uint32_t p=sparse[id], last=uint32_t(dense.size()-1);
        if(p!=last){dense[p]=dense[last];data[p]=data[last];sparse[dense[p]]=p;}
        dense.pop_back();data.pop_back();sparse[id]=kMissing;
    }
    Velocity& at(uint32_t id){return data[sparse[id]];}
    size_t bytes() const {
        return sparse.capacity()*sizeof(uint32_t)+dense.capacity()*sizeof(uint32_t)+data.capacity()*sizeof(Velocity);
    }
};

struct SparseWorld {
    std::vector<Position> pos;
    SparsePool vel;
    HierMask posMask,velMask,excluded;
    void build(size_t n,unsigned density) {
        pos.resize(n);vel.reserveIds(n);
        posMask.reserveIds(n);velMask.reserveIds(n);excluded.reserveIds(n);
        for(uint32_t id=0;id<n;++id) {
            pos[id].x=id;posMask.set(id);
            if(id%100<density){vel.add(id);velMask.set(id);}
            if(id%10==0)excluded.set(id);
        }
    }
    void update() {
        for(size_t i=0;i<vel.dense.size();++i) {
            const uint32_t id=vel.dense[i];
            const Velocity& v=vel.data[i];Position& p=pos[id];
            p.x+=v.x;p.y+=v.y;p.z+=v.z;
        }
    }
    void updateBitmap() {
        HierMask::eachAll(posMask,velMask,[&](uint32_t id){
            const Velocity& v=vel.at(id);Position& p=pos[id];
            p.x+=v.x;p.y+=v.y;p.z+=v.z;
        });
    }
    void updateFiltered() {
        HierMask::eachAllNone(posMask,velMask,excluded,[&](uint32_t id){
            const Velocity& v=vel.at(id);Position& p=pos[id];
            p.x+=v.x;p.y+=v.y;p.z+=v.z;
        });
    }
    size_t bytes() const {return pos.capacity()*sizeof(Position)+vel.bytes()+posMask.bytes()+velMask.bytes()+excluded.bytes();}
};

struct SlotWorld {
    std::vector<Position> pos;
    std::vector<Velocity> vel;
    HierMask posMask,velMask,excluded;
    void build(size_t n,unsigned density) {
        pos.resize(n);vel.resize(n);
        posMask.reserveIds(n);velMask.reserveIds(n);excluded.reserveIds(n);
        for(uint32_t id=0;id<n;++id){
            pos[id].x=id;posMask.set(id);
            if(id%100<density)velMask.set(id);
            if(id%10==0)excluded.set(id);
        }
    }
    void update() {
        HierMask::eachAll(posMask,velMask,[&](uint32_t id){
            const Velocity& v=vel[id];Position& p=pos[id];
            p.x+=v.x;p.y+=v.y;p.z+=v.z;
        });
    }
    void updateFiltered() {
        HierMask::eachAllNone(posMask,velMask,excluded,[&](uint32_t id){
            const Velocity& v=vel[id];Position& p=pos[id];
            p.x+=v.x;p.y+=v.y;p.z+=v.z;
        });
    }
    // Disjoint 4096-entity chunks: deterministic independent update with no shared writes.
    void updateParallel(unsigned threads) {
        if(threads<=1){update();return;}
        const size_t chunks=posMask.chunks.size();
        const unsigned workers=std::min<unsigned>(threads,unsigned(chunks));
        std::vector<std::thread> pool;
        pool.reserve(workers);
        for(unsigned worker=0;worker<workers;++worker)pool.emplace_back([&,worker]{
            for(size_t c=worker;c<chunks;c+=workers) {
                uint64_t ss=posMask.chunks[c]&velMask.chunks[c];
                while(ss){
                    size_t s=c*16+std::countr_zero(ss);
                    uint64_t bb=posMask.segments[s]&velMask.segments[s];
                    while(bb) {
                        size_t block=s*4+std::countr_zero(bb);
                        uint64_t bits=posMask.blocks[block]&velMask.blocks[block];
                        while(bits) {
                            uint32_t id=uint32_t(block*64+std::countr_zero(bits));
                            const auto& v=vel[id];auto& p=pos[id];
                            p.x+=v.x;p.y+=v.y;p.z+=v.z;
                            bits&=bits-1;
                        }
                        bb&=bb-1;
                    }
                    ss&=ss-1;
                }
            }
        });
        for(auto& worker:pool)worker.join();
    }
    size_t bytes() const {return pos.capacity()*sizeof(Position)+vel.capacity()*sizeof(Velocity)+posMask.bytes()+velMask.bytes()+excluded.bytes();}
};

static uint64_t hashPositions(const std::vector<Position>& data) {
    uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<data.size();++i) {
        for(uint64_t v:{uint64_t(i),uint64_t(data[i].x),uint64_t(data[i].y),uint64_t(data[i].z)})
            h=(h^v)*1099511628211ull;
    }
    return h;
}
template<class F> static double timed(F&& f) {
    const auto t=Clock::now();f();
    return std::chrono::duration<double,std::milli>(Clock::now()-t).count();
}
static double median(std::vector<double> values) {
    std::sort(values.begin(),values.end());return values[values.size()/2];
}
template<class F> static double sample(F&& fn) {
    std::vector<double> values;
    for(int rep=-2;rep<7;++rep) {
        const double v=fn();
        if(rep>=0)values.push_back(v);
    }
    return median(values);
}
static void run(size_t n,unsigned density) {
    SparseWorld sparse;SlotWorld slots;
    sparse.build(n,density);slots.build(n,density);
    if(hashPositions(sparse.pos)!=hashPositions(slots.pos)) throw std::runtime_error("seed hash mismatch");
    uint64_t expected=0;
    auto bench=[&](const char* name,auto seed,auto update) {
        double ms=sample([&](){
            auto w=seed;
            auto time=timed([&](){for(int i=0;i<10;++i)update(w);});
            const uint64_t h=hashPositions(w.pos);
            if(expected==0)expected=h;
            if(h!=expected)throw std::runtime_error("query checksum mismatch");
            observed=observed^h;
            return time;
        });
        std::cout<<name<<","<<n<<","<<density<<","<<ms<<","<<seed.bytes()<<","<<expected<<"\n";
    };
    bench("sparse-dense",sparse,[](SparseWorld& w){w.update();});
    bench("sparse-hierbitmap",sparse,[](SparseWorld& w){w.updateBitmap();});
    bench("static-hierbitmap",slots,[](SlotWorld& w){w.update();});
    bench("static-hierbitmap-2threads",slots,[](SlotWorld& w){w.updateParallel(2);});

    // Filtered All(Position,Velocity) + None(Excluded). Compare final states.
    const auto s=sparse;
    auto a=s,b=slots;
    for(int i=0;i<10;++i){a.updateFiltered();b.updateFiltered();}
    if(hashPositions(a.pos)!=hashPositions(b.pos))throw std::runtime_error("None filter mismatch");
    observed=observed^hashPositions(a.pos);

    // Snapshot copy overhead and correctness: all buffers copied, not delta snapshots.
    double sparseCopy=sample([&](){
        SparseWorld copy;
        double ms=timed([&](){copy=sparse;});
        if(hashPositions(copy.pos)!=hashPositions(sparse.pos))throw std::runtime_error("sparse snapshot");
        return ms;
    });
    double slotsCopy=sample([&](){
        SlotWorld copy;
        double ms=timed([&](){copy=slots;});
        if(hashPositions(copy.pos)!=hashPositions(slots.pos))throw std::runtime_error("slot snapshot");
        return ms;
    });
    std::cout<<"sparse-snapshot,"<<n<<","<<density<<","<<sparseCopy<<","<<sparse.bytes()<<",0\n";
    std::cout<<"static-snapshot,"<<n<<","<<density<<","<<slotsCopy<<","<<slots.bytes()<<",0\n";

    // Component add/remove churn. Measure both data and mask updates for each world.
    auto churnSparse=[&](SparseWorld& w){
        for(uint32_t id=1;id<n;id+=8) {
            w.vel.add(id);w.velMask.set(id);
            w.vel.remove(id);w.velMask.clear(id);
        }
    };
    auto churnStatic=[&](SlotWorld& w){
        for(uint32_t id=1;id<n;id+=8) {
            w.velMask.set(id);w.velMask.clear(id);
        }
    };
    // Churn only absent entities so the initial membership is preserved.
    if(density==1){
        double ca=sample([&](){auto w=sparse;return timed([&](){churnSparse(w);});});
        double cb=sample([&](){auto w=slots;return timed([&](){churnStatic(w);});});
        std::cout<<"sparse-churn,"<<n<<","<<density<<","<<ca<<","<<sparse.bytes()<<",0\n";
        std::cout<<"static-churn,"<<n<<","<<density<<","<<cb<<","<<slots.bytes()<<",0\n";
    }
}
int main(int argc,char** argv) {
    try {
        const size_t n=argc>1?std::strtoull(argv[1],nullptr,10):0;
        std::cout<<"variant,entities,density_percent,median_ms,capacity_bytes,checksum\n";
        if(n)for(unsigned d:{1u,25u,100u})run(n,d);
        else for(size_t count:{size_t(10000),size_t(100000),size_t(1000000)})
            for(unsigned d:{1u,25u,100u})run(count,d);
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
