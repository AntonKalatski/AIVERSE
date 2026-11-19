// VIVID shared full-model CSR for base facial mesh and authored LOD meshes.
// A single GpuActorStream dispatch deforms the existing full ModelVertex
// array, so Renderer3D's already implemented LOD selection remains valid.
#pragma once
#include "SparseMorph.h"
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::vivid {
class MorphLodGpuAuthoring {
public:
    // compactLod targets correspond to sorted model-relative target IDs.
    // The names and order must match the original full-model library.
    // Fail closed if LOD indices overlap existing source-shape deltas.
    static SparseMorphLibrary combine(
        const SparseMorphLibrary& full,
        const SparseMorphLibrary& compactLod,
        std::span<const std::uint32_t> targetVertexIds) {
        if(full.vertices()==0 || compactLod.targets()!=full.targets() ||
           compactLod.vertices()!=targetVertexIds.size() ||
           compactLod.vertices()==0)
            throw std::invalid_argument("VIVID LOD GPU: target topology/count mismatch");
        for(std::size_t i=0;i<targetVertexIds.size();++i) {
            if(targetVertexIds[i]>=full.vertices() ||
               (i && targetVertexIds[i]<=targetVertexIds[i-1]))
                throw std::invalid_argument("VIVID LOD GPU: target IDs outside or unordered");
        }
        std::vector<MorphTargetInput> targets;
        targets.reserve(full.targets());
        for(std::size_t target=0;target<full.targets();++target) {
            if(full.targetName(target)!=compactLod.targetName(target))
                throw std::invalid_argument("VIVID LOD GPU: target name/order mismatch");
            MorphTargetInput authoring;
            authoring.name=std::string(full.targetName(target));
            const auto baseRange=full.gpuRanges()[target];
            const auto lodRange=compactLod.gpuRanges()[target];
            authoring.deltas.reserve(std::size_t(baseRange.count)+lodRange.count);
            auto unpack=[&](const GpuMorphDelta& input,std::uint32_t id) {
                MorphDelta out;
                out.vertex=id;
                for(std::size_t a=0;a<3;++a) {
                    out.position[a]=input.position[a];
                    out.normal[a]=input.normal[a];
                    out.tangent[a]=input.tangent[a];
                }
                return out;
            };
            for(std::uint32_t i=0;i<baseRange.count;++i) {
                const auto& gpu=full.gpuDeltas()[baseRange.first+i];
                authoring.deltas.push_back(unpack(gpu,gpu.vertex));
            }
            for(std::uint32_t i=0;i<lodRange.count;++i) {
                const auto& gpu=compactLod.gpuDeltas()[lodRange.first+i];
                if(gpu.vertex>=targetVertexIds.size())
                    throw std::invalid_argument("VIVID LOD GPU: compact vertex outside mapping");
                const auto global=targetVertexIds[gpu.vertex];
                for(const auto& existing:authoring.deltas)
                    if(existing.vertex==global)
                        throw std::invalid_argument("VIVID LOD GPU: source and LOD facial vertices overlap");
                authoring.deltas.push_back(unpack(gpu,global));
            }
            targets.push_back(std::move(authoring));
        }
        return SparseMorphLibrary::compile(full.vertices(),std::move(targets));
    }
};
} // namespace aiverse::vivid
