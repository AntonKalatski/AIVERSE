// Genre-neutral, single-worker CPU import queue for native model sources.
// No GPU calls on workers; all publication happens on the device owner thread.
// Reader callbacks MUST remain thread-safe and independent of mutable project
// state. Revisions prevent stale work from being published after invalidation.
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
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render {

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
        uint64_t finished=0, discarded=0;
    };
    AsyncModelCpuQueue()=default;
    AsyncModelCpuQueue(const AsyncModelCpuQueue&)=delete;
    AsyncModelCpuQueue& operator=(const AsyncModelCpuQueue&)=delete;
    ~AsyncModelCpuQueue(){shutdown();}

    void setMaxQueued(size_t count) noexcept {maxQueued_=count;}
    size_t maxQueued() const noexcept{return maxQueued_;}
    Stats stats() const noexcept {
        return {queued_.size(),active_.has_value(),finished_,discarded_};
    }
    bool contains(const std::string& source) const {
        if(active_ && active_->source==source)return true;
        return std::any_of(queued_.begin(),queued_.end(),
            [&](const Request& r){return r.source==source;});
    }

    bool request(std::string source,FileReader reader,
                 std::shared_ptr<ModelTextureCache> textures) {
        if(source.empty() || !reader || !textures || contains(source) ||
           (maxQueued_!=0 && queued_.size()>=maxQueued_))
            return false;
        const uint64_t revision=revisions_[source];
        queued_.push_back({std::move(source),revision,
                           std::move(reader),std::move(textures)});
        startNext();
        return true;
    }

    // The in-flight task may finish on its worker; its result is discarded
    // if its revision no longer matches. This does not block the caller.
    void invalidate(const std::string& source) {
        ++revisions_[source];
        for(auto it=queued_.begin();it!=queued_.end();)
            if(it->source==source)it=queued_.erase(it);
            else ++it;
    }
    bool pollReady(Prepared& output) {
        if(!pending_.valid() ||
           pending_.wait_for(std::chrono::seconds(0))!=
               std::future_status::ready)return false;
        Prepared next=pending_.get();
        active_.reset();
        startNext();
        if(next.revision!=revisions_[next.source]) {
            ++discarded_;
            return false;
        }
        ++finished_;
        output=std::move(next);
        return true;
    }

    void shutdown() {
        queued_.clear();
        if(pending_.valid())pending_.wait();
        pending_={};
        active_.reset();
        revisions_.clear();
    }
private:
    struct Request {
        std::string source;
        uint64_t revision;
        FileReader reader;
        std::shared_ptr<ModelTextureCache> textures;
    };
    void startNext() {
        if(pending_.valid() || queued_.empty())return;
        active_=std::move(queued_.front());
        queued_.pop_front();
        const Request request=*active_;
        pending_=std::async(std::launch::async,[request]() mutable {
            Prepared result;
            result.source=request.source;
            result.revision=request.revision;
            try {
                result.model=loadModel(request.source,request.reader,
                                       &result.warnings,request.textures);
                if(result.model)prepareModelUpload(*result.model);
            }catch(const std::exception& e) {
                result.warnings.push_back(request.source+": "+e.what());
                result.model.reset();
            }
            return result;
        });
    }
    std::deque<Request> queued_;
    std::optional<Request> active_;
    std::future<Prepared> pending_;
    std::map<std::string,uint64_t> revisions_;
    size_t maxQueued_=16;
    uint64_t finished_=0,discarded_=0;
};

} // namespace aiverse::render
