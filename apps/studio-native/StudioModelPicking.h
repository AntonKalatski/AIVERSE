// CPU surface picking for actual AIVERSE render::Model bind-pose geometry.
// This is an opt-in geometry adapter: only select surfaces which are actually
// rendered with the SAME world/node transform. Do not fall back to AABB for
// missing CPU vertices (uploadModel often releases them). Skinning, morphing,
// per-material alpha masks and rendered LOD selection require GPU ID/depth pick
// for pixel accuracy. Hosts allowing rigid masked/LOD models use base geometry
// as an explicitly approximate authoring selection surface.
#pragma once
#include "engine/render/Model.h"
#include "apps/studio-native/StudioVisualPicking.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace aiverse::studio {

struct StudioModelSurfaceHit {
    float distance = std::numeric_limits<float>::infinity();
    int node = -1;
    int mesh = -1;
    int primitive = -1;
    std::size_t triangle = 0;
    explicit operator bool() const { return std::isfinite(distance); }
};

// Moller-Trumbore, double-sided, ray measured in world-space units when dir
// is normalized. Handles thin and open surfaces without inflated bounds.
inline float studioRayTriangle(render::float3 origin,render::float3 direction,
                              render::float3 a,render::float3 b,render::float3 c) {
    constexpr float epsilon=1e-7f;
    const auto edge1=b-a, edge2=c-a;
    const auto p=render::cross(direction,edge2);
    const float determinant=render::dot(edge1,p);
    if (!std::isfinite(determinant) || std::abs(determinant)<epsilon)
        return std::numeric_limits<float>::infinity();
    const float inverse=1.0f/determinant;
    const auto t=origin-a;
    const float u=render::dot(t,p)*inverse;
    if (!std::isfinite(u) || u<0 || u>1) return std::numeric_limits<float>::infinity();
    const auto q=render::cross(t,edge1);
    const float v=render::dot(direction,q)*inverse;
    if (!std::isfinite(v) || v<0 || u+v>1) return std::numeric_limits<float>::infinity();
    const float distance=render::dot(edge2,q)*inverse;
    return std::isfinite(distance) && distance>=epsilon
        ? distance : std::numeric_limits<float>::infinity();
}

inline StudioModelSurfaceHit studioPickModelSurface(
    const render::Model& model,const render::mat4& world,
    render::float3 origin,render::float3 direction,
    std::size_t maxTriangles=1000000) {
    StudioModelSurfaceHit hit;
    const float length2=render::dot(direction,direction);
    if (!std::isfinite(length2) || std::abs(length2-1.0f)>0.01f ||
        !std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z))
        return hit;
    if (model.vertices.empty() || model.indices.empty() ||
        model.bindWorld.size()!=model.nodes.size() || maxTriangles==0)
        return hit;
    std::size_t tested=0;
    for (std::size_t n=0;n<model.nodes.size();++n) {
        const auto& node=model.nodes[n];
        if (node.mesh<0 || std::size_t(node.mesh)>=model.meshes.size()) continue;
        // CPU bind-world is valid for rigid geometry only. Deformed meshes
        // need the same current skin/morph pose as the rendered surface.
        if (node.skin>=0 || model.meshes[std::size_t(node.mesh)].deformable) continue;
        const auto& mesh=model.meshes[std::size_t(node.mesh)];
        const render::mat4 transform=world*model.bindWorld[n];
        for (std::size_t p=0;p<mesh.primitives.size();++p) {
            const auto& primitive=mesh.primitives[p];
            if (primitive.morphTargets>0 || !primitive.jointPalette.empty()) continue;
            if (primitive.firstIndex>model.indices.size() ||
                primitive.indexCount>model.indices.size()-primitive.firstIndex)
                continue;
            for (std::size_t t=0;t+2<primitive.indexCount;t+=3) {
                if (tested++>=maxTriangles) return hit;
                const auto i=primitive.firstIndex+t;
                const uint32_t ia=model.indices[i], ib=model.indices[i+1],ic=model.indices[i+2];
                if (ia>=model.vertices.size()||ib>=model.vertices.size()||
                    ic>=model.vertices.size()) continue;
                const auto a=transform.transformPoint(model.vertices[ia].pos);
                const auto b=transform.transformPoint(model.vertices[ib].pos);
                const auto c=transform.transformPoint(model.vertices[ic].pos);
                const float d=studioRayTriangle(origin,direction,a,b,c);
                if (d<hit.distance) {
                    hit.distance=d;
                    hit.node=int(n);
                    hit.mesh=node.mesh;
                    hit.primitive=int(p);
                    hit.triangle=t/3;
                }
            }
        }
    }
    return hit;
}

struct StudioModelPickInstance {
    std::string id;                  // Studio document identity, not a mesh/node ID
    const render::Model* model = nullptr;
    render::mat4 world{};
};

struct StudioModelInstanceHit {
    std::string id;
    StudioModelSurfaceHit surface;
};

// Query all currently rendered rigid model instances and choose their
// foremost actual triangle. Callers should include ONLY instances also
// submitted to Renderer3D, using identical world transforms.
inline StudioModelInstanceHit studioPickModelInstances(
    const std::vector<StudioModelPickInstance>& instances,
    render::float3 origin,render::float3 direction,
    std::size_t triangleBudgetPerModel=1000000) {
    StudioModelInstanceHit nearest;
    for (const auto& instance:instances) {
        if (!instance.model || instance.id.empty()) continue;
        const auto surface=studioPickModelSurface(*instance.model,instance.world,
            origin,direction,triangleBudgetPerModel);
        if (surface.distance<nearest.surface.distance) {
            nearest.id=instance.id;
            nearest.surface=surface;
        }
    }
    return nearest;
}

// Resolve the visible nearest surface among both real model instances and
// fallback document cubes. Any proxy replaced by a drawn model is excluded:
// otherwise an invisible proxy could steal the click from the real model.
inline std::string studioPickSceneSurfaces(
    const std::vector<StudioModelPickInstance>& renderedModels,
    const std::vector<StudioDocumentProxy>& documentProxies,
    render::float3 origin,render::float3 direction) {
    const auto modelHit=studioPickModelInstances(renderedModels,origin,direction);
    std::vector<StudioDocumentProxy> visibleProxies;
    visibleProxies.reserve(documentProxies.size());
    for (const auto& proxy:documentProxies) {
        bool replaced=false;
        for (const auto& instance:renderedModels)
            if (instance.id==proxy.id) {replaced=true;break;}
        if (!replaced) visibleProxies.push_back(proxy);
    }
    const auto proxyHit=studioPickVisibleProxy(visibleProxies,origin,direction);
    return modelHit.surface.distance<proxyHit.distance ? modelHit.id : proxyHit.id;
}
} // namespace aiverse::studio
