#include "SparseMorph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace aiverse::vivid {
namespace {
constexpr std::size_t kMaxTargets = 4096;
constexpr std::size_t kMaxDeltas = 16'000'000;
constexpr float kMaxAbsCoordinate = 100'000'000.0f;

bool finiteBounded(float v) noexcept {
    return std::isfinite(v) && std::fabs(v) <= kMaxAbsCoordinate;
}
bool finiteVec(const MorphVec3& v) noexcept {
    return finiteBounded(v[0]) && finiteBounded(v[1]) && finiteBounded(v[2]);
}
bool validDelta(const MorphDelta& d) noexcept {
    return finiteVec(d.position) && finiteVec(d.normal) && finiteVec(d.tangent);
}
bool zeroDelta(const MorphDelta& d) noexcept {
    for (float v : d.position) if (v != 0.0f) return false;
    for (float v : d.normal) if (v != 0.0f) return false;
    for (float v : d.tangent) if (v != 0.0f) return false;
    return true;
}
bool validVertex(const MorphVertex& v) noexcept {
    return finiteVec(v.position) && finiteVec(v.normal) && finiteVec(v.tangent);
}
void normalize(MorphVec3& v) noexcept {
    const double lengthSq = double(v[0]) * v[0] + double(v[1]) * v[1] + double(v[2]) * v[2];
    if (lengthSq <= 1.0e-30) return;
    const float inverse = static_cast<float>(1.0 / std::sqrt(lengthSq));
    for (float& c : v) c *= inverse;
}
} // namespace

SparseMorphLibrary SparseMorphLibrary::compile(std::size_t vertexCount,
                                               std::vector<MorphTargetInput> targets) {
    if (vertexCount > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("VIVID sparse morph: vertex count exceeds 32-bit index");
    if (targets.size() > kMaxTargets)
        throw std::length_error("VIVID sparse morph: target count exceeds budget (4096)");

    SparseMorphLibrary result;
    result.vertices_ = vertexCount;
    result.names_.reserve(targets.size());
    result.ranges_.reserve(targets.size());
    std::unordered_set<std::string> usedNames;
    for (auto& target : targets) {
        if (target.name.empty() || target.name.find('\0') != std::string::npos)
            throw std::invalid_argument("VIVID sparse morph: missing/invalid target name");
        if (!usedNames.insert(target.name).second)
            throw std::invalid_argument("VIVID sparse morph: duplicate target name");

        std::sort(target.deltas.begin(), target.deltas.end(),
                  [](const MorphDelta& a, const MorphDelta& b) { return a.vertex < b.vertex; });
        for (std::size_t i = 0; i < target.deltas.size(); ++i) {
            const MorphDelta& d = target.deltas[i];
            if (d.vertex >= vertexCount || !validDelta(d))
                throw std::invalid_argument("VIVID sparse morph: invalid vertex index or delta");
            if (i && target.deltas[i-1].vertex == d.vertex)
                throw std::invalid_argument("VIVID sparse morph: duplicate vertex in target");
        }
        const auto first = result.deltas_.size();
        for (const MorphDelta& d : target.deltas) {
            if (zeroDelta(d)) continue;
            if (result.deltas_.size() >= kMaxDeltas)
                throw std::length_error("VIVID sparse morph: total deltas exceed budget");
            GpuMorphDelta packed;
            packed.vertex = d.vertex;
            for (std::size_t k = 0; k < 3; ++k) {
                packed.position[k] = d.position[k];
                packed.normal[k] = d.normal[k];
                packed.tangent[k] = d.tangent[k];
            }
            result.deltas_.push_back(packed);
        }
        const std::size_t count = result.deltas_.size() - first;
        result.ranges_.push_back({
            static_cast<std::uint32_t>(first),
            static_cast<std::uint32_t>(count),
            {}});
        result.names_.push_back(std::move(target.name));
    }
    return result;
}

bool SparseMorphLibrary::findTarget(std::string_view name, std::size_t& index) const noexcept {
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) { index = i; return true; }
    }
    return false;
}

bool SparseMorphLibrary::evaluate(std::span<const MorphVertex> base,
                                   std::span<const float> weights,
                                   std::span<MorphVertex> output,
                                   bool normalizeDirections) const noexcept {
    if (base.size() != vertices_ || output.size() != vertices_ || weights.size() != targets())
        return false;
    // Preflight validation: malformed inputs must never produce partial results.
    for (float w : weights) if (!std::isfinite(w) || w < 0.0f || w > 1.0f) return false;
    for (const MorphVertex& v : base) if (!validVertex(v)) return false;

    if (base.data() != output.data())
        std::copy(base.begin(), base.end(), output.begin());

    // Accumulate independent semantic targets without overwriting unaffected
    // vertices. The order is stable and is shared by the GPU ABI's range table.
    for (std::size_t target = 0; target < targets(); ++target) {
        const float weight = weights[target];
        if (weight == 0.0f) continue;
        const GpuMorphRange range = ranges_[target];
        for (std::uint32_t i = 0; i < range.count; ++i) {
            const auto& delta = deltas_[std::size_t(range.first) + i];
            MorphVertex& v = output[delta.vertex];
            for (std::size_t axis = 0; axis < 3; ++axis) {
                v.position[axis] += delta.position[axis] * weight;
                v.normal[axis] += delta.normal[axis] * weight;
                v.tangent[axis] += delta.tangent[axis] * weight;
            }
        }
    }
    if (normalizeDirections) {
        for (MorphVertex& v : output) {
            normalize(v.normal);
            normalize(v.tangent);
        }
    }
    return true;
}

} // namespace aiverse::vivid
