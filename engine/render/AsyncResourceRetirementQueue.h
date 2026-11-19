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
    struct Stats {
        size_t pendingResources=0;
        size_t pendingBatches=0;
        size_t unsubmittedResources=0;
        size_t highWatermark=0;
        uint64_t completedResources=0;
        uint64_t completedBatches=0;
        uint64_t pendingPolls=0;
        uint64_t tokenFailures=0;
        uint64_t idleFallbackFailures=0;
    };
    void enqueue(Resource resource) {
        pending_.push_back(std::move(resource));
        ++count_;
        if(count_>highWatermark_)highWatermark_=count_;
    }
    size_t pending() const noexcept {return count_;}
    size_t pendingBatches() const noexcept {return batches_.size();}
    bool empty() const noexcept {return count_==0;}
    // Advisory backpressure for new allocations, never a reason to
    // destroy a model before its own GPU token completes.
    bool saturated(size_t limit) const noexcept {
        return limit!=0 && count_>=limit;
    }
    Stats stats() const noexcept {
        return {count_,batches_.size(),pending_.size(),highWatermark_,
                completedResources_,completedBatches_,pendingPolls_,
                tokenFailures_,idleFallbackFailures_};
    }
    // Read-only diagnostic traversal. The estimate callback receives a
    // const Resource&, may return approximate bytes, and must have no GPU
    // side effects. Both token-assigned and unsubmitted resources count.
    template<class Estimate>
    uint64_t estimatedPendingBytes(Estimate&& estimate) const {
        uint64_t bytes=0;
        const auto accumulate=[&](const Resource& resource) {
            const auto amount=uint64_t(estimate(resource));
            if(amount>UINT64_MAX-bytes)bytes=UINT64_MAX;
            else bytes+=amount;
        };
        for(const auto& resource:pending_)accumulate(resource);
        for(const auto& batch:batches_)
            for(const auto& resource:batch.resources)accumulate(resource);
        return bytes;
    }

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
            if(!completed(batch.token)) {
                ++pendingPolls_;
                break;
            }
            close(batch.token);
            for(auto& resource:batch.resources) {
                destroy(resource);
                --count_;
                ++completedResources_;
            }
            ++completedBatches_;
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
        ++tokenFailures_;
        if(!waitIdle()) {
            ++idleFallbackFailures_;
            return false;
        }
        drainKnownCompleted(close,destroy);
        return true;
    }

    // Only a successful full-device completion barrier may bypass in-flight
    // per-batch completion checks (e.g., application shutdown).
    template<class Wait,class Close,class Destroy>
    bool drain(Wait&& waitIdle,Close&& close,Destroy&& destroy) {
        if(empty())return true;
        if(!waitIdle()) {
            ++idleFallbackFailures_;
            return false;
        }
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
                ++completedResources_;
            }
            ++completedBatches_;
        }
        batches_.clear();
        for(auto& resource:pending_) {
            destroy(resource);
            --count_;
            ++completedResources_;
        }
        pending_.clear();
    }
    size_t count_=0,highWatermark_=0;
    uint64_t completedResources_=0,completedBatches_=0;
    uint64_t pendingPolls_=0,tokenFailures_=0,idleFallbackFailures_=0;
    std::vector<Resource> pending_;
    std::deque<Batch> batches_;
};

} // namespace aiverse::render
