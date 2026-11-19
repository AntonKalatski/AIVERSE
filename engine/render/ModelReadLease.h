// Short-lived render-owner-thread reader guard for ModelResourceCache.
//
// This is NOT shared Model ownership and NOT a GPU completion fence.
// A live lease blocks cache eviction/replacement/shutdown; host must
// release it after joined animation evaluation and synchronous draw.
// The host's separate ModelResourceCache GPU queue still retires RHI handles.
#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <utility>

namespace aiverse::render {

struct Model;
class ModelResourceCache;

struct ModelReadLeaseState final {
    std::atomic<size_t> readers{0};
    std::atomic<bool> alive{true};
    std::thread::id ownerThread;
};

class ModelReadLease final {
public:
    ModelReadLease()=default;
    ModelReadLease(const ModelReadLease&)=delete;
    ModelReadLease& operator=(const ModelReadLease&)=delete;
    ModelReadLease(ModelReadLease&& other) noexcept
        :state_(std::move(other.state_)),
         model_(std::exchange(other.model_,nullptr)) {}
    ModelReadLease& operator=(ModelReadLease&& other) noexcept {
        if(this!=&other) {
            reset();
            state_=std::move(other.state_);
            model_=std::exchange(other.model_,nullptr);
        }
        return *this;
    }
    ~ModelReadLease(){reset();}

    const Model* get() const noexcept {
        return state_ && model_ && state_->alive.load(std::memory_order_acquire) &&
               state_->ownerThread==std::this_thread::get_id()
               ?model_:nullptr;
    }
    bool valid() const noexcept{return get()!=nullptr;}
    explicit operator bool() const noexcept{return valid();}
    void reset() noexcept {
        if(state_) {
            // Atomic releases may happen during exception unwinding,
            // but dereferencing is only allowed on the cache owner thread.
            state_->readers.fetch_sub(1,std::memory_order_acq_rel);
            state_.reset();
        }
        model_=nullptr;
    }
private:
    friend class ModelResourceCache;
    ModelReadLease(std::shared_ptr<ModelReadLeaseState> state,
                   const Model* model) noexcept
        :state_(std::move(state)),model_(model) {
        state_->readers.fetch_add(1,std::memory_order_acq_rel);
    }
    std::shared_ptr<ModelReadLeaseState> state_;
    const Model* model_=nullptr; // borrowed; cache mutations blocked while live
};

} // namespace aiverse::render
