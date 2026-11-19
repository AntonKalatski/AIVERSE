#include "StrataView.h"
#include "Geometry.h"

#include <cmath>
#include <limits>
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
} // namespace

bool planView(const Asset& source, const ViewConfig& config,
              ViewPlan& out, std::string* error) {
    if (error) error->clear();
    if (config.width <= 0 || config.height <= 0 ||
        config.width > 32768 || config.height > 32768 ||
        !std::isfinite(config.errorThresholdPixels) ||
        config.errorThresholdPixels <= 0 || config.maxSelectedNodes == 0)
        return fail(error, "invalid Strata view dimensions or LOD policy");

    for (float value : config.objectToClip.m)
        if (!std::isfinite(value))
            return fail(error, "nonfinite Strata object-to-clip matrix");

    if (!validate(source.nodes, source.roots) ||
        source.bounds.size() != source.nodes.size())
        return fail(error, "invalid Strata cluster topology/bounds count");

    // Check all bound ranges, including currently nonresident nodes, so the
    // result never refers to an out-of-bounds or malformed source index stream.
    for (size_t i = 0; i < source.nodes.size(); ++i) {
        const auto& node = source.nodes[i];
        if (!validBounds(source.bounds[i]) ||
            node.firstIndex > source.indices.size() ||
            node.indexCount > source.indices.size() - node.firstIndex)
            return fail(error, "invalid Strata cluster bounds/index range");
    }

    std::vector<float> errors(source.nodes.size());
    for (size_t i = 0; i < source.nodes.size(); ++i) {
        const auto& node = source.nodes[i];
        const auto& b = source.bounds[i];
        // geometryErrorPixels is conservative near the projection plane and
        // under arbitrary affine transforms (including non-uniform scale).
        errors[i] = geometryErrorPixels(config.objectToClip,
            low(b), high(b), node.geometricError, config.width, config.height,
            config.clipDepthZeroToOne);
        if (!std::isfinite(errors[i]) || errors[i] < 0)
            return fail(error, "nonfinite projected Strata error");
    }

    SelectionInput input;
    input.errorPixels = errors.data();
    input.errorCount = uint32_t(errors.size());
    input.thresholdPixels = config.errorThresholdPixels;
    input.maxVisibleNodes = config.maxSelectedNodes;
    SelectionOutput selection;
    if (!select(source.nodes, source.roots, input, selection))
        return fail(error, "Strata nodes not resident or selection budget exhausted");

    ViewPlan planned;
    planned.testedNodes = uint32_t(selection.nodes.size());
    planned.missingRefinements = selection.missingRefinements;
    planned.budgetExceeded = selection.budgetExceeded;
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
