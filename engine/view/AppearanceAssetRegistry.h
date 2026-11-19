// Genre-neutral, owner-thread appearance publication with pinned immutable
// snapshots. Rendering and AnimationGraph clocks remain in a single View.
#pragma once
#include "AppearanceAuthoringService.h"
#include "AppearanceChoiceAuthoring.h"
#include "ModelPublicationEpoch.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aiverse::view {

class AppearanceAssetRegistry final {
public:
    using Warning=AppearanceAuthoringService::Warning;
    using TextureLoader=AppearanceAuthoringService::TextureLoader;

    struct Request {
        // Model and texture indices remain host-owned. The registry owns only
        // material overrides, graph programs and snapshot metadata.
        const render::Model* model=nullptr;
        Json materialLibrary=Json::object();
        std::vector<Json> materialOverrides; // by imported model material index
        // Optional shared rule keys: materials("*"/named), assign(node glob), variant.
        // Exact same CPU policy is used by the RTS legacy adapter and native FPS.
        Json appearance;
        std::vector<int> nodeMaterial;
        Json animationGraph; // null: no graph, string: external asset, object: inline
        render::FileReader graphReader;
        render::AnimationOperations operations;
        render::AnimationTraits traits;
        render::AnimationGraphs namedGraphs;
        render::AnimationParameters parameters;
        std::vector<std::string> sources; // include material-library and model paths
        render::GeometryPolicy geometry{};
        render::float4 tint{1,1,1,1};
        render::float3 team{1,1,1};
        int variant=-1;
        uint32_t flags=0;
        uint64_t sourceId=0; // mandatory for an animated source
        uint64_t graphRevision=0; // increment for changed in-memory graph inputs
        uint64_t modelRevision=0; // host increments when the Model is re-imported at a reused address
    };

    struct Asset final {
        // A shared Asset pins the previous material+graph snapshot across a
        // transaction. It does NOT extend the host Model/GPU Device lifetime.
        const render::Model* model=nullptr;
        std::vector<render::Material> materials;
        std::vector<int> nodeMaterial;
        std::shared_ptr<const render::AnimationGraph> graph;
        render::AnimationParameters parameters;
        render::GeometryPolicy geometry{};
        render::float4 tint{1,1,1,1};
        render::float3 team{1,1,1};
        int variant=-1;
        uint32_t flags=0;
        uint64_t sourceId=0;
        uint64_t generation=0; // changes only when graph/model/source identity changes
        uint64_t version=0;    // changes on every successful publication
        uint64_t graphRevision=0;
        uint64_t modelRevision=0;
        std::string modelSource;
        std::string graphDefinition;
        std::string graphSource;
        std::vector<std::string> sources;
        // Globally unique publication identity for the borrowed Model
        // appearance, safe even when a Model address or registry is reused.
        uint64_t modelPublicationGeneration=0;

        bool packet(const render::mat4& world, Object& out) const {
            AppearanceSpec spec;
            spec.model=model;
            spec.materials=&materials;
            spec.nodeMaterial=&nodeMaterial;
            spec.geometry=geometry;
            spec.tint=tint;
            spec.team=team;
            spec.variant=variant;
            spec.flags=flags;
            spec.graph=graph;
            spec.parameters=parameters;
            spec.sourceId=sourceId;
            spec.generation=generation;
            if(!buildAppearancePacket(spec,world,out))return false;
            out.animationModelGeneration=modelPublicationGeneration;
            return true;
        }
    };

    using Snapshot=std::shared_ptr<const Asset>;

    Snapshot find(const std::string& key) const {
        const auto it=assets_.find(key);
        return it==assets_.end()?Snapshot{}:it->second;
    }
    bool dirty(const std::string& key) const noexcept {
        return dirty_.contains(key);
    }
    size_t size() const noexcept{return assets_.size();}

