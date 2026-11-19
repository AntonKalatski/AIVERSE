// Genre-neutral, owner-thread appearance publication with pinned immutable
// snapshots. Rendering and AnimationGraph clocks remain in a single View.
#pragma once
#include "AppearanceAuthoringService.h"
#include "AppearanceChoiceAuthoring.h"
#include "ModelPublicationEpoch.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
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
            spec.modelPublicationGeneration=modelPublicationGeneration;
            return buildAppearancePacket(spec,world,out);
        }

        // Native C++ authoring helper: compile a visual LOD against two
        // already published, immutable snapshots. Both revisions must be
        // nonzero and are checked again by View::submitOne on every draw.
        // On failure, out is unchanged, preserving a host's last-good LOD.
        // This pins the plan, NOT the borrowed Models or their GPU buffers.
        bool compileVisualLod(const Asset& visual,float minDistance,
                              const ModelPoseRetarget& authored,
                              ModelLod& out,std::string* error=nullptr) const {
            if(error)error->clear();
            try {
                if(!model || !visual.model ||
                   !modelPublicationGeneration ||
                   !visual.modelPublicationGeneration)
                    throw std::invalid_argument(
                        "compiled LOD needs two published Model snapshots");
                if(!std::isfinite(minDistance) || minDistance<=0.f)
                    throw std::invalid_argument(
                        "compiled LOD distance must be finite and positive");
                auto plan=compileModelPoseRetarget(
                    *model,*visual.model,authored,
                    modelPublicationGeneration,
                    visual.modelPublicationGeneration);
                if(!plan) {
                    if(error) {
                        const auto report=describeModelPoseRetarget(
                            *model,*visual.model,authored,
                            modelPublicationGeneration,
                            visual.modelPublicationGeneration);
                        *error=std::string(modelPoseRetargetIssueName(report.issue))+
                            " (visual node "+std::to_string(report.targetNode)+")";
                    }
                    return false;
                }
                ModelLod candidate;
                candidate.model=visual.model;
                candidate.minDistance=minDistance;
                candidate.compiledRetarget=std::move(plan);
                candidate.visualModelGeneration=
                    visual.modelPublicationGeneration;
                out=std::move(candidate);
                return true;
            }catch(const std::exception& ex) {
                if(error)*error=ex.what();
                return false;
            }
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
            const bool changedModel=!asset->modelSource.empty() &&
                changed.contains(asset->modelSource);
            const bool changedGraph=!asset->graphSource.empty() &&
                changed.contains(asset->graphSource);
            if(changedModel)dirtyModel_.insert(key);
            if(changedModel || changedGraph)dirtyGraph_.insert(key);
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
            // Appearance/material/graph edits are NOT new Model assets.
            // Keep retarget maps valid if both the host Model revision and
            // borrowed Model identity are unchanged. Source invalidation
            // explicitly marks in-place reimports even with pointer reuse.
            const bool sameModel=previous &&
                previous->model==next->model &&
                previous->modelRevision==next->modelRevision &&
                previous->modelSource==next->modelSource &&
                !dirtyModel_.contains(key) &&
                previous->modelPublicationGeneration!=0;
            next->modelPublicationGeneration=sameModel
                ?previous->modelPublicationGeneration
                :ModelPublicationEpoch::next();
            // Commit only after every potentially throwing preparation step.
            assets_.insert_or_assign(key,std::move(next));
            ++versionSerial_;
            if(assets_.at(key)->graph)
                animationSerial_=std::max(animationSerial_,assets_.at(key)->generation);
            dirty_.erase(key);
            dirtyGraph_.erase(key);
            dirtyModel_.erase(key);
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
            const bool sameModel=old &&
                old->model==next->model &&
                old->modelRevision==next->modelRevision &&
                old->modelSource==next->modelSource &&
                !dirtyModel_.contains(key) &&
                old->modelPublicationGeneration!=0;
            next->modelPublicationGeneration=sameModel
                ?old->modelPublicationGeneration
                :ModelPublicationEpoch::next();
            if(next->graph) {
                if(old && old->graph==next->graph &&
                   old->model==next->model &&
                   old->sourceId==next->sourceId &&
                   old->modelRevision==next->modelRevision &&
                   !dirtyGraph_.contains(key) &&
                   !dirtyModel_.contains(key))
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
            dirty_.erase(key);dirtyGraph_.erase(key);dirtyModel_.erase(key);
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
        dirtyModel_.erase(key);
    }
    void clear() {
        assets_.clear();dirty_.clear();dirtyGraph_.clear();dirtyModel_.clear();
        // Serial counters intentionally survive clear(), preventing an old
        // animated source from aliasing a new generation in a live View.
    }
private:
    // Mutation is confined to the owning presentation/render thread. Stable
    // shared snapshots may be read by consumers that hold their own handles.
    std::unordered_map<std::string,Snapshot> assets_;
    std::unordered_set<std::string> dirty_,dirtyGraph_,dirtyModel_;
    uint64_t versionSerial_=0,animationSerial_=0;
};

// Retains published appearance/material/graph snapshots across one
// View::update() + synchronous View::submit() transaction. Native and
// cinematic hosts may construct a batch while the asset registry is
// hot reloading other keys. Pointer fields in ModelDraw refer into the
// pinned snapshots, not the registry's replaceable current entries.
//
// Important: snapshots own ONLY appearance CPU values + shared Graph,
// NOT a Model or its GPU resources. The host still retains each borrowed
// Model and its Device epoch through the animation worker join and draw.
class PinnedAppearanceBatch final {
public:
    using Snapshot=AppearanceAssetRegistry::Snapshot;
    void reserve(size_t count) {
        // Reserve first for stable, failure-safe packet assembly.
        pins_.reserve(count);
        packets_.reserve(count);
    }
    bool add(Snapshot asset,const render::mat4& world) {
        if(!asset)return false;
        Object packet;
        if(!asset->packet(world,packet))return false;
        pins_.push_back(std::move(asset));
        try {
            packets_.push_back(std::move(packet));
        }catch(...) {
            pins_.pop_back();
            throw;
        }
        return true;
    }
    size_t size() const noexcept{return packets_.size();}
    bool empty() const noexcept{return packets_.empty();}
    const std::vector<Object>& objects() const noexcept {return packets_;}
    // Modify model LODs, attachments or significance AFTER constructing the
    // batch, not while View::update / Render3D submission is in progress.
    // References to packet elements may be invalidated by subsequent add().
    Object& edit(size_t index) {return packets_.at(index);}
    void clear() noexcept {
        // End of the host's joined update/render transaction only.
        packets_.clear();
        pins_.clear();
    }
private:
    std::vector<Snapshot> pins_;
    std::vector<Object> packets_;
};

} // namespace aiverse::view
