// Explicit source-rig -> visual LOD pose mapping. Presentation-only.
// The source Animator/MotionWeave state is authoritative for the rendered pose;
// visual LOD remapping must never drive the source animation World or physics.
#pragma once
#include "engine/render/Animation.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace aiverse::view {

// Source index for each visual Model node. -1 preserves the target bind pose.
// The mapping is authored by the Model/LOD asset producer, never inferred by
// copying a source index into an unrelated target node layout.
struct ModelPoseRetarget {
    std::vector<int> sourceNodeForVisual;
    // Optional per-visual-node morph map: source morph channel for each target
    // channel. -1 preserves target bind weight. Empty means no morph transfer.
    std::vector<std::array<int,render::kMaxMorphTargets>> sourceMorphForVisual;
    float translationScale=1.f;
};

inline bool validModelPoseRetarget(const render::Model& source,
                                  const render::Model& visual,
                                  const ModelPoseRetarget& map) {
    if(map.sourceNodeForVisual.size()!=visual.nodes.size() ||
       (!map.sourceMorphForVisual.empty() &&
        map.sourceMorphForVisual.size()!=visual.nodes.size()) ||
       !std::isfinite(map.translationScale) ||
       map.translationScale<0.f || map.translationScale>1000.f)
        return false;
    std::vector<bool> seen(source.nodes.size(),false);
    size_t mapped=0;
    for(size_t i=0;i<visual.nodes.size();++i) {
        const int sourceId=map.sourceNodeForVisual[i];
        if(sourceId< -1 || (sourceId>=0 && size_t(sourceId)>=source.nodes.size()))
            return false;
        if(sourceId>=0) {
            if(seen[size_t(sourceId)])return false;
            seen[size_t(sourceId)]=true;
            ++mapped;
        }
        const auto& dst=visual.nodes[i];
        if(dst.parent< -1 ||
           (dst.parent>=0 && size_t(dst.parent)>=visual.nodes.size()))
            return false;
        // Model::nodes is stored in importer order, not necessarily
        // parent-before-child. Validate cycles without assuming indices.
        int parent=dst.parent;
        for(size_t step=0;parent>=0;++step) {
            if(step>=visual.nodes.size() || parent==int(i))return false;
            parent=visual.nodes[size_t(parent)].parent;
            if(parent< -1 ||
               (parent>=0 && size_t(parent)>=visual.nodes.size()))
                return false;
        }
        if(sourceId>=0) {
            const auto& src=source.nodes[size_t(sourceId)];
            // An explicitly mapped visual child must not attach to an
            // unrelated source hierarchy. Unmapped target parents retain
            // the bind pose, permitting authored lower-bone-count LODs.
            if(dst.parent>=0) {
                const int parentSource=map.sourceNodeForVisual[size_t(dst.parent)];
                if(parentSource>=0) {
                    int ancestor=src.parent;
                    bool found=false;
                    for(size_t steps=0;steps<source.nodes.size() && ancestor>=0;++steps) {
                        if(ancestor==parentSource){found=true;break;}
                        if(size_t(ancestor)>=source.nodes.size())return false;
                        const int next=source.nodes[size_t(ancestor)].parent;
                        if(next==ancestor)return false;
                        ancestor=next;
                    }
                    if(!found)return false;
                }
            }
        }
        if(map.sourceMorphForVisual.empty())continue;
        const auto& channels=map.sourceMorphForVisual[i];
        int sourceMorphCount=0,visualMorphCount=0;
        if(sourceId>=0) {
            const int mesh=source.nodes[size_t(sourceId)].mesh;
            if(mesh>=0 && size_t(mesh)<source.meshes.size())
                for(const auto& prim:source.meshes[size_t(mesh)].primitives)
                    sourceMorphCount=std::max(sourceMorphCount,prim.morphTargets);
        }
        if(dst.mesh>=0 && size_t(dst.mesh)<visual.meshes.size())
            for(const auto& prim:visual.meshes[size_t(dst.mesh)].primitives)
                visualMorphCount=std::max(visualMorphCount,prim.morphTargets);
        for(size_t k=0;k<channels.size();++k) {
            const int from=channels[k];
            if(from< -1 || (from>=0 &&
               (sourceId<0 || int(k)>=visualMorphCount ||
                from>=sourceMorphCount)))
                return false;
        }
    }
    return mapped>0;
}

