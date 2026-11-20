// AIVERSE editor/content pipeline: immutable prefab config definitions.
// Strict, versioned JSON authoring -> typed native PrefabStaticCatalog<T>.
// No editor/ImGui or ECS World dependency: tooling may validate, generate,
// preview and seal the same content without launching gameplay.
#pragma once

#include "engine/sim/PrefabStaticCatalog.h"
#include "engine/scene/SceneAsset.h"
#include <exception>
#include "engine/data/Json.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace aiverse::scene {

template<class T>
struct PrefabStaticAuthoring {
    // Native game owns this decoder. It MUST validate every field and
    // reject invalid numbers instead of coercing with Json::getInt/getFx.
    using Decode = std::function<bool(const Json&, T&, std::string&)>;

    // Schema v1:
    // { "version": 1, "domain": "weapon", "prefabs": [
    //   { "id": "prefabs/soldier", "config": { ... } }, ...
    // ]}
    // StaticTable's stable ID ordering is sorted by prefab key; authoring
    // array order is intentionally irrelevant.
    static std::shared_ptr<const storage::PrefabStaticCatalog<T>>
    parse(const Json& document,const std::string& expectedDomain,
          const Decode& decode,std::string& error) {
        error.clear();
        auto fail=[&](const std::string& reason)
           -> std::shared_ptr<const storage::PrefabStaticCatalog<T>> {
            error=reason;
            return {};
        };
        if(!decode)return fail("missing typed prefab config decoder");
        if(!SceneAsset::validKey(expectedDomain))
            return fail("invalid expected prefab config domain");
        if(!document.isObject() || !document["version"].isNumber() ||
           document["version"].numberLiteral()!="1" ||
           !document["domain"].isString() ||
           document["domain"].str()!=expectedDomain ||
           !document["prefabs"].isArray())
            return fail("invalid prefab static v1 document");
        if(document["prefabs"].size()>100000)
            return fail("too many prefab static definitions");

        storage::PrefabStaticCatalogBuilder<T> builder;
        std::set<std::string> seen;
        for(const Json& object:document["prefabs"].items()) {
            if(!object.isObject() ||
               !object["id"].isString() ||
               !object["config"].isObject())
                return fail("prefab static entry expects id/config");
            const std::string key=object["id"].str();
            if(!SceneAsset::validKey(key) ||
               !seen.insert(key).second)
                return fail("invalid or duplicate prefab static key: "+key);
            T value{};
            std::string issue;
            try {
                if(!decode(object["config"],value,issue))
                    return fail("invalid "+key+": "+issue);
                if(!builder.add(key,std::move(value)))
                    return fail("duplicate prefab static key: "+key);
            } catch(const std::exception& e) {
                return fail("cannot parse "+key+": "+e.what());
            }
        }
        if(seen.empty())return fail("prefab static document is empty");
        try {
            return builder.seal();
        } catch(const std::exception& e) {
            return fail(std::string("prefab catalog seal failed: ")+e.what());
        }
    }

    static std::shared_ptr<const storage::PrefabStaticCatalog<T>>
    parseText(const std::string& bytes,const std::string& expectedDomain,
              const Decode& decoder,std::string& error) {
        if(bytes.size()>4*1024*1024) {
            error="prefab static authored JSON exceeds 4 MiB";
            return {};
        }
        try {
            return parse(Json::parse(bytes),expectedDomain,decoder,error);
        } catch(const std::exception& e) {
            error=e.what();
            return {};
        }
    }
};

} // namespace aiverse::scene
