// VIVID pre-upload palette seam separation with full-model morph replication.
// Source vertex IDs never move. Clones append at the end of the model stream.
// This bounded authoring operation is intentionally independent of all genres.
#pragma once
#include "SkinSeamAnalysis.h"
#include "SparseMorph.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::vivid {
class SkinSeamSplit final {
public:
    struct Clone {std::uint32_t source=0,target=0;std::size_t mesh=0,primitive=0;};
    struct Result {
        std::vector<Clone> clones;
        std::size_t primitives=0;
    };
    // Guarantees no writes to source Model or source facial sparse data
    // until the whole replacement has been compiled successfully.
    static Result apply(render::Model& model,SparseMorphLibrary& morph,
                        std::size_t maxClones=65536) {
        if(model.vb.valid() || model.ib.valid() || model.geometryPoolPage ||
           model.geometry || morph.vertices()!=model.vertices.size())
            throw std::invalid_argument("VIVID seam split: requires private pre-upload Model/morph");
        const auto conflicts=SkinSeamAnalysis::detect(model,maxClones);
        if(conflicts.empty())return {};
        // Stage full Model in scratch. A failed allocation or invalid
        // primitive can never damage the currently visible imported Model.
        auto vertices=model.vertices;
        auto indices=model.indices;
        auto meshes=model.meshes;
        struct Key {
            std::size_t mesh=0,primitive=0;
            bool operator<(const Key& b)const noexcept {
                return mesh!=b.mesh?mesh<b.mesh:primitive<b.primitive;
            }
        };
        std::map<Key,std::vector<std::uint32_t>> sourceByPrimitive;
        for(const auto& conflict:conflicts) {
            const Key key{conflict.otherMesh,conflict.otherPrimitive};
            if(key.mesh>=meshes.size() ||
               key.primitive>=meshes[key.mesh].primitives.size())
                throw std::invalid_argument("VIVID seam split: conflict primitive missing");
            sourceByPrimitive.try_emplace(key);
        }
        Result result;
        for(auto& [key,unused]:sourceByPrimitive) {
            (void)unused;
            auto& p=meshes[key.mesh].primitives[key.primitive];
            // Legacy baked morph atlas / mesh shadow and material batches
            // refer to primitive-relative vertex ranges and must be rebuilt
            // separately. Fail closed instead of publishing stale draws.
            if(!p.morphDeltas.empty() || p.morphTargets!=0 ||
               meshes[key.mesh].shadowBatch.indexCount ||
               !meshes[key.mesh].materialBatches.empty())
                throw std::invalid_argument(
                    "VIVID seam split: legacy morph/batched primitive requires rebake");
            const auto first=std::size_t(p.firstIndex);
            if(first>indices.size() ||
               p.indexCount>indices.size()-first ||
               !p.indexCount || vertices.size()>std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("VIVID seam split: invalid primitive range");
            // Clone the ENTIRE primitive into its own contiguous vertex
            // interval. The old source vertex stream is never reindexed.
            std::map<std::uint32_t,std::uint32_t> remap;
            std::vector<std::uint32_t> ordered;
            for(std::size_t k=first;k<first+p.indexCount;++k) {
                const auto source=indices[k];
                if(source>=model.vertices.size())
                    throw std::invalid_argument("VIVID seam split: primitive index out of range");
                ordered.push_back(source);
            }
            std::sort(ordered.begin(),ordered.end());
            ordered.erase(std::unique(ordered.begin(),ordered.end()),ordered.end());
            if(ordered.size()>maxClones-result.clones.size() ||
               ordered.size()>std::numeric_limits<std::uint32_t>::max()-vertices.size())
                throw std::invalid_argument("VIVID seam split: clone budget exceeded");
            const auto base=std::uint32_t(vertices.size());
            for(auto source:ordered) {
                const auto target=std::uint32_t(vertices.size());
                vertices.push_back(model.vertices[source]);
                remap.emplace(source,target);
                result.clones.push_back({source,target,key.mesh,key.primitive});
            }
            for(std::size_t k=first;k<first+p.indexCount;++k)
                indices[k]=remap.at(indices[k]);
            p.firstVertex=base;
            p.vertexCount=std::uint32_t(ordered.size());
            ++result.primitives;
        }
        // Sparse facial targets are keyed by model-global vertex ID. Clone
        // every nonzero delta on a duplicated source so both primitive
        // halves retain exactly the same facial expression response.
        std::vector<MorphTargetInput> targets;
        targets.reserve(morph.targets());
        for(std::size_t i=0;i<morph.targets();++i) {
            MorphTargetInput target;
            target.name=std::string(morph.targetName(i));
            const auto range=morph.gpuRanges()[i];
            for(std::uint32_t j=0;j<range.count;++j) {
                const auto& gpu=morph.gpuDeltas()[range.first+j];
                MorphDelta delta;
                delta.vertex=gpu.vertex;
                for(int axis=0;axis<3;++axis) {
                    delta.position[axis]=gpu.position[axis];
                    delta.normal[axis]=gpu.normal[axis];
                    delta.tangent[axis]=gpu.tangent[axis];
                }
                target.deltas.push_back(delta);
            }
            const auto original=target.deltas;
            for(const auto& clone:result.clones)
                for(const auto& entry:original)
                    if(entry.vertex==clone.source) {
                        auto copied=entry;
                        copied.vertex=clone.target;
                        target.deltas.push_back(copied);
                    }
            targets.push_back(std::move(target));
        }
        auto expanded=SparseMorphLibrary::compile(vertices.size(),std::move(targets));
        // All may-throw operations completed. The four swaps publish
        // geometry, primitive ranges and morph CSR as one logical update.
        model.vertices.swap(vertices);
        model.indices.swap(indices);
        model.meshes.swap(meshes);
        morph=std::move(expanded);
        return result;
    }
};
} // namespace aiverse::vivid
