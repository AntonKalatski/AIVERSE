// Stage 7AT — real Renderer3D 60-byte GPU ModelVertex source for VIVID ID.
// Metal-only compute expansion borrows the actor's current-frame vertexOverride.
// It does not own, read back or duplicate the GPU vertex buffer. CPU-supplied
// topology, palette and pose metadata exactly specify which source vertices
// and MotionWeave joint transforms to evaluate in the GPU compute kernel.
// Rendering still validates the source/LOD with Renderer3D provenance.
#pragma once
#include "SculptGpuPickShaders.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace aiverse::vivid {

struct SculptGpuSourceIndex {
    std::uint32_t vertex=0;
    std::uint32_t triangleId=0; // same little-endian RGBA word as packSculptTriangleId
};
static_assert(sizeof(SculptGpuSourceIndex)==8);

struct SculptGpuSkinInfluence {
    std::uint32_t joints[4]{};
    float weights[4]{};
};
static_assert(sizeof(SculptGpuSkinInfluence)==32);

// Layout matches the 64-byte float4x4 following four uint32 in MSL.
struct alignas(16) SculptGpuSourceParams {
    std::uint32_t count=0,vertices=0,skinned=0,jointMatrices=0;
    render::mat4 rigidWorld{};
};
static_assert(sizeof(SculptGpuSourceParams)==80);

struct SculptGpuVertexSource {
    gfx::BufferHandle actualVertexOverride{}; // BORROWED current-frame 60B buffer
    std::uint32_t sourceVertexCount=0;
    SculptGpuSourceParams params{};
    std::vector<SculptGpuSourceIndex> indices;
    std::vector<SculptGpuSkinInfluence> influences;
    std::vector<render::mat4> jointMatrices;
};

// CPU derives ONLY topology/skin palette and matrices. GPU vertex positions
// come from the exact 60B stream Renderer3D uses for the colour draw.
// Whole-primitive source-ID validity and material semantics must be verified
// by VIVID's existing source-map and renderer-provenance gates.
inline bool prepareSculptGpuVertexSource(
        const render::Model& model,
        std::size_t meshId,
        std::span<const render::mat4> nodeWorld,
        const render::mat4& selectedWorld,
        gfx::BufferHandle currentGpuVertexOverride,
        std::uint32_t overrideVertexCount,
        std::span<const std::uint32_t> authorIds,
        std::size_t authorSourceCount,
        SculptGpuVertexSource& output) {
    using namespace render;
    if(!currentGpuVertexOverride.valid() ||
       overrideVertexCount!=model.vertices.size() ||
       model.vertices.empty() || model.vertices.size()>1000000u ||
       !authorSourceCount || authorSourceCount>model.vertices.size() ||
       meshId>=model.meshes.size() ||
       nodeWorld.size()!=model.nodes.size() ||
       (!authorIds.empty() && authorIds.size()!=model.vertices.size()))
        return false;
    for(float value:selectedWorld.m)
        if(!std::isfinite(value))return false;
    for(const auto& matrix:nodeWorld)
        for(float value:matrix.m)if(!std::isfinite(value))return false;
    const std::size_t notSet=std::numeric_limits<std::size_t>::max();
    std::size_t owner=notSet;
    for(std::size_t n=0;n<model.nodes.size();++n) {
        if(model.nodes[n].mesh!=int(meshId))continue;
        if(owner!=notSet)return false; // ambiguous GPU source mesh instance
        owner=n;
    }
    if(owner==notSet)return false;
    const auto skinIndex=model.nodes[owner].skin;
    const Skin* skin=nullptr;
    if(skinIndex>=0) {
        if(std::size_t(skinIndex)>=model.skins.size())return false;
        skin=&model.skins[std::size_t(skinIndex)];
        if(skin->joints.empty() || skin->joints.size()!=skin->inverseBind.size() ||
           skin->joints.size()>1024u)return false;
    }
    SculptGpuVertexSource candidate;
    candidate.actualVertexOverride=currentGpuVertexOverride;
    candidate.sourceVertexCount=overrideVertexCount;
    candidate.params.vertices=overrideVertexCount;
    candidate.params.skinned=skin?1u:0u;
    candidate.params.rigidWorld=selectedWorld;
    if(skin) {
        candidate.jointMatrices.reserve(skin->joints.size());
        for(std::size_t j=0;j<skin->joints.size();++j) {
            const auto joint=skin->joints[j];
            if(joint<0 || std::size_t(joint)>=nodeWorld.size())return false;
            const auto matrix=selectedWorld*
                nodeWorld[std::size_t(joint)]*skin->inverseBind[j];
            for(float value:matrix.m)
                if(!std::isfinite(value))return false;
            candidate.jointMatrices.push_back(matrix);
        }
        candidate.params.jointMatrices=
            std::uint32_t(candidate.jointMatrices.size());
        candidate.influences.resize(model.vertices.size());
    }
    std::vector<std::size_t> firstPalette;
    if(skin)firstPalette.assign(model.vertices.size(),notSet);
    std::size_t count=0;
    for(const auto& primitive:model.meshes[meshId].primitives) {
        const auto first=std::size_t(primitive.firstIndex);
        if(first>model.indices.size() ||
           primitive.indexCount>model.indices.size()-first ||
           primitive.indexCount%3u)return false;
        count+=primitive.indexCount/3u;
        if(count>1000000u)return false;
        // Only fully opaque conventional source faces use GPU expansion.
        // The existing CPU-fed ID path handles alpha-tested selection.
        const Material* material=nullptr;
        if(!model.materials.empty()) {
            if(primitive.material<0 ||
               std::size_t(primitive.material)>=model.materials.size())
                return false;
            material=&model.materials[std::size_t(primitive.material)];
        }
        if(material && (material->alpha!=AlphaMode::Opaque ||
                        material->transparent || material->baseColor.w<0.999f ||
                        material->shaderGraph))
            return false;
        if(skin && primitive.jointPalette.empty())return false;
        for(std::size_t i=first;i<first+primitive.indexCount;++i) {
            const auto id=model.indices[i];
            if(id>=model.vertices.size())return false;
            if(!skin)continue;
            if(firstPalette[id]==notSet) {
                firstPalette[id]=std::size_t(&primitive-
                    model.meshes[meshId].primitives.data());
                const auto& vertex=model.vertices[id];
                float weightSum=0.f;
                auto& influence=candidate.influences[id];
                for(int k=0;k<4;++k) {
                    const auto weight=vertex.weights[k];
                    if(!weight)continue;
                    const std::size_t local=vertex.joints[k];
                    if(local>=primitive.jointPalette.size())return false;
                    const auto joint=primitive.jointPalette[local];
                    if(std::size_t(joint)>=skin->joints.size())return false;
                    influence.joints[k]=joint;
                    influence.weights[k]=float(weight)/255.f;
                    weightSum+=influence.weights[k];
                }
                if(!(weightSum>0.f) || !std::isfinite(weightSum))return false;
                for(auto& weight:influence.weights)weight/=weightSum;
            }else if(model.meshes[meshId].primitives[firstPalette[id]]
                     .jointPalette!=primitive.jointPalette)return false;
        }
    }
    if(!count)return false;
    candidate.indices.reserve(count*3u);
    std::uint32_t ordinal=0;
    for(const auto& primitive:model.meshes[meshId].primitives) {
        for(std::size_t i=primitive.firstIndex;
            i<std::size_t(primitive.firstIndex)+primitive.indexCount;i+=3u) {
            const std::array<std::uint32_t,3> ids{
                model.indices[i],model.indices[i+1],model.indices[i+2]};
            bool editable=true;
            for(const auto id:ids)
                if((authorIds.empty()?id:authorIds[id])>=authorSourceCount)
                    editable=false;
            const auto packed=editable?ordinal+1u:0u;
            for(const auto id:ids)candidate.indices.push_back({id,packed});
            ++ordinal;
        }
    }
    candidate.params.count=std::uint32_t(candidate.indices.size());
    output=std::move(candidate);
    return true;
}