    // Marks dependents dirty without deleting their last working appearance.
    // The host re-reads sources and calls publish at a safe frame boundary.
    // A changed graph file also forces a new graph generation even if the
    // authored reference string is identical.
    size_t invalidateSources(const std::vector<std::string>& sources) {
        std::unordered_set<std::string> changed(sources.begin(),sources.end());
        size_t count=0;
        for(const auto& [key,asset]:assets_) {
            bool affected=false;
            for(const auto& source:asset->sources)
                if(changed.contains(source)){affected=true;break;}
            if(!affected)continue;
            if(dirty_.insert(key).second)++count;
            if((!asset->graphSource.empty() &&
                changed.contains(asset->graphSource)) ||
               (!asset->modelSource.empty() &&
                changed.contains(asset->modelSource)))
                dirtyGraph_.insert(key);
        }
        return count;
    }

    // Strict publish transaction: program validation, material preparation
    // and graph compilation must ALL succeed before the live map is swapped.
    // On failure the old snapshot/dirty flag/graph clock survive unchanged.
    // TextureLoader is a host resolver: if it allocates GPU textures, its
    // rollback/retirement responsibility remains with the host.
    bool publish(const std::string& key,const Request& request,
                 const TextureLoader& textureLoader={},
                 const Warning& warn={},std::string* error=nullptr) {
        if(error)error->clear();
        try {
            if(key.empty() || !request.model)
                throw std::invalid_argument("appearance needs key and model");
            if(request.materialOverrides.size()>request.model->materials.size())
                throw std::invalid_argument("appearance material override index out of range");
            if(!request.animationGraph.isNull() && !request.sourceId)
                throw std::invalid_argument("animated appearance requires stable sourceId");
            if(versionSerial_==std::numeric_limits<uint64_t>::max())
                throw std::overflow_error("appearance version overflow");

            auto next=std::make_shared<Asset>();
            next->model=request.model;
            next->materials=request.model->materials;
            next->nodeMaterial=request.nodeMaterial;
            next->geometry=request.geometry;
            next->tint=request.tint;
            next->team=request.team;
            next->variant=request.variant;
            next->flags=request.flags;
            next->sourceId=request.sourceId;
            next->parameters=request.parameters;
            next->graphRevision=request.graphRevision;
            next->modelRevision=request.modelRevision;
            next->modelSource=request.model->path;
            next->graphDefinition=request.animationGraph.isNull()?
                std::string{}:request.animationGraph.dump(0);
            if(request.animationGraph.isString())
                next->graphSource=request.animationGraph.str();
            next->sources=request.sources;
            if(!next->modelSource.empty() &&
               std::find(next->sources.begin(),next->sources.end(),
                         next->modelSource)==next->sources.end())
                next->sources.push_back(next->modelSource);
            if(!next->graphSource.empty() &&
               std::find(next->sources.begin(),next->sources.end(),
                         next->graphSource)==next->sources.end())
                next->sources.push_back(next->graphSource);

            const auto applyStrict=[&](render::Material& material,const Json& ref) {
                if(ref.isNull())return;
                const Json resolved=AppearanceAuthoringService::resolveMaterial(
                    request.materialLibrary,ref,warn);
                // Precompile a shaderGraph before the best-effort material API
                // can discard a broken graph; this is a CPU transaction only.
                if(resolved.has("shaderGraph"))
                    (void)render::graph::compile(resolved["shaderGraph"],"material");
                AppearanceAuthoringService::applyMaterial(
                    material,request.materialLibrary,ref,textureLoader,warn);
            };
            if(request.appearance.isObject()) {
                const auto choice=AppearanceChoiceAuthoring::resolve(
                    *request.model,request.appearance,warn);
                for(size_t i=0;i<choice.materialLayers.size();++i)
                    for(const auto& layer:choice.materialLayers[i])
                        applyStrict(next->materials[i],layer);
                for(const auto& extra:choice.extraMaterials) {
                    render::Material material;
                    material.name=extra.name;
                    applyStrict(material,extra.reference);
                    next->materials.push_back(std::move(material));
                }
                if(request.nodeMaterial.empty())
                    next->nodeMaterial=choice.nodeMaterial;
                if(request.variant<0)
                    next->variant=choice.variant;
            }
            for(size_t i=0;i<request.materialOverrides.size();++i)
                applyStrict(next->materials[i],request.materialOverrides[i]);

            const auto previous=find(key);
            if(!request.animationGraph.isNull()) {
                const bool reuse=previous && previous->graph &&
                    previous->model==request.model &&
                    previous->sourceId==request.sourceId &&
                    previous->graphDefinition==next->graphDefinition &&
                    previous->graphRevision==request.graphRevision &&
                    previous->modelRevision==request.modelRevision &&
                    !dirtyGraph_.contains(key);
                if(reuse) {
                    next->graph=previous->graph;
                    next->generation=previous->generation;
                } else {
                    next->graph=AppearanceAuthoringService::compileGraph(
                        *request.model,request.animationGraph,request.graphReader,
                        request.operations,request.traits,request.namedGraphs);
                    if(!next->graph)
                        throw std::runtime_error("animation graph compilation returned null");
                    if(animationSerial_==std::numeric_limits<uint64_t>::max())
                        throw std::overflow_error("animation generation overflow");
                    next->generation=animationSerial_+1;
                }
            }

            next->version=versionSerial_+1;
            next->modelPublicationGeneration=ModelPublicationEpoch::next();
            // Commit only after every potentially throwing preparation step.
            assets_.insert_or_assign(key,std::move(next));
            ++versionSerial_;
            if(assets_.at(key)->graph)
                animationSerial_=std::max(animationSerial_,assets_.at(key)->generation);
            dirty_.erase(key);
            dirtyGraph_.erase(key);
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    // Adopt an ALREADY compiled/validated host appearance without reloading
    // textures or recompiling its AnimationGraph. Useful when a native host
    // staged multiple RTS/scene choices and must publish them as one outer
    // frame-boundary transaction. Does not own Model/GPU resources or clocks.
    bool publishPrepared(const std::string& key,const Asset& prepared,
                         std::string* error=nullptr) {
        if(error)error->clear();
        try {
            if(key.empty() || !prepared.model)
                throw std::invalid_argument("prepared appearance needs key/model");
            if(prepared.graph && !prepared.sourceId)
                throw std::invalid_argument("prepared animated appearance needs sourceId");
            if(prepared.materials.size()<prepared.model->materials.size())
                throw std::invalid_argument("prepared materials missing base entries");
            for(const int index:prepared.nodeMaterial)
                if(index < -1 || (index>=0 &&
                    size_t(index)>=prepared.materials.size()))
                    throw std::invalid_argument("prepared node material index invalid");
            if(versionSerial_==std::numeric_limits<uint64_t>::max())
                throw std::overflow_error("prepared appearance version overflow");
            const auto old=find(key);
            auto next=std::make_shared<Asset>(prepared);
            next->version=versionSerial_+1;
            next->modelPublicationGeneration=ModelPublicationEpoch::next();
            if(next->graph) {
                if(old && old->graph==next->graph &&
                   old->model==next->model &&
                   old->sourceId==next->sourceId)
                    next->generation=old->generation;
                else {
                    if(animationSerial_==std::numeric_limits<uint64_t>::max())
                        throw std::overflow_error("prepared graph generation overflow");
                    next->generation=animationSerial_+1;
                }
            }else next->generation=0;
            assets_.insert_or_assign(key,std::move(next));
            ++versionSerial_;
            if(assets_.at(key)->graph)
                animationSerial_=std::max(animationSerial_,
                                          assets_.at(key)->generation);
            dirty_.erase(key);dirtyGraph_.erase(key);
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    void erase(const std::string& key) {
        assets_.erase(key);
        dirty_.erase(key);
        dirtyGraph_.erase(key);
    }
    void clear() {
        assets_.clear();dirty_.clear();dirtyGraph_.clear();
        // Serial counters intentionally survive clear(), preventing an old
        // animated source from aliasing a new generation in a live View.
    }
private:
    // Mutation is confined to the owning presentation/render thread. Stable
    // shared snapshots may be read by consumers that hold their own handles.
    std::unordered_map<std::string,Snapshot> assets_;
    std::unordered_set<std::string> dirty_,dirtyGraph_;
    uint64_t versionSerial_=0,animationSerial_=0;
};

} // namespace aiverse::view
