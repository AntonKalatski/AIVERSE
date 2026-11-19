// AIVERSE View — model, whole-asset LOD and socket attachment composition.
// Presentation-only. Models and GPU memory remain owned by the calling host.
// No game genres, simulation components, animation clock or RHI allocations.
#pragma once

#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace aiverse::view {

// Optional whole-model LOD replacements. The base ModelDraw::model is LOD 0;
// entries are farther variants, sorted by strictly increasing distance.
// Renderer3D also has its own *within-model mesh* LOD, independent of this.
struct ModelLod {
    const render::Model* model=nullptr;
    float minDistance=0.f;
};

inline bool validModelLods(std::span<const ModelLod> levels) noexcept {
    float previous=0.f;
    for(const auto& level:levels) {
        if(!std::isfinite(level.minDistance) ||
           level.minDistance<=previous)return false;
        previous=level.minDistance;
    }
    return true;
}

// Pure, deterministic hysteretic distance choice for one source ID.
// The animation graph still runs on the base animation Model regardless
// of which GPU Model is drawn. h=0 gives distance thresholds exactly.
inline size_t selectModelLodLevel(std::span<const ModelLod> levels,
                                  float distance,size_t previous=0,
                                  float hysteresis=0.1f) noexcept {
    if(!validModelLods(levels) || !std::isfinite(distance) ||
       distance<0.f)return 0;
    const float h=std::clamp(std::isfinite(hysteresis)?hysteresis:0.f,
                             0.f,0.45f);
    size_t selected=std::min(previous,levels.size());
    while(selected<levels.size() &&
          distance>=levels[selected].minDistance*(1.f+h))
        ++selected;
    while(selected>0 &&
          distance<levels[selected-1].minDistance*(1.f-h))
        --selected;
    return selected;
}

// Reject incompatible skinning layouts instead of interpreting a stale
// base-model Pose as a differently imported LOD skeleton. Different
// meshes/materials are allowed; bone ordering, bind TRS and skin joints
// must match. A failed check renders the far LOD in its own bind pose,
// while sockets keep following the base animation rig.
inline bool compatibleModelPose(const render::Model& source,
                                const render::Model& visual) noexcept {
    if(&source==&visual)return true;
    if(source.nodes.size()!=visual.nodes.size() ||
       source.skins.size()!=visual.skins.size())return false;
    auto near=[](float a,float b) {
        return std::isfinite(a)&&std::isfinite(b) &&
               std::abs(a-b)<=0.0005f;
    };
    for(size_t i=0;i<source.nodes.size();++i) {
        const auto& a=source.nodes[i];
        const auto& b=visual.nodes[i];
        if(a.name!=b.name || a.parent!=b.parent || a.skin!=b.skin ||
           !near(a.t.x,b.t.x)||!near(a.t.y,b.t.y)||!near(a.t.z,b.t.z) ||
           !near(a.r.x,b.r.x)||!near(a.r.y,b.r.y)||
           !near(a.r.z,b.r.z)||!near(a.r.w,b.r.w) ||
           !near(a.s.x,b.s.x)||!near(a.s.y,b.s.y)||!near(a.s.z,b.s.z))
            return false;
    }
    for(size_t i=0;i<source.skins.size();++i) {
        const auto& a=source.skins[i];
        const auto& b=visual.skins[i];
        if(a.joints!=b.joints || a.inverseBind.size()!=b.inverseBind.size())
            return false;
        for(size_t j=0;j<a.inverseBind.size();++j)
            for(size_t k=0;k<16;++k)
                if(!near(a.inverseBind[j].m[k],b.inverseBind[j].m[k]))
                    return false;
    }
    return true;
}

struct ModelAttachment {
    // The child is a standalone synchronous draw. Its own world matrix
    // is local to the socket, after attachment.local. It does not receive
    // an AnimationGraph update; use an independent View Object for that.
    render::ModelDraw draw;
    int socket=-1;               // -1 = parent root; nonnegative = node index
    std::string socketName;      // optional name resolved on BASE animation rig
    render::mat4 local;
    bool visible=true;
};

// Compose an already evaluated socket pose from any host. This API is
// usable from native projects, RTS compatibility adapters and View Objects.
// nodeWorld is model-space, not absolute scene/world-space. The host must
// keep model and draw resource pointers alive through submission.
// Invalid socket indices are errors, never silently parented at root.
inline bool composeAttachment(const render::ModelDraw& parent,
                              const render::Model& rig,
                              std::span<const render::mat4> nodeWorld,
                              const render::ModelDraw& child,
                              int socket,
                              const render::mat4& local,
                              render::ModelDraw& output) {
    if(!child.model || socket< -1)return false;
    render::mat4 socketPose{};
    if(socket>=0) {
        const size_t index=size_t(socket);
        if(index>=rig.nodes.size())return false;
        if(nodeWorld.size()==rig.nodes.size())
            socketPose=nodeWorld[index];
        else if(rig.bindWorld.size()==rig.nodes.size())
            socketPose=rig.bindWorld[index];
        else return false;
    }
    output=child;
    output.world=parent.world*socketPose*local*child.world;
    return true;
}

inline bool composeAttachment(const render::ModelDraw& parent,
                              const render::Model& rig,
                              std::span<const render::mat4> nodeWorld,
                              const ModelAttachment& attachment,
                              render::ModelDraw& output) {
    if(!attachment.visible)return false;
    int socket=attachment.socket;
    if(!attachment.socketName.empty()) {
        socket=rig.findNode(attachment.socketName);
        if(socket<0)return false;
    }
    return composeAttachment(parent,rig,nodeWorld,
                             attachment.draw,socket,attachment.local,output);
}

} // namespace aiverse::view
