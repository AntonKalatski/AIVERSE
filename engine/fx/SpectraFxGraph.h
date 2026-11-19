// SpectraFX graph v2: bounded presentation-cue DAG with typed parameters.
// Executes on the host's presentation thread, never during authoritative ECS
// simulation. The ParticleSystem remains the renderer/simulation backend.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SpectraFxCueBridge.h"

namespace aiverse::fx {

enum class SpectraFxNodeKind : uint8_t {
    Input, Scale, Tint, Emit,
    ParameterScale, ParameterTint, Offset, Gate, EventFilter
};

struct SpectraFxNode {
    SpectraFxNodeKind kind = SpectraFxNodeKind::Input;
    // One source, strictly preceding this node. The topology is a DAG:
    // multiple nodes may reference the same previous node to branch.
    uint32_t source = 0;
    float factor = 1;
    float4 tint{1, 1, 1, 1};
    // Emit overrides the cue's effect binding ID. EventFilter matches it.
    uint32_t cueId = 0;
    // Named typed parameter for ParameterScale, ParameterTint and Gate.
    std::string parameter;
    float3 offset{};
};

struct SpectraFxGraphParameters {
    std::unordered_map<std::string, float> scalars;
    std::unordered_map<std::string, float4> colors;
    std::unordered_map<std::string, bool> flags;

    void setScalar(std::string name, float value) { scalars[std::move(name)] = value; }
    void setColor(std::string name, float4 value) { colors[std::move(name)] = value; }
    void setFlag(std::string name, bool value) { flags[std::move(name)] = value; }
};

class SpectraFxGraph {
public:
    static constexpr size_t kMaxNodes = 128;
    struct Result {
        size_t emitted = 0;
        size_t rejected = 0;
        bool valid = false;
    };

    void setNodes(std::vector<SpectraFxNode> nodes) { nodes_ = std::move(nodes); }
    const std::vector<SpectraFxNode>& nodes() const { return nodes_; }

    bool validate(std::string* error = nullptr) const {
        const auto fail = [&](const char* message) {
            if (error) *error = message;
            return false;
        };
        if (nodes_.empty()) return fail("SpectraFX graph has no nodes");
        if (nodes_.size() > kMaxNodes) return fail("SpectraFX graph node limit exceeded");
        if (nodes_[0].kind != SpectraFxNodeKind::Input)
            return fail("SpectraFX graph must begin with Input");

        size_t outputs = 0;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const auto& node = nodes_[i];
            if (node.kind == SpectraFxNodeKind::Input) {
                if (i != 0) return fail("SpectraFX graph has multiple Input nodes");
                continue;
            }
            if (node.source >= i)
                return fail("SpectraFX graph source must precede node");
            switch (node.kind) {
            case SpectraFxNodeKind::Scale:
                if (!std::isfinite(node.factor) || node.factor < 0 || node.factor > 1000)
                    return fail("SpectraFX graph scale out of bounds");
                break;
            case SpectraFxNodeKind::Tint:
                if (!validColor(node.tint))
                    return fail("SpectraFX graph tint out of bounds");
                break;
            case SpectraFxNodeKind::Offset:
                if (!std::isfinite(node.offset.x) || !std::isfinite(node.offset.y) ||
                    !std::isfinite(node.offset.z))
                    return fail("SpectraFX graph offset is not finite");
                break;
            case SpectraFxNodeKind::ParameterScale:
            case SpectraFxNodeKind::ParameterTint:
            case SpectraFxNodeKind::Gate:
                if (node.parameter.empty() || node.parameter.size() > 128)
                    return fail("SpectraFX graph parameter name invalid");
                break;
            case SpectraFxNodeKind::EventFilter:
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

    // Old call sites keep their behavior when the parameter block is absent.
    Result execute(const SpectraFxCue& input, SpectraFxCueBridge& bridge,
                   SpectraFxCommandQueue& queue) const {
        return execute(input, {}, bridge, queue);
    }

    Result execute(const SpectraFxCue& input, const SpectraFxGraphParameters& params,
                   SpectraFxCueBridge& bridge, SpectraFxCommandQueue& queue) const {
        Result result;
        if (!validate()) return result;
        // Keyed effects own a single lifetime. Multiple emitters with the
        // same instance key would replace one another in the command queue.
        if (input.key) {
            size_t emitCount = 0;
            for (const auto& node : nodes_)
                if (node.kind == SpectraFxNodeKind::Emit) ++emitCount;
            if (emitCount > 1) return result;
        }
        result.valid = true;
        struct Value { SpectraFxCue cue; bool active = false; };
        std::vector<Value> values(nodes_.size());
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const auto& node = nodes_[i];
            if (node.kind == SpectraFxNodeKind::Input) {
                values[i] = {input, true};
                continue;
            }
            Value v = values[node.source];
            if (!v.active) { values[i] = v; continue; }
            switch (node.kind) {
            case SpectraFxNodeKind::Scale:
                v.cue.scale *= node.factor;
                break;
            case SpectraFxNodeKind::Tint:
                v.cue.tint = mul(v.cue.tint, node.tint);
                break;
            case SpectraFxNodeKind::ParameterScale: {
                const auto it = params.scalars.find(node.parameter);
                if (it == params.scalars.end() || !std::isfinite(it->second) ||
                    it->second < 0 || it->second > 1000) v.active = false;
                else v.cue.scale *= it->second;
                break;
            }
            case SpectraFxNodeKind::ParameterTint: {
                const auto it = params.colors.find(node.parameter);
                if (it == params.colors.end() || !validColor(it->second)) v.active = false;
                else v.cue.tint = mul(v.cue.tint, it->second);
                break;
            }
            case SpectraFxNodeKind::Offset:
                v.cue.position = {v.cue.position.x + node.offset.x,
                                  v.cue.position.y + node.offset.y,
                                  v.cue.position.z + node.offset.z};
                break;
            case SpectraFxNodeKind::Gate: {
                const auto it = params.flags.find(node.parameter);
                v.active = (it != params.flags.end() && it->second);
                break;
            }
            case SpectraFxNodeKind::EventFilter:
                v.active = (v.cue.id == node.cueId);
                break;
            case SpectraFxNodeKind::Emit:
                if (node.cueId) v.cue.id = node.cueId;
                if (bridge.dispatch(v.cue, queue)) ++result.emitted;
                else ++result.rejected;
                break;
            case SpectraFxNodeKind::Input:
                break;
            }
            values[i] = v;
        }
        return result;
    }

private:
    static bool validColor(float4 c) {
        return std::isfinite(c.x) && std::isfinite(c.y) &&
               std::isfinite(c.z) && std::isfinite(c.w) &&
               c.x >= 0 && c.x <= 8 && c.y >= 0 && c.y <= 8 &&
               c.z >= 0 && c.z <= 8 && c.w >= 0 && c.w <= 8;
    }
    static float4 mul(float4 a, float4 b) {
        return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w};
    }
    std::vector<SpectraFxNode> nodes_;
};

} // namespace aiverse::fx
