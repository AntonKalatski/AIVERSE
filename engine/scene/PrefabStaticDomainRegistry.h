// Multi-domain immutable prefab configuration composition for AIVERSE.
// Multiple independent config types (weapon/AI/inventory/etc) are assigned
// to one SceneAsset prefab while each static value is shared globally.
// Stable domain-key ordering, preflight before any Entity mutation and
// opt-in deterministic v10 persistence registration.
#pragma once

#include "engine/scene/PrefabStaticSceneBinding.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::scene {

// Owner-thread registration and use. Not an ECS system and never stores
// mutable T configuration inside an Entity. The domain list is frozen at
// seal(); callbacks capture sealed state and immutable catalogs by value.
//
// One configuration domain per T: the ECS permits only one StaticRef<T>
// component per Entity. Different domains use different T types.
class PrefabStaticDomainRegistry {
public:
    using Spawn = SceneLoadCoordinator::SpawnPrefab;

    template<class T>
    bool addDomain(std::string domainKey, std::string persistenceId,
                   std::shared_ptr<const storage::PrefabStaticCatalog<T>> catalog,
                   MissingPrefabConfig missing=MissingPrefabConfig::Reject,
                   uint32_t schemaVersion=1) {
        if(sealed_ || !catalog || !SceneAsset::validKey(domainKey) ||
           !validPersistenceId(persistenceId) ||
           !schemaVersion || domains_.contains(domainKey) ||
           persistenceIds_.contains(persistenceId))
            return false;

        // typeId is a process-only uniqueness guard, not a saved/replicated ID.
        const uint32_t typeId=detail::componentTypeId<storage::StaticRef<T>>();
        if(typeIds_.contains(typeId)) return false;

        Domain domain;
        domain.canBind=[catalog](const World& world) {
            return catalog->canBindWorld(world);
        };
        domain.bind=[catalog](World& world) {
            return catalog->bindWorld(world);
        };
        domain.validate=[catalog](const World& world) {
            return catalog->validateWorld(world);
        };
        domain.registerSchemas=[persistenceId, schemaVersion](Simulation& sim) {
            storage::registerPrefabStaticPersistence<T>(
                sim,persistenceId,schemaVersion);
        };
        domain.preflight=[catalog,missing,domainKey](const World& world,
                   Entity entity,const SceneObjectAsset& object,
                   std::string& error) {
            if(!catalog->bound(world)) {
                error="static domain is not bound to ECS World: "+domainKey;
                return false;
            }
            const auto config=catalog->find(object.prefab);
            if(!config.valid()) {
                if(missing==MissingPrefabConfig::Skip) return true;
                error="required static domain "+domainKey+
                      " has no prefab: "+object.prefab;
                return false;
            }
            if(world.has<storage::StaticRef<T>>(entity)) {
                error="duplicate static component for domain: "+domainKey;
                return false;
            }
            return true;
        };
        domain.attach=[catalog,domainKey](World& world,Entity entity,
                   const SceneObjectAsset& object,std::string& error) {
            if(!catalog->find(object.prefab).valid())return true;
            if(!catalog->attach(world,entity,object.prefab)) {
                error="static config attachment failed for "+domainKey+
                      ": "+object.prefab;
                return false;
            }
            return true;
        };

        // Map insertion succeeds before auxiliary indexes are mutated.
        domains_.emplace(std::move(domainKey),std::move(domain));
        typeIds_.insert(typeId);
        persistenceIds_.insert(std::move(persistenceId));
        return true;
    }

    bool seal() {
        if(sealed_ || domains_.empty())return false;
        sealed_=true;
        return true;
    }
    bool sealed() const noexcept {return sealed_;}
    size_t domainCount() const noexcept {return domains_.size();}

    // Called during GameModule::setup, before Simulation seals its registry.
    // Each distinct T gets a ref, static-table fingerprint and prefab-map
    // fingerprint under one explicit stable ID. Do not register the same T
    // separately elsewhere, or native schema registration will conflict.
    void registerPersistence(Simulation& simulation) const {
        requireSealed();
        for(const auto& [name,domain]:domains_) {
            (void)name;
            domain.registerSchemas(simulation);
        }
    }

    // Preflight ALL content binding revisions before touching World state,
    // so a stale second catalog does not cause a partially bound World.
    // OOM can still throw partway through resource creation: bind during
    // setup at a structural barrier, not on the gameplay hot path.
    bool bindWorld(World& world) const {
        if(!sealed_)return false;
        for(const auto& [name,domain]:domains_) {
            (void)name;
            if(!domain.canBind(world))return false;
        }
        for(const auto& [name,domain]:domains_) {
            (void)name;
            if(!domain.bind(world))return false;
        }
        return true;
    }

    bool validateWorld(const World& world) const {
        if(!sealed_)return false;
        for(const auto& [name,domain]:domains_) {
            (void)name;
            if(!domain.validate(world))return false;
        }
        return true;
    }

    // Real SceneLoadCoordinator::SpawnPrefab. All mandatory/optional
    // domains preflight FIRST, then attach immutable 4-byte refs in sorted
    // domain-key order, and finally create mutable per-instance components.
    // If an instance initializer fails, SceneLoadCoordinator cancels and
    // destroys the incomplete scene-owned Entities atomically.
    Spawn callback(Spawn initializeInstance={}) const {
        requireSealed();
        const auto entries=domains_;
        return [entries,initializeInstance](World& world,Entity entity,
                   const SceneObjectAsset& object,std::string& error) {
            if(!world.alive(entity)) {
                error="multi-domain spawn rejected dead Entity";
                return false;
            }
            for(const auto& [name,domain]:entries) {
                (void)name;
                if(!domain.preflight(world,entity,object,error))return false;
            }
            for(const auto& [name,domain]:entries) {
                (void)name;
                if(!domain.attach(world,entity,object,error))return false;
            }
            if(initializeInstance &&
               !initializeInstance(world,entity,object,error)) {
                if(error.empty())error="multi-domain instance initialization failed";
                return false;
            }
            return true;
        };
    }

private:
    // PersistentEcsRegistry accepts up to 128 ASCII chars for the
    // FINAL schema ID, including the ".prefab_binding" suffix.
    static bool validPersistenceId(std::string_view id) {
        if(id.empty() || id.size()>112)return false;
        for(char c:id) {
            if((c>='a' && c<='z') || (c>='A' && c<='Z') ||
               (c>='0' && c<='9') || c=='.' || c=='_' ||
               c=='-' || c==':') continue;
            return false;
        }
        return true;
    }
    struct Domain {
        std::function<bool(const World&)> canBind,validate;
        std::function<bool(World&)> bind;
        std::function<void(Simulation&)> registerSchemas;
        std::function<bool(const World&,Entity,const SceneObjectAsset&,
                           std::string&)> preflight;
        Spawn attach;
    };

    void requireSealed() const {
        if(!sealed_)throw std::logic_error(
            "PrefabStaticDomainRegistry must be sealed before use");
    }

    std::map<std::string,Domain> domains_; // stable canonical order
    std::set<uint32_t> typeIds_;           // only for detecting duplicate T
    std::set<std::string> persistenceIds_;
    bool sealed_=false;
};

} // namespace aiverse::scene
