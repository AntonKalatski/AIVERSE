#include "StrataStreamingCoordinator.h"
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace aiverse::render::strata {
namespace {
struct GpuLedger {
    uint64_t limit=0, bytes=0, serial=0;
    std::map<uint64_t,uint64_t> leases;
};
std::mutex gpuMutex;
std::map<gfx::Device*,GpuLedger> gpuLedgers;

class IoExecutor final {
public:
    ~IoExecutor() {
        {
            std::lock_guard lock(mutex_);
            stopping_=true;
        }
        jobsCv_.notify_all();
        readyCv_.notify_all();
        for(auto& thread:threads_) if(thread.joinable())thread.join();
    }
    bool configure(uint32_t workers,uint64_t bytes) {
        if(!workers || workers>8)return false;
        if(!bytes)bytes=kDefaultReady;
        if(bytes<4096 || bytes>(uint64_t(1)<<34))return false;
        std::lock_guard lock(mutex_);
        if(started_)return requestedWorkers_==workers && limit_==bytes;
        requestedWorkers_=workers;
        limit_=bytes;
        return true;
    }
    bool submit(std::function<void()> job) {
        if(!job)return false;
        std::unique_lock lock(mutex_);
        if(!startLocked()){
            ++rejected_;
            return false;
        }
        if(stopping_ || jobs_.size()>=kMaxPendingJobs){
            ++rejected_;
            return false;
        }
        jobs_.push_back(std::move(job));
        lock.unlock();
        jobsCv_.notify_one();
        return true;
    }
    bool acquire(size_t bytes,const std::atomic<bool>& cancelled) {
        std::unique_lock lock(mutex_);
        if(bytes>limit_)return false;
        readyCv_.wait(lock,[&] {
            return stopping_ || cancelled.load(std::memory_order_acquire) ||
                bytes <= limit_-ready_;
        });
        if(stopping_ || cancelled.load(std::memory_order_acquire))return false;
        ready_+=bytes;
        return true;
    }
    void release(size_t bytes) {
        {
            std::lock_guard lock(mutex_);
            ready_=bytes>ready_ ? 0 : ready_-bytes;
        }
        readyCv_.notify_all();
    }
    void wake() { readyCv_.notify_all(); }
    SharedDiskIoStats stats() {
        std::lock_guard lock(mutex_);
        return {uint32_t(threads_.size()),uint64_t(jobs_.size()),
                active_,rejected_,ready_,limit_};
    }
private:
    static constexpr size_t kMaxPendingJobs=2048;
    static constexpr uint64_t kDefaultReady=64ull*1024ull*1024ull;
    bool startLocked() {
        if(started_)return true;
        try {
            for(uint32_t i=0;i<requestedWorkers_;++i)
                threads_.emplace_back([this] {run();});
        }catch (const std::system_error&) {
            // Already launched workers are retained; future submissions can
            // still be serviced. Do not detach threads or throw at render time.
            started_=!threads_.empty();
            return started_;
        }
        started_=true;
        return true;
    }
    void run() {
        for(;;){
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                jobsCv_.wait(lock,[&]{return stopping_ || !jobs_.empty();});
                if(stopping_ && jobs_.empty())return;
                job=std::move(jobs_.front());
                jobs_.pop_front();
                ++active_;
            }
            try {job();}catch (...) {
                // Jobs must catch and record their own failure; never kill
                // the process-wide worker on an unexpected bad archive.
            }
            {
                std::lock_guard lock(mutex_);
                --active_;
            }
        }
    }
    std::mutex mutex_;
    std::condition_variable jobsCv_,readyCv_;
    std::deque<std::function<void()>> jobs_;
    std::vector<std::thread> threads_;
    uint32_t requestedWorkers_=2;
    uint64_t limit_=kDefaultReady,ready_=0,active_=0,rejected_=0;
    bool stopping_=false,started_=false;
};
IoExecutor& io() {static IoExecutor executor;return executor;}
} // namespace

bool configureDeviceIndexBudget(gfx::Device& device,uint64_t limitBytes) {
    std::lock_guard lock(gpuMutex);
    auto& ledger=gpuLedgers[&device];
    if(!limitBytes)return true; // no explicit request: inherit existing cap
    if(ledger.limit && ledger.limit!=limitBytes && !ledger.leases.empty())
        return false;
    if(limitBytes<ledger.bytes)return false;
    ledger.limit=limitBytes;
    return true;
}

bool reserveDeviceIndexBytes(gfx::Device& device,uint64_t bytes,
                             uint64_t& outToken) {
    if(!bytes)return false;
    std::lock_guard lock(gpuMutex);
    auto& ledger=gpuLedgers[&device];
    if(bytes>UINT64_MAX-ledger.bytes ||
       (ledger.limit && bytes>ledger.limit-ledger.bytes))
        return false;
    if(ledger.serial==UINT64_MAX)return false;
    uint64_t id=++ledger.serial;
    while(ledger.leases.count(id) && ledger.serial<UINT64_MAX)
        id=++ledger.serial;
    if(ledger.leases.count(id))return false;
    ledger.leases.emplace(id,bytes);
    ledger.bytes+=bytes;
    outToken=id;
    return true;
}

void releaseDeviceIndexBytes(gfx::Device& device,uint64_t token) {
    if(!token)return;
    std::lock_guard lock(gpuMutex);
    const auto it=gpuLedgers.find(&device);
    if(it==gpuLedgers.end())return;
    auto lease=it->second.leases.find(token);
    if(lease==it->second.leases.end())return;
    it->second.bytes-=lease->second;
    it->second.leases.erase(lease);
    // Never retain a dead gfx::Device pointer after the last registered
    // source is released; a later device may reuse the same address.
    if(it->second.leases.empty())gpuLedgers.erase(it);
}

DeviceIndexBudgetStats deviceIndexBudgetStats(gfx::Device& device) {
    std::lock_guard lock(gpuMutex);
    const auto it=gpuLedgers.find(&device);
    if(it==gpuLedgers.end())return {};
    return {it->second.limit,it->second.bytes,
            uint32_t(it->second.leases.size())};
}

bool configureSharedDiskIo(uint32_t workers,uint64_t readyLimitBytes) {
    return io().configure(workers,readyLimitBytes);
}
SharedDiskIoStats sharedDiskIoStats() {return io().stats();}
bool submitSharedDiskJob(std::function<void()> job) {
    return io().submit(std::move(job));
}
bool acquireSharedDiskReady(size_t bytes,const std::atomic<bool>& cancelled) {
    return io().acquire(bytes,cancelled);
}
void releaseSharedDiskReady(size_t bytes) {io().release(bytes);}
void wakeSharedDiskIo() {io().wake();}
} // namespace aiverse::render::strata
