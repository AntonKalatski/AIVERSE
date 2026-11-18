// Versioned, deterministic persistence of native ECS state; register during setup.
#pragma once
#include "../core/Archive.h"
#include "../core/Hash.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace aiverse {
class PersistentEcsRegistry {
public:
    enum class Kind : uint8_t { Component=1, Resource=2 };
    static constexpr uint32_t kMaxPayload = 32u*1024u*1024u;
    static constexpr uint32_t kMaxEntities = 1000000;
    template<class T> void registerComponent(World& world,std::string id,uint32_t version=1) {
        static_assert(std::is_copy_constructible_v<T> && std::is_default_constructible_v<T>);
        insert({std::move(id),Kind::Component,version,detail::componentTypeId<T>(),
            [](Archive& a,const World& w) {
                const size_t count=w.countExisting<T>();
                if(count>kMaxEntities){a.fail();return;}
                uint32_t n=uint32_t(count);io(a,n);
                w.eachExistingConst<T>([&](Entity entity,const T& source) {
                    Entity key=entity;T item=source;
                    io(a,key);io(a,item);
                });
            },
            [](Archive& a,World& w) {
                uint32_t n=0;io(a,n);
                if(!a.ok() || n>kMaxEntities){a.fail();return;}
                auto& pool=w.pool<T>();pool.clear();
                for(uint32_t i=0;i<n && a.ok();++i) {
                    Entity entity;T item{};
                    io(a,entity);io(a,item);
                    if(!a.ok() || !w.alive(entity) || pool.has(entity.index)){a.fail();return;}
                    pool.emplace(entity,std::move(item));
                }
            }
        });
        world.pool<T>(); // even an empty registered pool must be counted
    }
    template<class T> void registerResource(std::string id,uint32_t version=1) {
        static_assert(std::is_copy_constructible_v<T> && std::is_default_constructible_v<T>);
        insert({std::move(id),Kind::Resource,version,detail::componentTypeId<T>(),
            [](Archive& a,const World& w) {
                const T* source=w.resourceConst<T>();
                bool present=source!=nullptr;io(a,present);
                if(source){T item=*source;io(a,item);}
            },
            [](Archive& a,World& w) {
                bool present=false;io(a,present);
                if(!a.ok())return;
                if(!present){w.eraseResource<T>();return;}
                T item{};io(a,item);
                if(a.ok())w.setResource<T>(std::move(item));
            }
        });
    }
    size_t componentCount()const {
        return size_t(std::count_if(entries_.begin(),entries_.end(),
            [](const Entry& e){return e.kind==Kind::Component;}));
    }
    bool empty()const{return entries_.empty();}
    void seal(){sealed_=true;}
    bool serialize(Archive& a,World& world,std::string* err=nullptr)const {
        auto fail=[&](const std::string& reason) {
            a.fail();if(err)*err="persistent ECS: "+reason;return false;
        };
        uint32_t count=uint32_t(entries_.size());io(a,count);
        if(!a.ok() || count!=entries_.size())return fail("schema count differs");
        World staged;
        if(!a.writing())staged=world; // registered part commits only after full validation
        for(const Entry& e:entries_) {
            std::string id=e.id;
            uint8_t kind=uint8_t(e.kind);
            uint32_t version=e.version;
            io(a,id);io(a,kind);io(a,version);
            if(!a.ok() || id!=e.id || kind!=uint8_t(e.kind) || version!=e.version)
                return fail("schema ID, kind or version differs: "+e.id);
            std::string chunk;
            if(a.writing()){
                Archive sub(&chunk);sub.formatVersion=version;
                e.write(sub,world);
                if(!sub.ok() || chunk.size()>kMaxPayload)return fail("invalid/oversized payload: "+e.id);
            }
            uint32_t len=uint32_t(chunk.size());io(a,len);
            if(!a.ok() || len>kMaxPayload || (!a.writing() && len>a.remaining()))
                return fail("invalid payload size: "+e.id);
            if(!a.writing())chunk.resize(len);
            a.block(chunk.data(),len);
            if(!a.ok())return fail("truncated payload: "+e.id);
            if(!a.writing()){
                Archive sub(chunk);sub.formatVersion=version;
                e.read(sub,staged);
                if(!sub.ok() || sub.remaining()!=0)return fail("invalid payload: "+e.id);
            }
        }
        if(!a.writing())world=std::move(staged);
        return a.ok();
    }
    void hash(const World& world,Hasher& h)const {
        if(entries_.empty())return; // legacy checksums remain unchanged
        h.str("aiverse.native.ecs.v1");
        h.u32(uint32_t(entries_.size()));
        for(const Entry& e:entries_){
            h.str(e.id);h.u32(uint32_t(e.kind));h.u32(e.version);
            Archive a(h);a.formatVersion=e.version;
            e.write(a,world);
            if(!a.ok())throw std::runtime_error("persistent ECS hash failed: "+e.id);
        }
    }
private:
    struct Entry {
        std::string id;Kind kind;uint32_t version,typeId;
        void (*write)(Archive&,const World&);
        void (*read)(Archive&,World&);
    };
    void insert(Entry e){
        if(sealed_)throw std::logic_error("persistent ECS registration only during setup");
        if(e.id.empty() || e.id.size()>128 || !e.version)
            throw std::invalid_argument("invalid persistent ECS ID/version");
        for(char c:e.id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
            (c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'||c==':'))
            throw std::invalid_argument("persistent ECS ID must be stable ASCII");
        for(const Entry& old:entries_){
            if(old.id==e.id)throw std::invalid_argument("duplicate persistent ECS ID: "+e.id);
            if(old.typeId==e.typeId && old.kind==e.kind)
                throw std::invalid_argument("persistent ECS C++ type registered more than once");
        }
        auto at=std::lower_bound(entries_.begin(),entries_.end(),e.id,
            [](const Entry& left,const std::string& right){return left.id<right;});
        entries_.insert(at,std::move(e));
    }
    std::vector<Entry> entries_;
    bool sealed_=false;
};
} // namespace aiverse