// Rebuilds a target-local pose using the authored target rest pose, applying
// ONLY the per-bone source animation delta. This intentionally does not copy
// source inverse-bind matrices or mesh-local morph slots. Source parents may
// be omitted by coarse LODs; the target hierarchy controls the new world
// matrices. Full rig retarget with twist constraints is a distinct feature.
// On failure outputs are untouched, allowing a bind-pose fallback.
inline bool retargetModelPose(const render::Model& source,
                             const render::Model& visual,
                             std::span<const render::NodePose> sourcePose,
                             const ModelPoseRetarget& map,
                             render::Pose& out,
                             std::vector<render::mat4>& world) {
    if(sourcePose.size()!=source.nodes.size() ||
       !validModelPoseRetarget(source,visual,map))return false;
    render::Pose next;
    render::bindPose(visual,next);
    const auto finite=[](float n){return std::isfinite(n);};
    for(size_t i=0;i<visual.nodes.size();++i) {
        const int sourceId=map.sourceNodeForVisual[i];
        if(sourceId<0)continue;
        const auto& s=source.nodes[size_t(sourceId)];
        const auto& pose=sourcePose[size_t(sourceId)];
        const auto& target=visual.nodes[i];
        for(float f:{pose.t.x,pose.t.y,pose.t.z,pose.r.x,pose.r.y,
                     pose.r.z,pose.r.w,pose.s.x,pose.s.y,pose.s.z,
                     s.s.x,s.s.y,s.s.z})
            if(!finite(f))return false;
        if(std::abs(s.s.x)<1.e-6f || std::abs(s.s.y)<1.e-6f ||
           std::abs(s.s.z)<1.e-6f)return false;
        const render::quat bind=render::qnormalize(s.r);
        const render::quat inverse{-bind.x,-bind.y,-bind.z,bind.w};
        auto& dst=next[i];
        dst.t=target.t+(pose.t-s.t)*map.translationScale;
        dst.r=render::qnormalize(target.r*(inverse*pose.r));
        dst.s={target.s.x*(pose.s.x/s.s.x),target.s.y*(pose.s.y/s.s.y),
               target.s.z*(pose.s.z/s.s.z)};
        for(float f:{dst.t.x,dst.t.y,dst.t.z,dst.s.x,dst.s.y,dst.s.z})
            if(!finite(f))return false;
        if(!map.sourceMorphForVisual.empty())
            for(int k=0;k<render::kMaxMorphTargets;++k) {
                const int from=map.sourceMorphForVisual[i][size_t(k)];
                if(from<0)continue;
                const float weight=pose.w[from];
                if(!finite(weight))return false;
                dst.w[k]=weight;
            }
    }
    std::vector<render::mat4> matrices;
    render::worldMatrices(visual,next,matrices);
    out=std::move(next);
    world=std::move(matrices);
    return true;
}

// A validated, immutable plan for a particular pair of ASSET generations.
// Never infer generation from a Model pointer: allocators may reuse addresses
// after hot reload. The host owns the nonzero generation of each Model and
// publishes a NEW plan whenever source or visual assets are replaced.
//
// All source/target bind transforms and morph indices are copied at compile
// time. Runtime pose evaluation never traverses/validates the mapping and
// never allocates validation scratch, even for thousands of crowd instances.
// The output Pose and world matrices are still per-draw transient buffers.
struct CompiledModelPoseRetarget final {
    struct NodeStep {
        int source=-1;
        render::float3 sourceBindT{},sourceBindS{1,1,1};
        render::quat inverseSourceBind{};
        render::float3 targetBindT{},targetBindS{1,1,1};
        render::quat targetBindR{};
        std::array<int,render::kMaxMorphTargets> morph{};
    };
    const render::Model* sourceModel=nullptr; // identity only, never dereference until generation check
    const render::Model* visualModel=nullptr;
    uint64_t sourceGeneration=0,visualGeneration=0;
    float translationScale=1.f;
    render::Pose visualBind;
    std::vector<NodeStep> steps;
    size_t mappedNodes=0;
    bool matches(const render::Model& source,const render::Model& visual,
                 uint64_t sourceGen,uint64_t visualGen) const noexcept {
        // A zero generation is deliberately not trusted: old cached maps
        // could otherwise survive pointer reuse after an asset reload.
        return sourceGen!=0 && visualGen!=0 &&
               sourceGeneration==sourceGen && visualGeneration==visualGen &&
               sourceModel==&source && visualModel==&visual &&
               steps.size()==visualBind.size() &&
               steps.size()==visual.nodes.size();
    }
};

