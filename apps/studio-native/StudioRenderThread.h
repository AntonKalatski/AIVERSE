// Dedicated Scene render thread for the Studio host (the Unreal game thread /
// render thread split, reduced to what the Studio needs). The worker is the only
// thread that touches the Scene and its GPU resources (the engine's model caches
// bind to one owner thread). The UI thread
//   - publishes the latest frame inputs and calls kick(): the frame runs on the
//     worker; kicks during a running frame coalesce into one more frame;
//   - runs everything else (picking, camera, gizmos, character preview) through
//     invoke(): the call runs on the worker between frames, the UI thread waits
//     for its result, exceptions are rethrown to the caller.
// threaded=false runs both on the caller (diagnostic fallback). Portable C++.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <stdexcept>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace aiverse::studio {

class StudioRenderThread {
public:
    explicit StudioRenderThread(std::function<void()> frame, bool threaded=true)
        : frame_(std::move(frame)), threaded_(threaded) {
        if (threaded_) {
            worker_=std::thread([this]{ run(); });
            workerId_.store(worker_.get_id(),std::memory_order_release);
        }
    }
    ~StudioRenderThread() { stop(); }
    StudioRenderThread(const StudioRenderThread&) = delete;
    StudioRenderThread& operator=(const StudioRenderThread&) = delete;

    bool threaded() const noexcept { return threaded_; }
    bool onWorker() const noexcept {
        return threaded_ && std::this_thread::get_id()==workerId_.load(std::memory_order_acquire);
    }

    // Request one more frame. Threaded: never blocks on a running frame.
    void kick() {
        if (!threaded_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return;
            }
            runFrame();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return;
            pending_=true;
        }
        wake_.notify_one();
    }

    // Run fn on the worker (after the running frame) and return its result.
    template<class F>
    auto invoke(F&& fn) -> std::invoke_result_t<F&> {
        using R=std::invoke_result_t<F&>;
        if (!threaded_ || onWorker()) return fn();
        auto task=std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
        auto result=task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) throw std::runtime_error("Studio render thread stopped");
            tasks_.push_back([task]{ (*task)(); });
        }
        wake_.notify_one();
        return result.get();
    }

    // Wait until every kick and call issued before this has finished.
    void flush() {
        if (!threaded_ || onWorker()) return;
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock,[this]{ return stopping_ || (!pending_ && !busy_ && tasks_.empty()); });
    }
    // Finish the running frame and queued calls, drop pending kicks, join. Idempotent.
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_=true;
            pending_=false;
        }
        wake_.notify_all();
        if (worker_.joinable() && std::this_thread::get_id()!=worker_.get_id()) worker_.join();
        idle_.notify_all();
    }
    uint64_t framesRun() const { std::lock_guard<std::mutex> lock(mutex_); return frames_; }

private:
    void runFrame() {
        // The callback owns its exceptions; a stray one must not kill the worker.
        try { frame_(); } catch (...) {}
        std::lock_guard<std::mutex> lock(mutex_);
        ++frames_;
    }
    void run() {
        workerId_.store(std::this_thread::get_id(),std::memory_order_release);
        for (;;) {
            std::deque<std::function<void()>> tasks;
            bool frame=false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock,[this]{ return stopping_ || pending_ || !tasks_.empty(); });
                tasks.swap(tasks_);
                if (tasks.empty() && stopping_) break;
                frame=pending_ && !stopping_;
                pending_=false;
                busy_=true;
            }
            // Calls first: the UI thread is waiting on them.
            for (auto& task:tasks) task();
            if (frame) runFrame();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                busy_=false;
            }
            idle_.notify_all();
        }
        idle_.notify_all();
    }

    std::function<void()> frame_;
    const bool threaded_;
    mutable std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::deque<std::function<void()>> tasks_;
    bool pending_=false, busy_=false, stopping_=false;
    uint64_t frames_=0;
    std::atomic<std::thread::id> workerId_{};
    std::thread worker_;
};

} // namespace aiverse::studio
