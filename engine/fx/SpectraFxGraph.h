// SpectraFX VFX graph v1: bounded, ordered, renderer-independent cue graph.
// Graph nodes perform presentation-only cue transformations; Emit targets the
// existing effect bindings and ParticleSystem. This is not a GPU shader graph.
// The authoring/editor serialization layer may compile a visual graph into
// these ordered nodes after validating connections.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "SpectraFxCueBridge.h"

namespace aiverse::fx {

enum class SpectraFxNodeKind : uint8_t { Input, Scale, Tint, Emit };

struct SpectraFxNode {
    SpectraFxNodeKind kind = SpectraFxNodeKind::Input;
    // The input is a node index strictly less than this node's index.
    // Input nodes have no parent and ignore 'source'.
    uint32_t source = 0;
    // Scale is multiplied into the input cue's scale.
    float factor = 1;
    // Tint is multiplied into the input cue's RGBA.
    float4 tint{1, 1, 1, 1};
    // Emit: target binding cue ID; 0 means preserve the input cue ID.
    uint32_t cueId = 0;
};

class SpectraFxGraph {
public:
    static constexpr size_t kMaxNodes = 128;
    struct Result {
        size_t emitted = 0;
        size_t rejected = 0;
        bool valid = false;
    };

    // Replacing nodes invalidates any previously compiled/validated graph.
    void setNodes(std::vector<SpectraFxNode> nodes) {
        nodes_ = std::move(nodes);
    }
    const std::vector<SpectraFxNode>& nodes() const { return nodes_; }

    // Validation is pure, with no allocations or side effects.
    // Backward-only references guarantee an acyclic execution order.
    bool validate(std::string* error = nullptr) const {
        auto fail = [&](const char* msg) {
            if (error) *error = msg;
            return false;
        };
        if (nodes_.empty()) return fail("SpectraFX graph has no nodes");
        if (nodes_.size() > kMaxNodes) return fail("SpectraFX graph node limit exceeded");
        if (nodes_[0].kind != SpectraFxNodeKind::Input)
            return fail("SpectraFX graph must begin with Input");
        size_t outputs = 0;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const SpectraFxNode& node = nodes_[i];
            if (node.kind == SpectraFxNodeKind::Input) {
                if (i != 0) return fail("SpectraFX graph has multiple Input nodes");
                continue;
            }
            if (node.source >= i)
                return fail("SpectraFX graph source must precede node");
            switch (node.kind) {
            case SpectraFxNodeKind::Scale:
                if (!(node.factor >= 0 && node.factor <= 1000))
                    return fail("SpectraFX graph scale out of bounds");
                break;
            case SpectraFxNodeKind::Tint:
                if (!(node.tint.x >= 0 && node.tint.x <= 8 &&
                      node.tint.y >= 0 && node.tint.y <= 8 &&
                      node.tint.z >= 0 && node.tint.z <= 8 &&
                      node.tint.w >= 0 && node.tint.w <= 8))
                    return fail("SpectraFX graph tint out of bounds");
                break;
            case SpectraFxNodeKind::Emit:
                ++outputs;
                break;
            case SpectraFxNodeKind::Input:
                break;
            default:
                return fail("SpectraFX graph unknown node kind");
            }
        }
        if (!outputs) return fail("SpectraFX graph has no Emit node");
        if (error) error->clear();
        return true;
    }

    // Outputs are invoked in node order. A keyed (attached) input may have
    // only one Emit to prevent several outputs fighting over the same
    // SpectraFxCommandQueue instance key; unkeyed one-shots may branch freely.
    Result execute(const SpectraFxCue& input, SpectraFxCueBridge& bridge,
                   SpectraFxCommandQueue& queue) const {
        Result result;
        if (!validate()) return result;
        result.valid = true;
        if (input.key) {
            size_t outputs = 0;
            for (const auto& node : nodes_)
                if (node.kind == SpectraFxNodeKind::Emit) ++outputs;
            if (outputs > 1) { result.valid = false; return result; }
        }
        std::vector<SpectraFxCue> values(nodes_.size());
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const auto& node = nodes_[i];
            if (node.kind == SpectraFxNodeKind::Input) {
                values[i] = input;
                continue;
            }
            SpectraFxCue cue = values[node.source];
            switch (node.kind) {
            case SpectraFxNodeKind::Scale:
                cue.scale *= node.factor;
                break;
            case SpectraFxNodeKind::Tint:
                cue.tint = {cue.tint.x * node.tint.x, cue.tint.y * node.tint.y,
                            cue.tint.z * node.tint.z, cue.tint.w * node.tint.w};
                break;
            case SpectraFxNodeKind::Emit:
                if (node.cueId != 0) cue.id = node.cueId;
                if (bridge.dispatch(cue, queue)) ++result.emitted;
                else ++result.rejected;
                break;
            case SpectraFxNodeKind::Input:
                break;
            }
            values[i] = cue;
        }
        return result;
    }

private:
    std::vector<SpectraFxNode> nodes_;
};

} // namespace aiverse::fx
