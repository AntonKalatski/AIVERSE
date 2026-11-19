#include "Renderer3D.h"
#include "StrataRenderRouting.h"

#include <cmath>
#include <string>

namespace aiverse::render {

// Diagnostic bridge: this is called by GeometryScene BEFORE its ordinary
// Renderer3D::drawModel call, never instead of it. The public route gate
// remains Legacy while real GPU / pixel parity are unverified.
void Renderer3D::previewStrataModel(const ModelDraw& draw,
                                   const strata::ModelAsset& staged,
                                   const strata::GpuMetadataScene& pooled,
                                   const std::string& sourceKey) {
    ++strataPreviewStats_.modelDraws;
    const Model* model = draw.model;
    if (!dev_ || !model || model->order.empty() ||
        model->bindWorld.size() != model->nodes.size() ||
        draw.nodeWorld || draw.nodeMaterial || draw.materials ||
        draw.pose || draw.baked || draw.variant >= 0 ||
        draw.tint.w < 0.99f || draw.dissolve != 0.f ||
        draw.outline.w != 0.f ||
        (draw.flags & (kDrawTransparent | kDrawFadeDepth |
                       kDrawActorReveal | kDrawWaterSurface |
                       kDrawNereidCascade))) {
        ++strataPreviewStats_.skippedUnsafeDraws;
        return;
    }

    for (int id : model->order) {
        if (id < 0 || size_t(id) >= model->nodes.size()) {
            ++strataPreviewStats_.skippedUnsafeDraws;
            return;
        }
        const Node& node = model->nodes[size_t(id)];
        if (node.mesh < 0) continue;
        if (size_t(node.mesh) >= model->meshes.size()) {
            ++strataPreviewStats_.skippedUnsafeDraws;
            return;
        }
        const mat4 world = draw.world * model->bindWorld[size_t(id)];
        for (const auto& primitive : staged.primitives) {
            if (primitive.mesh != uint32_t(node.mesh)) continue;
            const bool canShadow = quality_ != ShadowQuality::Off &&
                !(draw.flags & kDrawNoCastShadow) &&
                model->materials.size() > primitive.material &&
                model->materials[primitive.material].castShadow;

            // Main and sun shadow paths need independent node-to-clip
            // transforms and different pixel budgets. Count only; never
            // publish the candidate or use it to suppress a legacy draw.
            for (int passIndex = 0; passIndex < (canShadow ? 2 : 1);
                 ++passIndex) {
                const bool shadow = passIndex == 1;
                const strata::RenderPass pass = shadow
                    ? strata::RenderPass::SunShadow
                    : strata::RenderPass::MainOpaque;
                strata::ViewConfig view{};
                view.objectToClip =
                    (shadow ? lightViewProj_ : viewProj_) * world;
                view.width = shadow ? shadowSize_ : sceneW_;
                view.height = shadow ? shadowSize_ : sceneH_;
                view.clipDepthZeroToOne = dev_->info().clipDepthZeroToOne;
                view.errorThresholdPixels = shadow
                    ? draw.geometry.shadowErrorPixels
                    : draw.geometry.errorPixels;
                if (!std::isfinite(view.errorThresholdPixels) ||
                    view.errorThresholdPixels <= 0)
                    view.errorThresholdPixels = 1.0f;
                ++strataPreviewStats_.attempted;
                strata::RenderCandidate candidate;
                if (!strata::prepareRenderCandidate(
                        *model, staged, pooled, sourceKey,
                        primitive.mesh, primitive.primitive, pass, view,
                        candidate)) {
                    ++strataPreviewStats_.rejected;
                    continue;
                }
                ++strataPreviewStats_.ready;
                strataPreviewStats_.selectedClusters +=
                    candidate.selectedClusters;
                if (candidate.ranges.empty())
                    ++strataPreviewStats_.fullyCulled;
            }
        }
    }
}
} // namespace aiverse::render
