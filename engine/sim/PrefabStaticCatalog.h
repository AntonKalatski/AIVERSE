// Genre-independent immutable prefab configuration catalog.
// Builds one StaticTable<T> per config domain, reusable by many scenes and
// independent ECS Worlds without copying T into each Entity.
//
// This header intentionally does not include SceneAsset/SceneLoadCoordinator:
// the scene feature is developed in a separate branch. Its SpawnPrefab
// callback can call catalog.attach(world, entity, object.prefab).
#pragma once

#include "SharedStaticComponents.h"
#include "engine/sim/SharedStaticPersistence.h"
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace aiverse::storage {

template<class T>
struct PrefabCatalogBinding {
    uint64_t fingerprint = 0;
};
template<class T>
void io(Archive& a, PrefabCatalogBinding<T>& binding) {
    a(binding.fingerprint);
}

template<class T> class PrefabStaticCatalogBuilder;

template<class T>
class PrefabStaticCatalog {
public:
    // All read APIs return immutable content. Catalog + table may be held
    // as shared_ptr<const ...> by multiple worlds / simulations.
    const StaticTable<T>& table() const noexcept {return *table_;}
    const std::shared_ptr<const StaticTable<T>>& sharedTable() const noexcept {
        return table_;
    }
    size_t prefabCount() const noexcept {return prefabRefs_.size();}
    size_t uniqueConfigCount() const noexcept {return table_->uniqueCount();}
    uint64_t fingerprint() const noexcept {return fingerprint_;}

    StaticRef<T> find(std::string_view prefab) const {
        // Prefab lookups are spawn-time, not per-tick hot loops.
        const auto it=prefabRefs_.find(std::string(prefab));
        return it==prefabRefs_.end()?StaticRef<T>{}:it->second;
    }

    // Bind to a World once. The catalog identity includes every
    // prefab->slot mapping as well as the StaticTable content fingerprint,
    // so a save cannot silently adopt a changed mapping with identical
    // underlying unique configuration values.
    bool bindWorld(World& world) const {
        const auto* catalog=world.resourceConst<PrefabCatalogBinding<T>>();
        const auto* tableBinding=world.resourceConst<StaticTableBinding<T>>();
        if((catalog && catalog->fingerprint!=fingerprint_) ||
           (tableBinding && tableBinding->fingerprint!=table_->fingerprint()))
            return false;
        if(!bindStaticTable(world,*table_))return false;
        if(!catalog)
            world.setResource<PrefabCatalogBinding<T>>(
                PrefabCatalogBinding<T>{fingerprint_});
        return true;
    }

    bool bound(const World& world) const noexcept {
        const auto* catalog=world.resourceConst<PrefabCatalogBinding<T>>();
        const auto* tableBinding=world.resourceConst<StaticTableBinding<T>>();
        return catalog && catalog->fingerprint==fingerprint_ &&
               tableBinding && tableBinding->fingerprint==table_->fingerprint();
    }

    // Called once for each newly spawned Entity. Fails on unknown prefab,
    // mismatched table identity, stale Entity or an existing StaticRef<T>.
    bool attach(World& world, Entity entity, std::string_view prefab) const {
        const auto ref=find(prefab);
        return ref.valid() && bound(world) &&
               setStatic(world,entity,ref,*table_);
    }

    const T* read(const World& world, Entity entity) const noexcept {
        return bound(world) ? readStatic(world,entity,*table_) : nullptr;
    }

    bool validateWorld(const World& world) const {
        return bound(world) && validateStatic(world,*table_);
    }

private:
    PrefabStaticCatalog(std::shared_ptr<const StaticTable<T>> table,
                        std::map<std::string,StaticRef<T>> refs)
        : table_(std::move(table)),prefabRefs_(std::move(refs)) {
        Hasher h;
        h.str("aiverse.ecs.prefab-static.v1");
        h.u64(table_->fingerprint());
        h.u32(uint32_t(prefabRefs_.size()));
        for(const auto& [key,ref]:prefabRefs_) {
            h.str(key);
            h.u32(ref.slot);
        }
        fingerprint_=h.value();
    }

    std::shared_ptr<const StaticTable<T>> table_;
    std::map<std::string,StaticRef<T>> prefabRefs_;
    uint64_t fingerprint_=0;
    friend class PrefabStaticCatalogBuilder<T>;
};

template<class T>
class PrefabStaticCatalogBuilder {
public:
    // Build keys are deterministic and sorted lexicographically regardless
    // of import/file traversal order. Identical config values are interned.
    bool add(std::string prefab,T configuration) {
        if(sealed_)throw std::logic_error("prefab static catalog is already sealed");
        if(!validKey(prefab))return false;
        return definitions_.emplace(std::move(prefab),
                                    std::move(configuration)).second;
    }

    std::shared_ptr<const PrefabStaticCatalog<T>> seal() {
        if(sealed_)throw std::logic_error("prefab static catalog is already sealed");
        StaticTableBuilder<T> builder;
        std::map<std::string,StaticRef<T>> refs;
        for(const auto& [prefab,config]:definitions_)
            refs.emplace(prefab,builder.define(config));
        const auto table=builder.seal();
        auto catalog=std::shared_ptr<const PrefabStaticCatalog<T>>(
            new PrefabStaticCatalog<T>(table,std::move(refs)));
        sealed_=true;
        definitions_.clear();
        return catalog;
    }

private:
    static bool validKey(std::string_view key) {
        if(key.empty() || key.size()>512 || key.front()=='/' ||
           key.back()=='/')return false;
        size_t begin=0;
        for(size_t i=0;i<=key.size();++i) {
            if(i<key.size()) {
                const char c=key[i];
                if((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
                   (c>='0'&&c<='9') || c=='_' || c=='-' ||
                   c=='.' || c=='/') {
                    if(c!='/')continue;
                } else return false;
            }
            const auto segment=key.substr(begin,i-begin);
            if(segment.empty() || segment=="." || segment=="..")return false;
            begin=i+1;
        }
        return true;
    }
    bool sealed_=false;
    std::map<std::string,T> definitions_;
};

// Register once per T during GameModule::setup instead of separately
// registering StaticRef<T> and the catalog binding under other IDs.
// The project must still retain/reconstruct the exact immutable catalog
// from content before loading state or simulating a tick.
template<class T>
void registerPrefabStaticPersistence(Simulation& simulation,
                                     std::string stableTypeId,
                                     uint32_t version=1) {
    registerStaticPersistence<T>(simulation,stableTypeId,version);
    simulation.registerPersistentResource<PrefabCatalogBinding<T>>(
        stableTypeId+".prefab_binding",version);
}

} // namespace aiverse::storage