// Compile only at an asset-publication boundary on the presentation owner
// thread. The returned shared const plan can be safely reused by many
// instances; it contains NO pointers into Model vectors. Null = rejected.
inline std::shared_ptr<const CompiledModelPoseRetarget>
compileModelPoseRetarget(const render::Model& source,
                         const render::Model& visual,
                         const ModelPoseRetarget& map,
                         uint64_t sourceGeneration,
                         uint64_t visualGeneration) {
    if(sourceGeneration==0 || visualGeneration==0 ||
       !validModelPoseRetarget(source,visual,map))return {};
    // Fail once at publication for malformed bind transforms. A weak or
    // zero-scale source bone must not force the same runtime rejection
    // for every member of a large animated crowd.
    const auto finite=[](float v){return std::isfinite(v);};
    for(size_t i=0;i<visual.nodes.size();++i) {
        const auto& dst=visual.nodes[i];
        for(float v:{dst.t.x,dst.t.y,dst.t.z,dst.r.x,dst.r.y,
                     dst.r.z,dst.r.w,dst.s.x,dst.s.y,dst.s.z})
            if(!finite(v))return {};
        const int sourceId=map.sourceNodeForVisual[i];
        if(sourceId<0)continue;
        const auto& src=source.nodes[size_t(sourceId)];
        for(float v:{src.t.x,src.t.y,src.t.z,src.r.x,src.r.y,
                     src.r.z,src.r.w,src.s.x,src.s.y,src.s.z})
            if(!finite(v))return {};
        if(std::abs(src.s.x)<1.e-6f || std::abs(src.s.y)<1.e-6f ||
           std::abs(src.s.z)<1.e-6f)return {};
    }
    auto compiled=std::make_shared<CompiledModelPoseRetarget>();
    compiled->sourceModel=&source;
    compiled->visualModel=&visual;
    compiled->sourceGeneration=sourceGeneration;
    compiled->visualGeneration=visualGeneration;
    compiled->translationScale=map.translationScale;
    render::bindPose(visual,compiled->visualBind);
    compiled->steps.resize(visual.nodes.size());
    for(size_t i=0;i<visual.nodes.size();++i) {
        auto& step=compiled->steps[i];
        step.morph.fill(-1);
        step.source=map.sourceNodeForVisual[i];
        const auto& target=visual.nodes[i];
        step.targetBindT=target.t;
        step.targetBindR=render::qnormalize(target.r);
        step.targetBindS=target.s;
        if(step.source<0)continue;
        const auto& src=source.nodes[size_t(step.source)];
        step.sourceBindT=src.t;
        step.sourceBindS=src.s;
        const render::quat rotation=render::qnormalize(src.r);
        step.inverseSourceBind={-rotation.x,-rotation.y,-rotation.z,rotation.w};
        if(!map.sourceMorphForVisual.empty())
            step.morph=map.sourceMorphForVisual[i];
        ++compiled->mappedNodes;
    }
    return compiled;
}

// Authoring-only diagnostics for rejected LOD maps. Call only when
// compileModelPoseRetarget returned null; never from an actor draw.
// No gameplay/asset state is mutated; the first actionable problem
// and target node index let importers report useful authoring warnings.
enum class ModelPoseRetargetIssue : uint8_t {
    None,
    MissingAssetGeneration,
    InvalidMapShape,
    InvalidTranslationScale,
    UnmappedRig,
    InvalidSourceNode,
    DuplicateSourceNode,
    TargetHierarchyCycle,
    InvalidTargetParent,
    IncompatibleAncestry,
    InvalidMorphChannel,
    NonFiniteBindTransform,
    DegenerateSourceBindScale,
    Other
};

struct ModelPoseRetargetReport {
    ModelPoseRetargetIssue issue=ModelPoseRetargetIssue::None;
    size_t targetNode=0;
    size_t mappedNodes=0;
    size_t unboundNodes=0;
    bool valid() const noexcept{return issue==ModelPoseRetargetIssue::None;}
};

inline const char* modelPoseRetargetIssueName(
        ModelPoseRetargetIssue issue) noexcept {
    switch(issue) {
        case ModelPoseRetargetIssue::None:return "valid";
        case ModelPoseRetargetIssue::MissingAssetGeneration:return "missing asset publication generation";
        case ModelPoseRetargetIssue::InvalidMapShape:return "map length does not match visual rig";
        case ModelPoseRetargetIssue::InvalidTranslationScale:return "nonfinite or out-of-range translation scale";
        case ModelPoseRetargetIssue::UnmappedRig:return "no visual nodes reference source bones";
        case ModelPoseRetargetIssue::InvalidSourceNode:return "target node maps to an invalid source node";
        case ModelPoseRetargetIssue::DuplicateSourceNode:return "multiple visual nodes map to the same source bone";
        case ModelPoseRetargetIssue::TargetHierarchyCycle:return "visual skeleton hierarchy has a cycle";
        case ModelPoseRetargetIssue::InvalidTargetParent:return "visual skeleton parent index is invalid";
        case ModelPoseRetargetIssue::IncompatibleAncestry:return "source and visual mapped hierarchy disagree";
        case ModelPoseRetargetIssue::InvalidMorphChannel:return "morph map references unavailable channel";
        case ModelPoseRetargetIssue::NonFiniteBindTransform:return "source or visual bind has nonfinite transform";
        case ModelPoseRetargetIssue::DegenerateSourceBindScale:return "source bind scale is zero or degenerate";
        case ModelPoseRetargetIssue::Other:return "unsupported rig retarget mapping";
    }
    return "unsupported rig retarget mapping";
}

