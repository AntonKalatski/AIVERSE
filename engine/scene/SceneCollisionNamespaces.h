// AIVERSE additive scene collision IDs without aliasing live SpatialWorld
// collider IDs. Reuses the existing world::CollisionStream and its async,
// hashed chunk reader; adds only a deterministic namespace reservation layer.
//
// The catalog is per SpatialWorld and built from canonical scene instance
// keys plus conservative maximum chunk counts BEFORE gameplay begins.
// Never assign namespaces in completion order or recycle an ID range mid-game.
#pragma once
#include "engine/world/CollisionStream.h"
#include "engine/core/Hash.h"
#include "engine/scene/SceneAsset.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace aiverse::scene {

class SceneCollisionNamespaces {
private:
    struct State {
        std::map<std::string,world::CollisionIdRange> ranges;
        std::set<std::string> occupied;
    };
public:
    class Lease {
    public:
        Lease()=default;
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other) noexcept
            :state_(std::move(other.state_)),
             key_(std::move(other.key_)),range_(other.range_) {
            other.range_={};
        }
        Lease& operator=(Lease&& other) noexcept {
            if(this!=&other) {
                reset();
                state_=std::move(other.state_);
                key_=std::move(other.key_);
                range_=other.range_;
                other.range_={};
            }
            return *this;
        }
        ~Lease(){reset();}
        bool valid()const noexcept {return state_!=nullptr && range_.valid();}
        explicit operator bool()const noexcept {return valid();}
        world::CollisionIdRange range()const noexcept {return range_;}
        const std::string& key()const noexcept {return key_;}
        void reset()noexcept {
            if(state_) {
                state_->occupied.erase(key_);
                state_.reset();
            }
            key_.clear();
            range_={};
        }
    private:
        friend class SceneCollisionNamespaces;
        Lease(std::shared_ptr<State> state,std::string key,
              world::CollisionIdRange range)
            :state_(std::move(state)),key_(std::move(key)),range_(range){}
        std::shared_ptr<State> state_;
        std::string key_;
        world::CollisionIdRange range_{};
    };

    // Multiple active instances of the same scene asset must use different
    // stable instance keys (e.g. "instances/zone2/caveA" not "scenes/cave").
    // One namespace slot holds at most 65,535 shapes in a chunk.
    bool define(std::string instanceKey,uint32_t maximumChunkCount) {
        if(sealed_ || !SceneAsset::validKey(instanceKey) ||
           !maximumChunkCount || maximumChunkCount>32768 ||
           definitions_.contains(instanceKey)) return false;
        if(total_+maximumChunkCount>32768) return false;
        definitions_.emplace(std::move(instanceKey),maximumChunkCount);
        total_+=maximumChunkCount;
        return true;
    }

    bool seal() {
        if(sealed_ || definitions_.empty())return false;
        auto fresh=std::make_shared<State>();
        uint32_t cursor=0;
        Hasher hash;
        hash.str("aiverse.scene.collider-namespace.v1");
        hash.u32(uint32_t(definitions_.size()));
        for(const auto& [key,capacity]:definitions_) {
            fresh->ranges.emplace(key,world::CollisionIdRange{cursor,capacity});
            hash.str(key);
            hash.u32(cursor);
            hash.u32(capacity);
            cursor+=capacity;
        }
        state_=std::move(fresh);
        fingerprint_=hash.value();
        sealed_=true;
        return true;
    }

    bool sealed()const noexcept{return sealed_;}
    size_t count()const noexcept{return definitions_.size();}
    uint32_t reservedChunks()const noexcept{return total_;}
    uint64_t fingerprint()const noexcept{return fingerprint_;}

    std::optional<world::CollisionIdRange>
    find(std::string_view instanceKey)const {
        if(!state_)return std::nullopt;
        const auto it=state_->ranges.find(std::string(instanceKey));
        if(it==state_->ranges.end())return std::nullopt;
        return it->second;
    }

    // Move-only lease prevents two active CollisionStreams using one range,
    // including when the range has zero resident chunks. A lease must outlive
    // the CollisionStream it guards (declare lease before stream, so stream
    // destructs first), and must remain held during async prepare/unload.
    std::optional<Lease> acquire(std::string_view instanceKey) {
        const auto range=find(instanceKey);
        if(!range || !state_)return std::nullopt;
        const std::string key(instanceKey);
        if(!state_->occupied.insert(key).second)return std::nullopt;
        return Lease{state_,key,*range};
    }

    size_t activeCount()const noexcept {
        return state_?state_->occupied.size():0;
    }

private:
    std::map<std::string,uint32_t> definitions_;
    std::shared_ptr<State> state_;
    uint32_t total_=0;
    uint64_t fingerprint_=0;
    bool sealed_=false;
};

} // namespace aiverse::scene
