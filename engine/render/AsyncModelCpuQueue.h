// Genre-neutral native multi-worker CPU import scheduler for model sources.
// Reader callbacks and texture CPU caches MUST be thread-safe.
// GPU upload, render packets, and ECS presentation NEVER run on workers.
// Owner-thread requests, invalidation, dispatch and publication are serialized.
#pragma once
#include "Model.h"
#include "ModelTextureCache.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class ModelLoadPriority : uint8_t {
    Background=0, Visible=1, Interactive=2
};

class AsyncModelCpuQueue final {
public:
    struct Prepared {
        std::string source;
        uint64_t revision=0;
        std::unique_ptr<Model> model;
        std::vector<std::string> warnings;
    };
    struct Stats {
        size_t queued=0;
        bool workerActive=false;
        uint64_t finished=0,discarded=0;
        size_t queuedInteractive=0,queuedVisible=0;
        size_t activeWorkers=0,configuredWorkers=1;
        uint64_t dispatched=0,backgroundFairnessDispatches=0;
    };
    AsyncModelCpuQueue()=default;
    AsyncModelCpuQueue(const AsyncModelCpuQueue&)=delete;
    AsyncModelCpuQueue& operator=(const AsyncModelCpuQueue&)=delete;
    ~AsyncModelCpuQueue(){shutdown();}

    // The worker cap is deliberately bounded to avoid saturating platforms.
    // Lowering it never interrupts an already running CPU decoder.
    void setWorkerCount(size_t workers) {
        workerCount_=std::clamp(workers,size_t(1),size_t(8));
        startAvailable();
    }
    size_t workerCount() const noexcept{return workerCount_;}
    void setMaxQueued(size_t count) noexcept{maxQueued_=count;}
    size_t maxQueued() const noexcept{return maxQueued_;}
    Stats stats() const noexcept {
        Stats result{queued_.size(),!workers_.empty(),finished_,discarded_};
        result.activeWorkers=workers_.size();
        result.configuredWorkers=workerCount_;
        result.dispatched=dispatched_;
        result.backgroundFairnessDispatches=fairnessDispatches_;
        for(const auto& item:queued_) {
            if(item.priority==ModelLoadPriority::Interactive)
                ++result.queuedInteractive;
            if(item.priority==ModelLoadPriority::Visible)
                ++result.queuedVisible;
        }
        return result;
    }
    bool contains(const std::string& source) const {
        const uint64_t revision=currentRevision(source);
        for(const auto& worker:workers_)
            if(worker.request.source==source &&
               worker.request.revision==revision)return true;
        return std::any_of(queued_.begin(),queued_.end(),
            [&](const Request& r) {
                return r.source==source && r.revision==revision;
            });
    }

    bool request(std::string source,FileReader reader,
                 std::shared_ptr<ModelTextureCache> textures,
                 ModelLoadPriority priority=ModelLoadPriority::Visible) {
        if(source.empty() || !reader || !textures)return false;
        const uint64_t revision=currentRevision(source);
        for(const auto& worker:workers_)
            if(worker.request.source==source &&
               worker.request.revision==revision)return false;
        for(auto& item:queued_) {
            if(item.source!=source || item.revision!=revision)continue;
            if(item.priority>=priority)return false;
            item.priority=priority;
            return true; // priority upgrade, no extra worker or queue slot
        }
        if(maxQueued_!=0 && queued_.size()>=maxQueued_)return false;
        queued_.push_back({std::move(source),revision,
                           std::move(reader),std::move(textures),priority});
        startAvailable();
        return true;
    }

    // Invalidation never waits for active workers. Their old revision
    // results are discarded on polling; a new request can begin immediately
    // when the configured worker budget has capacity.
    void invalidate(const std::string& source) {
        ++revisions_[source];
        for(auto it=queued_.begin();it!=queued_.end();) {
            if(it->source==source)it=queued_.erase(it);
            else ++it;
        }
    }

    // Poll ALL active workers; do not block on an older unfinished task
    // while a newer independent job is ready for publication. Return one
    // valid prepared result per call. Stale completions are discarded.
    bool pollReady(Prepared& output) {
        for(size_t i=0;i<workers_.size();) {
            if(workers_[i].future.wait_for(std::chrono::seconds(0))!=
                    std::future_status::ready) {
                ++i;
                continue;
            }
            Prepared finished=workers_[i].future.get();
            workers_.erase(workers_.begin()+static_cast<std::ptrdiff_t>(i));
            startAvailable();
            if(finished.revision!=currentRevision(finished.source)) {
                ++discarded_;
                continue;
            }
            ++finished_;
            output=std::move(finished);
            return true;
        }
        return false;
    }

    void shutdown() {
        queued_.clear();
        for(auto& worker:workers_)
            if(worker.future.valid())worker.future.wait();
        workers_.clear();
        revisions_.clear();
        priorityRun_=0;
    }
private:
    struct Request {
        std::string source;
        uint64_t revision=0;
        FileReader reader;
        std::shared_ptr<ModelTextureCache> textures;
        ModelLoadPriority priority=ModelLoadPriority::Visible;
    };
    struct Worker {
        Request request;
        std::future<Prepared> future;
    };
    uint64_t currentRevision(const std::string& source) const {
        const auto it=revisions_.find(source);
        return it==revisions_.end()?0:it->second;
    }
    // After eight consecutive priority-based dispatches, dispatch the
    // oldest queued request. This bounds starvation of Background jobs
    // while retaining FIFO for same-priority jobs under normal demand.
    size_t chooseNext() {
        if(priorityRun_>=8 && queued_.size()>1) {
            priorityRun_=0;
            ++fairnessDispatches_;
            return 0;
        }
        auto it=std::max_element(queued_.begin(),queued_.end(),
            [](const Request& a,const Request& b) {
                return a.priority<b.priority;
            });
        if(it==queued_.begin())priorityRun_=0;
        else ++priorityRun_;
        return size_t(std::distance(queued_.begin(),it));
    }
    void startAvailable() {
        while(workers_.size()<workerCount_ && !queued_.empty()) {
            const size_t next=chooseNext();
            Request request=std::move(queued_[next]);
            queued_.erase(queued_.begin()+static_cast<std::ptrdiff_t>(next));
            const Request workerInput=request;
            auto future=std::async(std::launch::async,
                [workerInput]() mutable {
                    Prepared result;
                    result.source=workerInput.source;
                    result.revision=workerInput.revision;
                    try {
                        result.model=loadModel(workerInput.source,
                            workerInput.reader,&result.warnings,
                            workerInput.textures);
                        if(result.model)prepareModelUpload(*result.model);
                    }catch(const std::exception& e) {
                        result.warnings.push_back(workerInput.source+
                                                  ": "+e.what());
                        result.model.reset();
                    }
                    return result;
                });
            workers_.push_back({std::move(request),std::move(future)});
            ++dispatched_;
        }
    }

    size_t workerCount_=1,maxQueued_=16;
    std::deque<Request> queued_;
    std::vector<Worker> workers_;
    std::map<std::string,uint64_t> revisions_;
    uint64_t finished_=0,discarded_=0,dispatched_=0;
    uint64_t fairnessDispatches_=0;
    size_t priorityRun_=0;
};

} // namespace aiverse::render
