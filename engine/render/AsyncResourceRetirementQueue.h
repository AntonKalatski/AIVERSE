// Genre-neutral GPU retirement batches. Each batch has its own ordered
// completion token: a newer eviction can never be released by an older fence.
// No polling operation blocks unless the backend lacks async tokens and the
// explicit synchronous fallback is used.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace aiverse::render {

template<class Resource>
class AsyncResourceRetirementQueue final {
public:
    void enqueue(Resource resource) {
        pending_.push_back(std::move(resource));
        ++count_;
    }
    size_t pending() const noexcept {return count_;}
    bool empty() const noexcept {return count_==0;}

    // Invoke only at a real host frame boundary after previous GPU work has
    // been submitted. Callbacks: begin()->token (0=unsupported), completed
    // (token)->bool, close(token), waitIdle()->bool, destroy(Resource&).
    template<class Begin,class Completed,class Close,class Wait,class Destroy>
    bool collect(Begin&& begin,Completed&& completed,Close&& close,
                 Wait&& waitIdle,Destroy&& destroy) {
        // GPU submissions are ordered. Later batches must not be reclaimed
        // ahead of an older uncompleted batch.
        while(!batches_.empty()) {
            auto& batch=batches_.front();
            if(!completed(batch.token))break;
            close(batch.token);
            for(auto& resource:batch.resources) {
                destroy(resource);
                --count_;
            }
            batches_.pop_front();
        }
        if(pending_.empty())return empty();
        const uint64_t token=begin();
        if(token!=0) {
            Batch batch;
            batch.token=token;
            batch.resources=std::move(pending_);
            pending_.clear();
            batches_.push_back(std::move(batch));
            return false; // new batch cannot be assumed complete
        }
        // Unsupported token backend: fall back to one explicit completion
        // barrier; never silently delete potentially GPU-visible resources.
        if(!waitIdle())return false;
        drainKnownCompleted(close,destroy);
        return true;
    }

    // Only a successful full-device completion barrier may bypass in-flight
    // per-batch completion checks (e.g., application shutdown).
    template<class Wait,class Close,class Destroy>
    bool drain(Wait&& waitIdle,Close&& close,Destroy&& destroy) {
        if(empty())return true;
        if(!waitIdle())return false;
        drainKnownCompleted(close,destroy);
        return true;
    }

private:
    struct Batch {
        uint64_t token=0;
        std::vector<Resource> resources;
    };
    template<class Close,class Destroy>
    void drainKnownCompleted(Close&& close,Destroy&& destroy) {
        for(auto& batch:batches_) {
            close(batch.token); // the GPU has been proven idle
            for(auto& resource:batch.resources) {
                destroy(resource);
                --count_;
            }
        }
        batches_.clear();
        for(auto& resource:pending_) {
            destroy(resource);
            --count_;
        }
        pending_.clear();
    }
    size_t count_=0;
    std::vector<Resource> pending_;
    std::deque<Batch> batches_;
};

} // namespace aiverse::render
