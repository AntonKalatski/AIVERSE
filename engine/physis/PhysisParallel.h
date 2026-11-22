// PHYSIS parallel grid update (2026-11-22).
//
// The per-tick grid passes (atmosphere, surface, disasters read/compute/commit)
// write each cell exactly once from values that do not depend on other cells'
// writes in the same pass, so they can run on several threads and still give
// BIT-IDENTICAL results to the serial order: no reductions over floats, no
// shared accumulators (flood transfer is gathered per receiving cell, integer
// sums are order independent). Tests: tests/physis_parallel_test.cpp.
//
// Opt-in and ambient: a host installs an executor for the duration of one
// PHYSIS system call with ScopedCellExecutor; every pass then splits its cell
// range. Without an executor (default) everything runs serially as before.
// physis::install creates one ThreadPoolCellExecutor when
// InstallOptions::workerThreads != 1 (map "physis.workerThreads").
//
// Rules for custom forcing / surface providers passed to advanceWith while an
// executor is active: they are called concurrently and must only READ World
// (tryGetConst / eachExistingConst / resourceConst), like the built-in ones.
#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace aiverse::physis {

using CellRangeFn = std::function<void(std::size_t begin, std::size_t end)>;

class CellExecutor {
public:
    virtual ~CellExecutor() = default;
    virtual unsigned lanes() const = 0;
    // Calls fn over disjoint ranges covering [0, count); returns when all are
    // done. Rethrows the first exception thrown by fn.
    virtual void run(std::size_t count, std::size_t grain, const CellRangeFn& fn) const = 0;
};

namespace parallel_detail {
inline thread_local const CellExecutor* tlsExecutor = nullptr;
} // namespace parallel_detail

inline const CellExecutor* currentCellExecutor() { return parallel_detail::tlsExecutor; }

class ScopedCellExecutor {
public:
    explicit ScopedCellExecutor(const CellExecutor* executor)
        : previous_(parallel_detail::tlsExecutor) { parallel_detail::tlsExecutor = executor; }
    ~ScopedCellExecutor() { parallel_detail::tlsExecutor = previous_; }
    ScopedCellExecutor(const ScopedCellExecutor&) = delete;
    ScopedCellExecutor& operator=(const ScopedCellExecutor&) = delete;
private:
    const CellExecutor* previous_;
};

// Below this many cells per lane a pass stays serial (thread wake-up costs
// more than the work).
inline constexpr std::size_t kMinParallelCells = 2048;

// True when the next forEachCellRange(count) would actually split.
inline bool cellPassIsParallel(std::size_t count) {
    const auto* e = currentCellExecutor();
    return e && e->lanes() > 1 && count >= 2 * kMinParallelCells;
}

template<class Fn>
inline void forEachCellRange(std::size_t count, Fn&& fn) {
    if (!cellPassIsParallel(count)) { if (count) fn(std::size_t(0), count); return; }
    const auto* e = currentCellExecutor();
    const std::size_t grain = std::max(kMinParallelCells / 2, count / (std::size_t(e->lanes()) * 4));
    e->run(count, grain, [&fn](std::size_t b, std::size_t end) { fn(b, end); });
}

// Persistent worker threads; the calling thread works too. Falls back to a
// single lane when threads cannot be created (e.g. WebAssembly without
// pthreads), which keeps results identical.
class ThreadPoolCellExecutor final : public CellExecutor {
public:
    // workers: total lanes incl. the caller; 0 = hardware concurrency (max 16).
    explicit ThreadPoolCellExecutor(unsigned workers = 0) {
        unsigned want = workers ? workers : std::thread::hardware_concurrency();
        want = std::clamp(want, 1u, 16u);
        try {
            for (unsigned i = 1; i < want; ++i) threads_.emplace_back([this] { loop(); });
        } catch (...) {
            // keep the lanes that started
        }
        lanes_ = unsigned(threads_.size()) + 1;
    }
    ~ThreadPoolCellExecutor() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }
        wake_.notify_all();
        for (auto& t : threads_) t.join();
    }
    ThreadPoolCellExecutor(const ThreadPoolCellExecutor&) = delete;
    ThreadPoolCellExecutor& operator=(const ThreadPoolCellExecutor&) = delete;

    unsigned lanes() const override { return lanes_; }

    void run(std::size_t count, std::size_t grain, const CellRangeFn& fn) const override {
        if (count == 0) return;
        if (lanes_ <= 1 || count <= grain) { fn(0, count); return; }
        std::lock_guard<std::mutex> serialize(runMutex_);   // one job at a time
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job_ = &fn; count_ = count; grain_ = std::max<std::size_t>(1, grain);
            next_.store(0, std::memory_order_relaxed);
            busy_ = unsigned(threads_.size());
            error_ = nullptr;
            ++generation_;
        }
        wake_.notify_all();
        work();   // the caller is a lane too
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this] { return busy_ == 0; });
        job_ = nullptr;
        if (error_) std::rethrow_exception(error_);
    }

private:
    void work() const {
        for (;;) {
            const std::size_t begin = next_.fetch_add(grain_, std::memory_order_relaxed);
            if (begin >= count_) return;
            const std::size_t end = std::min(count_, begin + grain_);
            try {
                (*job_)(begin, end);
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!error_) error_ = std::current_exception();
                next_.store(count_, std::memory_order_relaxed);   // stop handing out work
            }
        }
    }
    void loop() const {
        std::uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return quit_ || generation_ != seen; });
                if (quit_) return;
                seen = generation_;
            }
            work();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (--busy_ == 0) done_.notify_one();
            }
        }
    }

    std::vector<std::thread> threads_;
    unsigned lanes_ = 1;
    mutable std::mutex mutex_, runMutex_;
    mutable std::condition_variable wake_, done_;
    mutable const CellRangeFn* job_ = nullptr;
    mutable std::size_t count_ = 0, grain_ = 1;
    mutable std::atomic<std::size_t> next_{0};
    mutable unsigned busy_ = 0;
    mutable std::uint64_t generation_ = 0;
    mutable std::exception_ptr error_;
    bool quit_ = false;
};

} // namespace aiverse::physis
