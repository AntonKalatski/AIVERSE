// Authored and resident visual-source dependency index for any host.
// The index contains no GPU objects, animation players, ECS state or genre
// decisions. The host supplies its own type collection and visual Json.
#pragma once
#include "RtsModelStreaming.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace aiverse::game::view_adapter {

enum class ModelSourceKind : uint8_t {
    Model=1, Texture=2, AnimationGraph=4, MaterialLibrary=8
};

class ModelSourceDependencyIndex final {
public:
    struct Entry {
        uint8_t kinds=0;
        std::set<size_t> types;
        bool has(ModelSourceKind kind) const noexcept {
            return (kinds & uint8_t(kind))!=0;
        }
    };
    struct Impact {
        std::set<size_t> types;
        std::set<std::string> models; // all authored models of affected types
        bool model=false, texture=false, graph=false, materialLibrary=false;
        bool empty() const noexcept {
            return !model && !texture && !graph && !materialLibrary;
        }
        bool requiresWholeView() const noexcept {
            return graph || materialLibrary;
        }
    };

    void clear() { sources_.clear();typeModels_.clear();libraryUsers_.clear(); }

    // Never loads models or starts a worker: includes absent and deferred
    // model/LOD/stage/attachment references and streaming alternatives.
    template<class Types>
    void rebuild(const Types& types,const Json& materialLibrary=Json::object()) {
        clear();
        for(size_t i=0;i<types.size();++i)addType(i,types[i].view);
        for(const size_t type:libraryUsers_) {
            add("materials.json",ModelSourceKind::MaterialLibrary,type);
            collectTextures(materialLibrary,type);
        }
    }

    // Imported external textures discovered in an already-loaded model.
    // Embedded images have no source key and must not be registered.
    void addResidentTexture(const std::string& modelSource,
                            const std::string& textureSource) {
        const auto it=sources_.find(modelSource);
        if(it==sources_.end() || !it->second.has(ModelSourceKind::Model))
            return;
        for(const size_t type:it->second.types)
            add(textureSource,ModelSourceKind::Texture,type);
    }

    // A cache can retain models that are absent from the currently authored
    // rules (e.g. a just-edited choice). Keep them on the watch list too.
    void addResidentModel(const std::string& source) {
        if(!source.empty())sources_[source].kinds|=uint8_t(ModelSourceKind::Model);
    }

    const std::map<std::string,Entry>& sources() const noexcept {
        return sources_;
    }
    const Entry* lookup(const std::string& source) const noexcept {
        const auto it=sources_.find(source);
        return it==sources_.end()?nullptr:&it->second;
    }
    Impact resolve(const std::set<std::string>& changed) const {
        Impact impact;
        for(const auto& source:changed) {
            const auto it=sources_.find(source);
            if(it==sources_.end())continue;
            const Entry& entry=it->second;
            impact.types.insert(entry.types.begin(),entry.types.end());
            impact.model |= entry.has(ModelSourceKind::Model);
            impact.texture |= entry.has(ModelSourceKind::Texture);
            impact.graph |= entry.has(ModelSourceKind::AnimationGraph);
            impact.materialLibrary |= entry.has(ModelSourceKind::MaterialLibrary);
            if(entry.has(ModelSourceKind::Model))impact.models.insert(source);
        }
        // Refresh every model choice of a type affected by an authored
        // external texture: GPU caches are shared between model variants.
        for(const size_t type:impact.types) {
            const auto it=typeModels_.find(type);
            if(it!=typeModels_.end())
                impact.models.insert(it->second.begin(),it->second.end());
        }
        return impact;
    }
private:
    static bool usesLibrary(const Json& value) {
        if(value.isString())return true;
        if(value.isObject()) {
            if(value["use"].isString() || value["base"].isString())
                return true;
            for(const auto& [name,child]:value.members()) {
                (void)name;
                if(usesLibrary(child))return true;
            }
        }
        if(value.isArray())
            for(const auto& child:value.items())
                if(usesLibrary(child))return true;
        return false;
    }
    void add(const std::string& source,ModelSourceKind kind,size_t type) {
        if(source.empty() || source.rfind("shape:",0)==0)return;
        auto& entry=sources_[source];
        entry.kinds|=uint8_t(kind);
        entry.types.insert(type);
        if(kind==ModelSourceKind::Model)typeModels_[type].insert(source);
    }
    void collectTextures(const Json& root,size_t type) {
        static const std::set<std::string> keys{
            "texture","teamMask","normalMap","emissiveMap",
            "metallicRoughnessMap","occlusionMap"
        };
        std::function<void(const Json&)> walk=[&](const Json& value) {
            if(value.isObject()) {
                for(const auto& [name,child]:value.members()) {
                    if(keys.contains(name) && child.isString())
                        add(child.str(),ModelSourceKind::Texture,type);
                    walk(child);
                }
            } else if(value.isArray()) {
                for(const auto& child:value.items())walk(child);
            }
        };
        walk(root);
    }
    void addType(size_t type,const Json& view) {
        discoverModelPaths(view,false,
            [](const std::string&){return false;},
            [&](const std::string& path,const std::string&){
                add(path,ModelSourceKind::Model,type);
            });
        const Json& streaming=view["streaming"];
        add(streaming.getStr("coarseModel"),ModelSourceKind::Model,type);
        add(streaming.getStr("detailModel"),ModelSourceKind::Model,type);
        if(view["animationGraph"].isString())
            add(view["animationGraph"].str(),ModelSourceKind::AnimationGraph,type);
        collectTextures(view,type);
        if(usesLibrary(view["materials"]) || usesLibrary(view["assign"]))
            libraryUsers_.insert(type);
    }

    std::map<std::string,Entry> sources_;
    std::map<size_t,std::set<std::string>> typeModels_;
    std::set<size_t> libraryUsers_;
};

} // namespace aiverse::game::view_adapter
