// Read-only runtime inspector bridge for immutable prefab configurations.
// Native modules may call this from inspectComponents(World&,Entity,Json&,Json&).
// It produces Editor WorldInspect "components"/"componentSchemas" JSON
// without linking ImGui/editor code into the deterministic engine.
#pragma once

#include "engine/sim/PrefabStaticCatalog.h"
#include "engine/data/Json.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace aiverse::scene {

template<class T> class PrefabStaticInspector {
public:
    using Catalog = storage::PrefabStaticCatalog<T>;
    using Describe = std::function<Json(const T&)>;

    PrefabStaticInspector(std::shared_ptr<const Catalog> catalog,
                          std::string componentId,
                          std::string title,Describe describe)
        : catalog_(std::move(catalog)),
          componentId_(std::move(componentId)),
          title_(std::move(title)),
          describe_(std::move(describe)) {}

    // Returns false only for a missing component/unbound catalog.
    // Does NOT mutate World; malformed custom Describe output is stored as
    // read-only text to avoid introducing writable editor state.
    bool append(const World& world,Entity entity,
                Json& components,Json& schemas) const {
        if(!catalog_ || !catalog_->bound(world) || !world.alive(entity) ||
           !components.isObject() || !schemas.isObject() ||
           componentId_.empty() || componentId_.find('.')!=std::string::npos)
            return false;
        const auto* ref=world.tryGetConst<storage::StaticRef<T>>(entity);
        if(!ref)return false;
        const T* value=catalog_->read(world,entity);
        if(!value)return false;

        Json fields=Json::object();
        fields.set("slot",Json::number(int64_t(ref->slot)));
        fields.set("uniqueConfigs",Json::number(
            int64_t(catalog_->uniqueConfigCount())));
        // uint64 fingerprints must be displayed exactly. Json::number uses
        // Fixed numeric limits; unsigned content revisions are hex strings.
        auto hex=[](uint64_t x) {
            const char* symbols="0123456789abcdef";
            std::string out(16,'0');
            for(int i=15;i>=0;--i) {out[size_t(i)]=symbols[x&15];x>>=4;}
            return out;
        };
        fields.set("catalogRevision",Json::string(hex(catalog_->fingerprint())));
        fields.set("tableRevision",Json::string(
            hex(catalog_->table().fingerprint())));
        Json detail=Json::object();
        if(describe_)detail=describe_(*value);
        if(!detail.isObject()) {
            Json wrapped=Json::object();
            wrapped.set("description",Json::string(detail.dump(-1)));
            detail=std::move(wrapped);
        }
        fields.set("config",std::move(detail));
        components.set(componentId_,std::move(fields));

        Json schema=Json::object();
        schema.set("title",Json::string(
            title_.empty()?componentId_:title_));
        Json fieldSchema=Json::object();
        for(const auto& name:{"slot","uniqueConfigs","catalogRevision",
                              "tableRevision","config"}) {
            Json meta=Json::object();
            meta.set("editable",Json::fromBool(false));
            meta.set("title",Json::string(name));
            meta.set("kind",Json::string(
                std::string(name)=="slot" ||
                std::string(name)=="uniqueConfigs"?"number":"string"));
            fieldSchema.set(name,std::move(meta));
        }
        schema.set("fields",std::move(fieldSchema));
        schemas.set(componentId_,std::move(schema));
        return true;
    }

private:
    std::shared_ptr<const Catalog> catalog_;
    std::string componentId_,title_;
    Describe describe_;
};

// Convenience for native games with several static-config domains.
// Register typed inspectors once, then call registry.append(...) from the
// native module's existing inspectComponents hook. The editor never has
// to know or reflect arbitrary user C++ types.
class PrefabStaticInspectorRegistry {
public:
    template<class T>
    bool add(std::shared_ptr<const storage::PrefabStaticCatalog<T>> catalog,
             std::string componentId,std::string title,
             typename PrefabStaticInspector<T>::Describe describe) {
        if(sealed_ || !catalog || componentId.empty() ||
           componentId.size()>128 ||
           componentId.find('.')!=std::string::npos ||
           entries_.contains(componentId))
            return false;
        auto inspector=std::make_shared<const PrefabStaticInspector<T>>(
            std::move(catalog),componentId,std::move(title),
            std::move(describe));
        entries_.emplace(std::move(componentId),
            [inspector](const World& world,Entity entity,
                        Json& components,Json& schemas) {
                inspector->append(world,entity,components,schemas);
            });
        return true;
    }
    bool seal() {
        if(sealed_)return false;
        sealed_=true;
        return true;
    }
    size_t size() const noexcept {return entries_.size();}

    void append(const World& world,Entity entity,
                Json& components,Json& schemas) const {
        // NativeModule::inspectComponents receives mutable World&;
        // we accept a const World&, guaranteeing no ECS writes.
        for(const auto& [id,read]:entries_) {
            (void)id;
            read(world,entity,components,schemas);
        }
    }
private:
    using Reader=std::function<void(const World&,Entity,Json&,Json&)>;
    std::map<std::string,Reader> entries_; // stable canonical UI order
    bool sealed_=false;
};

} // namespace aiverse::scene
