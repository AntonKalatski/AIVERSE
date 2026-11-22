#include "StrataView.h"
#include "Geometry.h"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace aiverse::render::strata {
namespace {
bool fail(std::string* error, const char* reason) {
    if (error) *error = reason;
    return false;
}
bool validBounds(const ClusterBounds& b) {
    for (uint32_t axis = 0; axis != 3; ++axis)
        if (!std::isfinite(b.min[axis]) || !std::isfinite(b.max[axis]) ||
            b.min[axis] > b.max[axis])
            return false;
    return true;
}
float3 low(const ClusterBounds& b) {
    return {b.min[0], b.min[1], b.min[2]};
}
float3 high(const ClusterBounds& b) {
    return {b.max[0], b.max[1], b.max[2]};
}

// True when all 8 corners lie outside the same clip plane.
bool outsideFrustum(const mat4& clip, float3 mn, float3 mx, bool zeroToOne) {
    int out[6] = {};
    for (int c = 0; c < 8; ++c) {
        const float4 p = clip * float4{c & 1 ? mx.x : mn.x, c & 2 ? mx.y : mn.y, c & 4 ? mx.z : mn.z, 1};
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(p.w)) return false;
        out[0] += p.x < -p.w; out[1] += p.x > p.w;
        out[2] += p.y < -p.w; out[3] += p.y > p.w;
        out[4] += p.z < (zeroToOne ? 0.f : -p.w); out[5] += p.z > p.w;
    }
    for (int k = 0; k < 6; ++k) if (out[k] == 8) return true;
    return false;
}
} // namespace

bool planView(const Asset& source, const ViewConfig& config,
              ViewPlan& out, std::string* error,
              const std::vector<uint8_t>* residentMask) {
    if (error) error->clear();
    if (config.width <= 0 || config.height <= 0 ||
        config.width > 32768 || config.height > 32768 ||
        !std::isfinite(config.errorThresholdPixels) ||
        config.errorThresholdPixels <= 0 || config.maxSelectedNodes == 0 ||
        !std::isfinite(config.hysteresisFraction) ||
        config.hysteresisFraction < 0.f ||
        config.hysteresisFraction > .45f ||
        (config.previousRefined &&
         config.previousRefined->size()!=source.nodes.size()))
        return fail(error, "invalid Strata view dimensions or LOD policy");

    for (float value : config.objectToClip.m)
        if (!std::isfinite(value))
            return fail(error, "nonfinite Strata object-to-clip matrix");

    // Topology and range validation is O(all nodes): staged sources were
    // validated once at staging and pass trustedSource.
    const bool known = config.trustedSource;
    if (!known) {
        if (!validate(source.nodes, source.roots) ||
            source.bounds.size() != source.nodes.size())
            return fail(error, "invalid Strata cluster topology/bounds count");

        // Disk-backed sources may have released their redundant CPU index
        // payload after root GPU validation. Ranges are still checked against
        // the immutable logical count recorded before the release.
        const size_t logicalIndices=source.logicalIndexCount();
        for (size_t i = 0; i < source.nodes.size(); ++i) {
            const auto& node = source.nodes[i];
            if (!validBounds(source.bounds[i]) ||
                node.firstIndex > logicalIndices ||
                node.indexCount > logicalIndices - node.firstIndex)
                return fail(error, "invalid Strata cluster bounds/index range");
        }
    }

    // Projected error only for the nodes the selection can actually reach:
    // walk down from the roots and descend into a node's children only when
    // that node may be refined (its error exceeds the lowest threshold the
    // selection can use, i.e. with hysteresis). Unreached nodes keep error 0
    // and are never visited by select(), because their parent is not
    // refined. A far object touches a handful of nodes instead of all of
    // them (a 1M-triangle source has ~17k), which was the CPU cost per frame.
    std::vector<float> errors(source.nodes.size(), 0.f);
    {
        const float lowest = config.errorThresholdPixels * (1.f - config.hysteresisFraction);
        std::vector<uint32_t> walk(source.roots.begin(), source.roots.end());
        while (!walk.empty()) {
            const uint32_t i = walk.back();
            walk.pop_back();
            const auto& node = source.nodes[i];
            const auto& b = source.bounds[i];
            if (config.coarsenOutsideFrustum && outsideFrustum(config.objectToClip, low(b), high(b), config.clipDepthZeroToOne)) {
                errors[i] = 0.f;
                continue;
            }
            errors[i] = geometryErrorPixels(config.objectToClip,
                low(b), high(b), node.geometricError, config.width, config.height,
                config.clipDepthZeroToOne);
            if (!std::isfinite(errors[i]) || errors[i] < 0)
                return fail(error, "nonfinite projected Strata error");
            if (node.childCount && errors[i] > lowest)
                for (uint32_t k = 0; k < node.childCount; ++k)
                    walk.push_back(node.firstChild + k);
        }
    }

    // Physical residency does not modify source/import assets. This keeps
    // the immutable CPU hierarchy available for future streaming requests.
    std::vector<ClusterNode> maskedNodes;
    if (residentMask) {
        if (residentMask->size() != source.nodes.size())
            return fail(error, "incorrect Strata physical residency mask size");
        maskedNodes = source.nodes;
        for (size_t i = 0; i < maskedNodes.size(); ++i)
            maskedNodes[i].resident =
                maskedNodes[i].resident && (*residentMask)[i] != 0;
    }
    const auto& selectionNodes = residentMask ? maskedNodes : source.nodes;
    SelectionInput input;
    input.errorPixels = errors.data();
    input.errorCount = uint32_t(errors.size());
    input.thresholdPixels = config.errorThresholdPixels;
    input.topologyValidated = true; // validated above, or by staging (trustedSource)
    input.maxVisibleNodes = config.maxSelectedNodes;
    input.hysteresisFraction=config.hysteresisFraction;
    if(config.previousRefined){
        input.previousRefined=config.previousRefined->data();
        input.previousRefinedCount=uint32_t(config.previousRefined->size());
    }
    SelectionOutput selection;
    if (!select(selectionNodes, source.roots, input, selection))
        return fail(error, "Strata nodes not resident or selection budget exhausted");

    ViewPlan planned;
    planned.testedNodes = uint32_t(selection.nodes.size());
    planned.missingRefinements = selection.missingRefinements;
    planned.budgetExceeded = selection.budgetExceeded;
    planned.refinedNodes=std::move(selection.refinedNodes);
    planned.ranges.reserve(selection.nodes.size());

    for (uint32_t index : selection.nodes) {
        const auto& node = source.nodes[index];
        const auto& b = source.bounds[index];
        if (config.frustumCull &&
            !geometryBoxVisible(config.objectToClip, low(b), high(b))) {
            ++planned.culledNodes;
            continue;
        }
        planned.ranges.push_back({
            index, node.firstIndex, node.indexCount, node.materialGroup
        });
        planned.triangles += node.indexCount / 3;
    }
    out = std::move(planned);
    return true;
}
} // namespace aiverse::render::strata
