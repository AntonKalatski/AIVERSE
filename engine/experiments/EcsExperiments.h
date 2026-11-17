// Experimental ECS primitives for benchmarking, not wired into production Simulation.
// This header intentionally leaves World::each() ordering and storage unchanged.
#pragma once
#include "../core/ECS.h"
#include <algorithm>
#include <condition_variable>
#include <exception>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace aiverse::ecs_experiment {

// A prepared query carries type-level membership and resolves current pools on execution.
// Its iteration order equals World::each<A,B>(), including after structural changes.
template<class A, class B>
struct PreparedAll {
    template<class Fn> void each(World& world, Fn&& fn) const {
        if (world.countExisting<A>() == 0 || world.countExisting<B>() == 0) return;
        world.each<A,B>(std::forward<Fn>(fn));
    }
};

// Parallel operations are SAFE ONLY if each iteration touches its own entity and
// does not mutate pool structure or shared state. Caller owns synchronization for
// any other external data. Same component pool is split into stable index ranges.
class PersistentWorkers {
public:
    explicit PersistentWorkers(unsigned workers):workers_(workers) {
        // If thread creation fails, join the already-created workers before rethrowing.
        try {
            for(unsigned i=0;i<workers_;++i)
                threads_.emplace_back([this,i]{runWorker(i);});
        } catch(...) {
            stopAndJoin();
            throw;
        }
    }
    PersistentWorkers(const PersistentWorkers&)=delete;
    PersistentWorkers& operator=(const PersistentWorkers&)=delete;
    ~PersistentWorkers(){stopAndJoin();}
    unsigned workers() const {return workers_;}

    template<class Fn> void parallelFor(size_t n,Fn&& callback) {
        if (currentPool_==this)
            throw std::logic_error("nested parallelFor on the same worker pool");
        if(n==0)return;
        if(workers_==0){callback(size_t(0),n);return;}
        std::function<void(unsigned)> local;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(active_)throw std::logic_error("parallelFor is not reentrant");
            const size_t lanes=size_t(workers_)+1;
            local=[&callback,n,lanes](unsigned lane) {
                const size_t begin=(n*lane)/lanes;
                const size_t end=(n*(lane+1))/lanes;
                if(begin<end)callback(begin,end);
            };
            fn_=local;
            active_=true;
            finished_=0;
            failure_=nullptr;
            ++epoch_;
        }
        wake_.notify_all();
        const auto* previousPool=currentPool_;
        currentPool_=this;
        try {local(workers_);}
        catch(...) {recordFailure(std::current_exception());}
        currentPool_=previousPool;
        std::exception_ptr failure;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            done_.wait(lock,[this]{return finished_==workers_;});
            failure=failure_;
            active_=false;
            fn_={};
        }
        if(failure)std::rethrow_exception(failure);
    }
private:
    void recordFailure(std::exception_ptr exception) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!failure_)failure_=exception;
    }
    void stopAndJoin() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_=true;
            ++epoch_;
        }
        wake_.notify_all();
        for(auto& t:threads_)if(t.joinable())t.join();
    }
    void runWorker(unsigned index) {
        uint64_t seen=0;
        while(true) {
            std::function<void(unsigned)> fn;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock,[&]{return stopping_ || epoch_!=seen;});
                if(stopping_)return;
                seen=epoch_;
                fn=fn_;
            }
            try {
                const auto* previous=currentPool_;
                currentPool_=this;
                try {fn(index);} catch(...) {currentPool_=previous;throw;}
                currentPool_=previous;
            } catch(...) {recordFailure(std::current_exception());}
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++finished_;
                if(finished_==workers_)done_.notify_one();
            }
        }
    }
    static inline thread_local const PersistentWorkers* currentPool_=nullptr;
    const unsigned workers_;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable wake_,done_;
    std::function<void(unsigned)> fn_;
    std::exception_ptr failure_;
    uint64_t epoch_=0;
    unsigned finished_=0;
    bool active_=false,stopping_=false;
};

enum class ExecutionMode { Sequential, Parallel, Auto };
struct ParallelPolicy {
    // Conservative default; tune per system using real frame profiling.
    size_t minimumEntities=8192;
    size_t minimumPerLane=2048;
};
template<class A,class Fn>
void eachAdaptive(World& world,PersistentWorkers& workers,ExecutionMode mode,
                  ParallelPolicy policy,Fn&& fn) {
    const size_t n=world.countExisting<A>();
    const size_t lanes=size_t(workers.workers())+1;
    const bool useParallel=workers.workers()>0 &&
        (mode==ExecutionMode::Parallel ||
         (mode==ExecutionMode::Auto && n>=policy.minimumEntities &&
          n/lanes>=policy.minimumPerLane));
    if(!useParallel) {
        world.eachExistingRange<A>(0,n,std::forward<Fn>(fn));
        return;
    }
    workers.parallelFor(n,[&](size_t begin,size_t end){
        world.eachExistingRange<A>(begin,end,fn);
    });
}