inline ModelPoseRetargetReport describeModelPoseRetarget(
        const render::Model& source,const render::Model& visual,
        const ModelPoseRetarget& map,uint64_t sourceGeneration,
        uint64_t visualGeneration) {
    ModelPoseRetargetReport report;
    const auto fail=[&](ModelPoseRetargetIssue issue,size_t node) {
        report.issue=issue;
        report.targetNode=node;
        return report;
    };
    if(!sourceGeneration || !visualGeneration)
        return fail(ModelPoseRetargetIssue::MissingAssetGeneration,0);
    if(map.sourceNodeForVisual.size()!=visual.nodes.size() ||
       (!map.sourceMorphForVisual.empty() &&
        map.sourceMorphForVisual.size()!=visual.nodes.size()))
        return fail(ModelPoseRetargetIssue::InvalidMapShape,0);
    if(!std::isfinite(map.translationScale) ||
       map.translationScale<0.f || map.translationScale>1000.f)
        return fail(ModelPoseRetargetIssue::InvalidTranslationScale,0);
    std::vector<bool> visited(source.nodes.size(),false);
    const auto finite=[](float value) {return std::isfinite(value);};
    for(size_t i=0;i<visual.nodes.size();++i) {
        const int srcIndex=map.sourceNodeForVisual[i];
        const auto& dst=visual.nodes[i];
        if(srcIndex< -1 ||
           (srcIndex>=0 && size_t(srcIndex)>=source.nodes.size()))
            return fail(ModelPoseRetargetIssue::InvalidSourceNode,i);
        if(dst.parent< -1 ||
           (dst.parent>=0 && size_t(dst.parent)>=visual.nodes.size()))
            return fail(ModelPoseRetargetIssue::InvalidTargetParent,i);
        int parent=dst.parent;
        for(size_t step=0;parent>=0;++step) {
            if(step>=visual.nodes.size() || parent==int(i))
                return fail(ModelPoseRetargetIssue::TargetHierarchyCycle,i);
            parent=visual.nodes[size_t(parent)].parent;
            if(parent< -1 ||
               (parent>=0 && size_t(parent)>=visual.nodes.size()))
                return fail(ModelPoseRetargetIssue::InvalidTargetParent,i);
        }
        for(float value:{dst.t.x,dst.t.y,dst.t.z,
                         dst.r.x,dst.r.y,dst.r.z,dst.r.w,
                         dst.s.x,dst.s.y,dst.s.z})
            if(!finite(value))
                return fail(ModelPoseRetargetIssue::NonFiniteBindTransform,i);
        if(srcIndex<0) {
            ++report.unboundNodes;
        }else {
            if(visited[size_t(srcIndex)])
                return fail(ModelPoseRetargetIssue::DuplicateSourceNode,i);
            visited[size_t(srcIndex)]=true;
            ++report.mappedNodes;
            const auto& src=source.nodes[size_t(srcIndex)];
            for(float value:{src.t.x,src.t.y,src.t.z,
                             src.r.x,src.r.y,src.r.z,src.r.w,
                             src.s.x,src.s.y,src.s.z})
                if(!finite(value))
                    return fail(ModelPoseRetargetIssue::NonFiniteBindTransform,i);
            if(std::abs(src.s.x)<1.e-6f ||
               std::abs(src.s.y)<1.e-6f ||
               std::abs(src.s.z)<1.e-6f)
                return fail(ModelPoseRetargetIssue::DegenerateSourceBindScale,i);
            if(dst.parent>=0) {
                const int sourceParent=map.sourceNodeForVisual[size_t(dst.parent)];
                if(sourceParent>=0) {
                    int ancestor=src.parent;
                    bool found=false;
                    for(size_t step=0;ancestor>=0 && step<source.nodes.size();++step) {
                        if(ancestor==sourceParent){found=true;break;}
                        if(size_t(ancestor)>=source.nodes.size())
                            return fail(ModelPoseRetargetIssue::IncompatibleAncestry,i);
                        const int next=source.nodes[size_t(ancestor)].parent;
                        if(next==ancestor)break;
                        ancestor=next;
                    }
                    if(!found)
                        return fail(ModelPoseRetargetIssue::IncompatibleAncestry,i);
                }
            }
        }
        if(!map.sourceMorphForVisual.empty()) {
            int srcMorphCount=0,dstMorphCount=0;
            if(srcIndex>=0) {
                const int mesh=source.nodes[size_t(srcIndex)].mesh;
                if(mesh>=0 && size_t(mesh)<source.meshes.size())
                    for(const auto& primitive:source.meshes[size_t(mesh)].primitives)
                        srcMorphCount=std::max(srcMorphCount,primitive.morphTargets);
            }
            if(dst.mesh>=0 && size_t(dst.mesh)<visual.meshes.size())
                for(const auto& primitive:visual.meshes[size_t(dst.mesh)].primitives)
                    dstMorphCount=std::max(dstMorphCount,primitive.morphTargets);
            for(size_t k=0;k<render::kMaxMorphTargets;++k) {
                const int from=map.sourceMorphForVisual[i][k];
                if(from< -1 || (from>=0 &&
                   (srcIndex<0 || int(k)>=dstMorphCount || from>=srcMorphCount)))
                    return fail(ModelPoseRetargetIssue::InvalidMorphChannel,i);
            }
        }
    }
    if(report.mappedNodes==0)
        return fail(ModelPoseRetargetIssue::UnmappedRig,0);
    if(!validModelPoseRetarget(source,visual,map))
        return fail(ModelPoseRetargetIssue::Other,0);
    return report;
}

