// Read-only runtime inspector bridge for immutable prefab configurations.
// Native modules may call this from inspectComponents(World&,Entity,Json&,Json&).
// It produces Editor WorldInspect "components"/"componentSchemas" JSON
// without linking ImGui/editor code into the deterministic engine.
#pragma once

#include "engine/sim/PrefabStaticCatalog.h"
#include "engine/data/Json.h"
#include <cstdint>
#include <functional>
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

} // namespace aiverse::scene