// The first 3 uint32 per source ModelVertex are exact IEEE754 position,
// preserved by GpuMorphMeshStream's 48B→60B packing kernel. A CPU-built
// position stream is NOT read by the GPU expansion: only the already
// published current-frame ModelDraw::vertexOverride is sampled.
inline constexpr const char* kSculptGpuSourceExpandMSL=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Input { uint vertex; uint triangleId; };
struct Skin { uint4 joints; float4 weights; };
struct Output { packed_float3 world; uchar4 rgba; };
struct Params {
    uint count, vertices, skinned, jointMatrices;
    float4x4 rigidWorld;
};
kernel void cs_main(device const uint* source [[buffer(0)]],
                    device const Input* triangles [[buffer(1)]],
                    device const Skin* influences [[buffer(2)]],
                    device const float4x4* joints [[buffer(3)]],
                    device Output* output [[buffer(4)]],
                    constant Params& params [[buffer(8)]],
                    uint thread [[thread_position_in_grid]]) {
    if(thread>=params.count)return;
    const Input request=triangles[thread];
    if(request.vertex>=params.vertices)return;
    const uint offset=request.vertex*15u;
    const float4 bind=float4(as_type<float>(source[offset]),
                             as_type<float>(source[offset+1u]),
                             as_type<float>(source[offset+2u]),1.0);
    float4 world;
    if(params.skinned!=0u) {
        const Skin influence=influences[request.vertex];
        world=float4(0.0);
        for(uint k=0u;k<4u;++k) {
            const float weight=influence.weights[k];
            if(weight>0.0 && influence.joints[k]<params.jointMatrices)
                world+=(joints[influence.joints[k]]*bind)*weight;
        }
    }else{
        world=params.rigidWorld*bind;
    }
    const uint id=request.triangleId;
    output[thread].world=packed_float3(world.xyz);
    output[thread].rgba=uchar4(uchar(id&255u),
                               uchar((id>>8u)&255u),
                               uchar((id>>16u)&255u),
                               uchar((id>>24u)&255u));
}
)MSL";
} // namespace aiverse::vivid