// This helper follows the existing FIRST pool's dense order.
// No filtered query: use only when all entries in A are eligible or filter in fn.
// Do not add/remove/destroy any component while parallelFor is in progress.
template<class A,class Fn>
void eachParallel(World& world,PersistentWorkers& workers,Fn&& fn) {
    const size_t count=world.countExisting<A>();
    workers.parallelFor(count,[&](size_t begin,size_t end) {
        world.eachExistingRange<A>(begin,end,fn);
    });
}

// Structural commands are recorded with a stable producer lane and local sequence.
// Replay is ordered (lane, sequence). Actual mutation executes on the caller thread
// outside any active query. Commands must own captured data, never entity references.
class DeterministicCommands {
    struct Entry {
        uint32_t lane;
        uint64_t sequence;
        std::function<void(World&)> apply;
    };
    // Each lane has exactly one producer thread until the next flush/discard.
    // A lane mutex protects the vector against accidental simultaneous calls;
    // a different producer fails immediately instead of silently changing order.
    std::vector<std::vector<Entry>> lanes_;
    std::vector<std::mutex> locks_;
    std::vector<std::thread::id> owner_;
public:
    explicit DeterministicCommands(size_t lanes)
        :lanes_(lanes),locks_(lanes),owner_(lanes) {}
    DeterministicCommands(const DeterministicCommands&)=delete;
    DeterministicCommands& operator=(const DeterministicCommands&)=delete;
    template<class Fn> void enqueue(size_t lane,Fn&& command) {
        if(lane>=lanes_.size())throw std::out_of_range("command lane");
        std::lock_guard<std::mutex> lock(locks_[lane]);
        const auto me=std::this_thread::get_id();
        if(owner_[lane]==std::thread::id{})owner_[lane]=me;
        if(owner_[lane]!=me)
            throw std::logic_error("multiple producer threads on deterministic command lane");
        auto& list=lanes_[lane];
        list.push_back({uint32_t(lane),uint64_t(list.size()),std::forward<Fn>(command)});
    }
    void flush(World& world) {
        // Contract: all producer jobs have already joined. Locking prevents
        // an accidental producer from corrupting the queue, but the caller must
        // never flush concurrently with active producers.
        std::vector<std::vector<Entry>> batch(lanes_.size());
        for(size_t lane=0;lane<lanes_.size();++lane) {
            std::lock_guard<std::mutex> lock(locks_[lane]);
            batch[lane].swap(lanes_[lane]);
            owner_[lane]=std::thread::id{};
        }
        // Lane-major, per-lane submission order. Commands submitted from a
        // callback are queued for the next flush, not appended to this batch.
        for(const auto& list:batch)
            for(const auto& entry:list)entry.apply(world);
        world.flush();
    }
    void discard() {
        for(size_t lane=0;lane<lanes_.size();++lane) {
            std::lock_guard<std::mutex> lock(locks_[lane]);
            lanes_[lane].clear();
            owner_[lane]=std::thread::id{};
        }
    }
};

// Page-level delta for a trivially-copyable component array. The caller records
// all page writes through mark(); untracked writes invalidate the delta.
// Not a full World rollback: entity allocator and nontrivial components are excluded.
template<class T,size_t ElementsPerPage=256>
class DirtyPageSnapshot {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(ElementsPerPage>0);
    struct Page { size_t page; std::vector<T> values; };
    std::vector<uint8_t> dirty_;
    std::vector<Page> pages_;
    size_t size_=0;
public:
    void begin(size_t count) {
        size_=count;
        dirty_.assign((count+ElementsPerPage-1)/ElementsPerPage,0);
        pages_.clear();
    }
    void mark(size_t index) {
        if(index>=size_) throw std::out_of_range("snapshot dirty index");
        dirty_[index/ElementsPerPage]=1;
    }
    void capture(const std::vector<T>& source) {
        if(source.size()!=size_) throw std::logic_error("snapshot source size changed");
        pages_.clear();
        for(size_t p=0;p<dirty_.size();++p) if(dirty_[p]) {
            size_t begin=p*ElementsPerPage;
            size_t end=std::min(source.size(),begin+ElementsPerPage);
            pages_.push_back({p,std::vector<T>(source.begin()+begin,source.begin()+end)});
        }
    }
    void apply(std::vector<T>& destination) const {
        if(destination.size()!=size_) throw std::logic_error("snapshot destination size changed");
        for(const auto& page:pages_) {
            std::copy(page.values.begin(),page.values.end(),
                      destination.begin()+page.page*ElementsPerPage);
        }
    }
    size_t bytes() const {
        size_t result=0;
        for(const auto& p:pages_) result+=p.values.size()*sizeof(T);
        return result;
    }
    size_t pageCount() const {return pages_.size();}
};
} // namespace aiverse::ecs_experiment
