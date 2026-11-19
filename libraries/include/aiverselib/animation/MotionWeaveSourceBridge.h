// MotionWeave source-to-presentation ECS adapter.
// The library deliberately knows NOTHING about genres, input devices,
// replicated actor layouts, weapon mechanics or game-module components.
#pragma once
#include "EcsAnimationSystems.h"
#include <cstdint>
#include <map>
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>

namespace aiverse::lib::animation {

// Stable identity for an animation source in any host application.
// generation is host-owned; increment on logical replacement/respawn.
struct AnimationSourceLinkComponent {
    uint64_t sourceId=0;
    uint64_t generation=0;
    // Optional host Model asset-publication identity. Independent from
    // logical entity respawn and AnimationGraph compilation generation.
    // A zero generation preserves compatibility with older native hosts.
    uint64_t modelPublicationGeneration=0;
};

// A snapshot of authored animation inputs, not a gameplay entity object.
// Parameter names such as "speed", "cast", or "open" are chosen in the graph;
// the animation library never interprets their domain-specific meaning.
struct AnimationSourceFrame {
    uint64_t sourceId=0;
    uint64_t generation=0;
    const render::Model* model=nullptr;
    std::shared_ptr<const render::AnimationGraph> graph;
    render::AnimationParameters inputs;
    // Appended after legacy aggregate members to preserve older native
    // source callers. Hosts with hot reload must supply nonzero epochs.
    uint64_t modelPublicationGeneration=0;
};
struct AnimationBridgeStats {
    size_t created=0, updated=0, restarted=0, removed=0;
    size_t assetEpochRestarts=0;
    size_t playbackCaptured=0; // safe named state+clip handoff accepted
    size_t playbackDeclined=0; // identity or rig not compatible
};

// One bridge per presentation World / view. Hosts own an independent
// simulation and project any state into AnimationSourceFrame every frame.
// sync() owns entity lifetime for the sources passed to it, but intentionally
// never calls an animation updater, gameplay callbacks, or a renderer.
// No World structural changes are allowed during MotionWeave worker dispatch.
class MotionWeaveSourceBridge {
public:
    // Reject an invalid/duplicate snapshot before making changes to World.
    // Missing sources are removed only when removeAbsent=true.
    AnimationBridgeStats sync(World& presentation,
                              std::span<const AnimationSourceFrame> sources,
                              bool removeAbsent=true) {
        std::set<uint64_t> keys;
        // Preflight fresh immutable node-name/parent signatures before any
        // presentation World mutation. One allocation per distinct Model
        // publication per sync, not per actor sharing that Model.
        std::map<uint64_t,std::shared_ptr<const RigHierarchy>> incomingRigs;
        std::map<RigKey,std::shared_ptr<const RigHierarchy>,RigKeyLess> distinctRigs;
        for(const auto& input:sources) {
            if(!keys.insert(input.sourceId).second)
                throw std::invalid_argument("MotionWeave bridge: duplicate source ID");
            if(!input.model || !input.graph ||
               input.graph->model()!=input.model)
                throw std::invalid_argument("MotionWeave bridge: invalid graph/model");
            // A Model mutated/reimported in place cannot keep using its old
            // compiled graph, even when the C++ Model address is unchanged.
            // Validate before any World structural changes are made.
            const auto it=links_.find(input.sourceId);
            bool changed=it==links_.end() ||
                         !presentation.alive(it->second);
            if(!changed) {
                const auto& previous=presentation.get<
                    AnimationSourceLinkComponent>(it->second);
                const auto& asset=presentation.get<
                    AnimationAssetComponent>(it->second);
                changed=previous.generation!=input.generation ||
                    previous.modelPublicationGeneration!=
                        input.modelPublicationGeneration ||
                    asset.model!=input.model || asset.graph!=input.graph;
                if(input.modelPublicationGeneration!=0 &&
                   previous.modelPublicationGeneration!=0 &&
                   previous.modelPublicationGeneration!=
                       input.modelPublicationGeneration &&
                   asset.model==input.model &&
                   asset.graph==input.graph)
                    throw std::invalid_argument(
                        "MotionWeave bridge: Model asset epoch changed without recompiling AnimationGraph");
            }
            if(changed) {
                const RigKey key{input.model,input.modelPublicationGeneration};
                auto found=distinctRigs.find(key);
                if(found==distinctRigs.end())
                    found=distinctRigs.emplace(
                        key,captureHierarchy(*input.model)).first;
                incomingRigs.emplace(input.sourceId,found->second);
            }
        }
        AnimationBridgeStats stats;
        for(const auto& source:sources) {
            auto existing=links_.find(source.sourceId);
            Entity entity;
            if(existing==links_.end() ||
               !presentation.alive(existing->second)) {
                entity=presentation.create();
                links_[source.sourceId]=entity;
                presentation.add<AnimationSourceLinkComponent>(
                    entity,AnimationSourceLinkComponent{
                        source.sourceId,source.generation,
                        source.modelPublicationGeneration});
                auto& asset=presentation.add<AnimationAssetComponent>(entity);
                asset.model=source.model;
                asset.graph=source.graph;
                presentation.add<AnimationInputsComponent>(entity);
                presentation.add<AnimationPlayerComponent>(entity);
                presentation.add<AnimationPoseComponent>(entity);
                presentation.add<AnimationBudgetComponent>(entity);
                presentation.add<AnimationDiagnosticsComponent>(entity);
                sourceRigs_.insert_or_assign(
                    source.sourceId,incomingRigs.at(source.sourceId));
                ++stats.created;
            } else {
                entity=existing->second;
                auto& link=presentation.get<AnimationSourceLinkComponent>(entity);
                auto& asset=presentation.get<AnimationAssetComponent>(entity);
                const bool assetEpochChanged=
                    link.modelPublicationGeneration!=
                        source.modelPublicationGeneration;
                if(link.generation!=source.generation ||
                   assetEpochChanged || asset.model!=source.model ||
                   asset.graph!=source.graph) {
                    auto& player=presentation.get<AnimationPlayerComponent>(entity);
                    player.pendingPlayback.reset();
                    // Compare COPIED node hierarchies, never dereference a
                    // potentially retired previous Model just to compare
                    // its bones. A same-address in-place GPU reimport may
                    // have invalidated the old graph's clip metadata, so
                    // allow cross-epoch carryover only for distinct Models
                    // with two explicit nonzero publication generations.
                    const auto oldRig=sourceRigs_.find(source.sourceId);
                    const auto newRig=incomingRigs.find(source.sourceId);
                    const bool rigMatches=oldRig!=sourceRigs_.end() &&
                        newRig!=incomingRigs.end() && oldRig->second &&
                        newRig->second && *oldRig->second==*newRig->second;
                    const bool epochSafe=!assetEpochChanged ||
                        (link.modelPublicationGeneration!=0 &&
                         source.modelPublicationGeneration!=0 &&
                         asset.model!=source.model);
                    const bool eligible=link.generation==source.generation &&
                        rigMatches && epochSafe && asset.model && asset.graph &&
                        !player.restart && player.instance &&
                        player.active==asset.graph &&
                        player.instance->graph()==asset.graph.get();
                    if(eligible) {
                        try {
                            // The host must keep the old compiled Model
                            // CPU-readable until this synchronous sync()
                            // returns. This snapshots only state and phases,
                            // not prior native rig/pose/contact/notify data.
                            auto snapshot=player.instance->snapshotPlayback();
                            if(!snapshot.state.empty()) {
                                player.pendingPlayback=std::move(snapshot);
                                ++stats.playbackCaptured;
                            } else ++stats.playbackDeclined;
                        }catch(...) {
                            // Optional cosmetic continuity must not fail
                            // a valid new asset publication.
                            ++stats.playbackDeclined;
                        }
                    } else ++stats.playbackDeclined;
                    player.restart=true;
                    if(auto* overlays=presentation.tryGet<AnimationMontageSlotsComponent>(entity)) {
                        overlays->players.clear();
                        overlays->events.clear();
                    }
                    if(auto* eventStream=presentation.tryGet<AnimationGraphNotifiesComponent>(entity))
                        eventStream->events.clear();
                    ++stats.restarted;
                    if(assetEpochChanged)++stats.assetEpochRestarts;
                }
                link.generation=source.generation;
                link.modelPublicationGeneration=
                    source.modelPublicationGeneration;
                asset.model=source.model;
                asset.graph=source.graph;
                if(auto rig=incomingRigs.find(source.sourceId);
                   rig!=incomingRigs.end())
                    sourceRigs_.insert_or_assign(source.sourceId,rig->second);
                ++stats.updated;
            }
            presentation.get<AnimationInputsComponent>(entity).overrides=
                source.inputs;
        }
        if(removeAbsent) {
            for(auto it=links_.begin();it!=links_.end();) {
                if(keys.count(it->first)) {++it;continue;}
                if(presentation.alive(it->second))
                    presentation.destroy(it->second);
                sourceRigs_.erase(it->first);
                it=links_.erase(it);
                ++stats.removed;
            }
        }
        return stats;
    }
    Entity find(uint64_t sourceId) const {
        const auto it=links_.find(sourceId);
        return it==links_.end()?kNullEntity:it->second;
    }
    size_t size() const noexcept {return links_.size();}
    // Transactional resource invalidation: forget only selected source
    // identities before the host destroys their borrowed Model/Graph.
    // This is a presentation-thread structural operation, never a worker job.
    size_t forget(World& presentation,std::span<const uint64_t> sourceIds) {
        size_t removed=0;
        for(const uint64_t id:sourceIds) {
            auto it=links_.find(id);
            if(it==links_.end())continue;
            if(presentation.alive(it->second))presentation.destroy(it->second);
            sourceRigs_.erase(id);
            links_.erase(it);
            ++removed;
        }
        return removed;
    }
    void clear(World& presentation) {
        for(const auto& [id,entity]:links_) {
            (void)id;
            if(presentation.alive(entity))presentation.destroy(entity);
        }
        links_.clear();
        sourceRigs_.clear();
    }
private:
    // Immutable source rig identity, shared by every actor using the same
    // Model within one sync. Exact name and parent comparisons (no hash
    // collisions); contains no borrowed Model, Bone or native Device ptr.
    using RigHierarchy=std::vector<std::pair<std::string,int>>;
    struct RigKey {
        const render::Model* model=nullptr;
        uint64_t publication=0;
    };
    struct RigKeyLess {
        bool operator()(const RigKey& a,const RigKey& b) const noexcept {
            const auto less=std::less<const render::Model*>{};
            if(less(a.model,b.model))return true;
            if(less(b.model,a.model))return false;
            return a.publication<b.publication;
        }
    };
    static std::shared_ptr<const RigHierarchy> captureHierarchy(
            const render::Model& model) {
        auto nodes=std::make_shared<RigHierarchy>();
        nodes->reserve(model.nodes.size());
        for(const auto& node:model.nodes)
            nodes->emplace_back(node.name,node.parent);
        return nodes;
    }
    std::map<uint64_t,Entity> links_;
    std::map<uint64_t,std::shared_ptr<const RigHierarchy>> sourceRigs_;
};
} // namespace aiverse::lib::animation
