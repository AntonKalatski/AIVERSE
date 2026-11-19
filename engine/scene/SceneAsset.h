// Typed, deterministic, genre-independent scene manifest (format v1).
// Parsing happens off the simulation thread; ECS entity instantiation does not.
#pragma once
#include "engine/data/Json.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::scene {

struct SceneObjectAsset {
    uint64_t id = 0;        // Scene-local stable ID, independent of Entity allocator.
    uint64_t parent = 0;    // Zero means root; parent must be in the same SceneAsset.
    std::string prefab;     // Passed to host's genre-independent spawner.
    Json properties = Json::object();
};

struct SceneAsset {
    static constexpr size_t kMaxObjects = 10000;
    static constexpr size_t kMaxDependencies = 512;
    static constexpr size_t kMaxKeyLength = 512;
    static constexpr size_t kMaxManifestBytes = 4 * 1024 * 1024;
    uint32_t version = 1;
    std::string id;
    std::vector<std::string> dependencies;
    std::vector<SceneObjectAsset> objects;

    // Reject unsafe and ambiguous paths. Always use logical asset keys
    // relative to the project content root (no OS-dependent normalization).
    static bool validKey(const std::string& key) {
        if(key.empty() || key.size() > kMaxKeyLength ||
           key.front() == '/' || key.back() == '/') return false;
        size_t begin = 0;
        for(size_t i = 0; i <= key.size(); ++i) {
            if(i < key.size()) {
                const char c = key[i];
                if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                   c == '.' || c == '/') {
                    if(c != '/') continue;
                } else return false;
            }
            const std::string part = key.substr(begin, i - begin);
            if(part.empty() || part == "." || part == "..") return false;
            begin = i + 1;
        }
        return true;
    }

    // Strict schema validation. Sorting normalizes both dependency and object
    // order for deterministic canonical instantiation independent of JSON order.
    static bool decode(const Json& data, SceneAsset& out, std::string* error = nullptr) {
        auto fail = [&](const char* reason) {
            if(error) *error = reason;
            return false;
        };
        if(!data.isObject() || !data["version"].isNumber() ||
           data["version"].integer() != 1 || !data["id"].isString() ||
           !validKey(data["id"].str()) || !data["dependencies"].isArray() ||
           !data["objects"].isArray())
            return fail("invalid scene v1 header / id / arrays");
        if(data["dependencies"].size() > kMaxDependencies ||
           data["objects"].size() > kMaxObjects)
            return fail("scene count exceeds limit");

        SceneAsset candidate;
        candidate.id = data["id"].str();
        std::set<std::string> uniqueDependencies;
        for(const Json& dep : data["dependencies"].items()) {
            if(!dep.isString() || !validKey(dep.str()) ||
               !uniqueDependencies.insert(dep.str()).second)
                return fail("invalid or duplicated scene dependency");
            candidate.dependencies.push_back(dep.str());
        }
        std::sort(candidate.dependencies.begin(), candidate.dependencies.end());

        std::set<uint64_t> uniqueObjects;
        for(const Json& entry : data["objects"].items()) {
            if(!entry.isObject() || !entry["id"].isNumber() ||
               !entry["prefab"].isString() ||
               !validKey(entry["prefab"].str()))
                return fail("invalid scene object");
            const int64_t id = entry["id"].integer(-1);
            if(id <= 0 || !uniqueObjects.insert(uint64_t(id)).second)
                return fail("invalid or duplicate scene object ID");
            int64_t parent = 0;
            if(entry.has("parent")) {
                if(!entry["parent"].isNumber()) return fail("invalid parent type");
                parent = entry["parent"].integer(-1);
            }
            if(parent < 0 || parent == id)
                return fail("invalid scene parent ID");
            Json properties = Json::object();
            if(entry.has("properties")) {
                if(!entry["properties"].isObject()) return fail("invalid prefab properties");
                properties = entry["properties"];
            }
            candidate.objects.push_back(SceneObjectAsset{
                uint64_t(id), uint64_t(parent), entry["prefab"].str(),
                std::move(properties)});
        }
        std::sort(candidate.objects.begin(), candidate.objects.end(),
                  [](const SceneObjectAsset& a, const SceneObjectAsset& b) {
                      return a.id < b.id;
                  });

        // Parent graph is validated independently of authored order.
        std::map<uint64_t, uint64_t> parents;
        for(const auto& obj : candidate.objects)
            parents.emplace(obj.id, obj.parent);
        for(const auto& [id, parent] : parents) {
            (void)id;
            if(parent != 0 && !parents.contains(parent))
                return fail("scene parent references unknown local object");
        }
        // Visit states: 1=on path, 2=complete. Iterative to avoid recursion.
        std::map<uint64_t, uint8_t> marks;
        for(const auto& [id, parent] : parents) {
            (void)parent;
            if(marks[id] == 2) continue;
            uint64_t cur = id;
            std::vector<uint64_t> chain;
            while(cur && marks[cur] != 2) {
                if(marks[cur] == 1) return fail("cycle in scene parent graph");
                marks[cur] = 1;
                chain.push_back(cur);
                cur = parents.at(cur);
            }
            for(uint64_t node : chain) marks[node] = 2;
        }
        out = std::move(candidate);
        if(error) error->clear();
        return true;
    }
};

} // namespace aiverse::scene
