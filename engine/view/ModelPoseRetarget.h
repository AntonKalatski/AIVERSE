// Explicit source-rig -> visual LOD pose mapping. Presentation-only.
// The source Animator/MotionWeave state is authoritative for the rendered pose;
// visual LOD remapping must never drive the source animation World or physics.
#pragma once
#include "engine/render/Animation.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
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

} // namespace aiverse::view