// O(visual nodes) transform only, with no per-instance map validation
// or heap-allocated visited flags. A stale plan fails BEFORE accessing
// any Model node. Host must keep each supplied Model alive through this
// call and synchronous submission; this API cannot extend borrowed life.
inline bool retargetCompiledModelPose(
        const render::Model& source,const render::Model& visual,
        uint64_t sourceGeneration,uint64_t visualGeneration,
        std::span<const render::NodePose> sourcePose,
        const CompiledModelPoseRetarget& plan,
        render::Pose& out,std::vector<render::mat4>& world) {
    if(!plan.matches(source,visual,sourceGeneration,visualGeneration) ||
       sourcePose.size()!=source.nodes.size())return false;
    // Validate dynamic input before overwriting any reusable output.
    // The plan's bind transforms, mapping bounds and morph channels were
    // checked once at publication time.
    for(const auto& step:plan.steps) {
        if(step.source<0)continue;
        if(size_t(step.source)>=sourcePose.size())return false;
        const auto& pose=sourcePose[size_t(step.source)];
        for(float f:{pose.t.x,pose.t.y,pose.t.z,
                     pose.r.x,pose.r.y,pose.r.z,pose.r.w,
                     pose.s.x,pose.s.y,pose.s.z})
            if(!std::isfinite(f))return false;
        for(int from:step.morph)
            if(from>=0 && (from>=render::kMaxMorphTargets ||
                           !std::isfinite(pose.w[from])))return false;
    }
    // Assignment reuses the output's vector capacity from earlier draws.
    // Renderer3D consumes it before the next synchronous submitModel.
    out=plan.visualBind;
    for(size_t i=0;i<plan.steps.size();++i) {
        const auto& step=plan.steps[i];
        if(step.source<0)continue;
        const auto& pose=sourcePose[size_t(step.source)];
        auto& dst=out[i];
        dst.t=step.targetBindT +
              (pose.t-step.sourceBindT)*plan.translationScale;
        dst.r=render::qnormalize(step.targetBindR*
                                (step.inverseSourceBind*pose.r));
        dst.s={
            step.targetBindS.x*(pose.s.x/step.sourceBindS.x),
            step.targetBindS.y*(pose.s.y/step.sourceBindS.y),
            step.targetBindS.z*(pose.s.z/step.sourceBindS.z)};
        for(float f:{dst.t.x,dst.t.y,dst.t.z,dst.s.x,dst.s.y,dst.s.z})
            if(!std::isfinite(f))return false;
        for(size_t k=0;k<step.morph.size();++k) {
            const int from=step.morph[k];
            if(from<0)continue;
            if(from>=render::kMaxMorphTargets)return false;
            const float value=pose.w[from];
            if(!std::isfinite(value))return false;
            dst.w[k]=value;
        }
    }
    // worldMatrices resizes in place, preserving matrix capacity as well.
    render::worldMatrices(visual,out,world);
    return true;
}

} // namespace aiverse::view
