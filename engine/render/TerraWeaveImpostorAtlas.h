// TerraWeave multi-azimuth tree impostors. All azimuths share a single GPU
// texture and a single quad model; per-view MATERIAL overrides select UV tiles.
// This preserves Renderer3D's normal model instancing/material grouping.
#pragma once

#include "engine/render/TerraWeaveArborBillboard.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ArborImpostorAtlasSettings {
    ArborBillboardSettings tile{};
    uint32_t azimuthViews = 8; // 4, 8, or 16
};

// Keep one vector<Material> per azimuth to pass to ModelDraw::materials.
// These overrides MUST outlive all Renderer3D submissions of the frame.
struct ArborImpostorAtlas {
    std::unique_ptr<render::Model> model;
    std::vector<std::vector<render::Material>> viewMaterials;
    uint32_t views = 0;
};

inline ArborImpostorAtlas makeArborImpostorAtlas(
        const TreeProfile& profile, uint64_t seed,
        ArborImpostorAtlasSettings settings = {}) {
    const uint32_t views = settings.azimuthViews;
    const uint32_t width = settings.tile.width;
    const uint32_t height = settings.tile.height;
    const uint32_t gutter = settings.tile.margin;
    if ((views != 4 && views != 8 && views != 16) ||
        width < 32 || width > 512 || height < 32 || height > 1024 ||
        gutter < 2 || gutter * 3 >= width || gutter * 3 >= height ||
        width * views > 8192)
        throw std::invalid_argument("TerraWeave impostor atlas settings");

    // Reuse the standard one-quad mesh and alpha-mask material.
    ArborImpostorAtlas out;
    out.model = makeArborBillboardModel(profile, seed, settings.tile);
    out.views = views;
    out.model->path = "terraweave/arbor-impostor-atlas/" +
                      std::to_string(profile.speciesId) + "/" +
                      std::to_string(seed) + "/" + std::to_string(views);

    render::Image& atlas = out.model->images[0];
    atlas.name = "TerraWeave Arbor azimuth atlas";
    atlas.width = static_cast<int>(width * views);
    atlas.height = static_cast<int>(height);
    atlas.rgba.assign(static_cast<size_t>(atlas.width) * atlas.height * 4, 0);
    atlas.repeat = false;

    // Derive all viewpoints from the *same* full skeleton. Never regenerate
    // branches with a different seed as the camera azimuth changes.
    const TreeSkeleton tree = generateTree(profile, seed, TreeLOD::High);
    const float halfWidthCm = float(std::max<int32_t>(100, tree.crownRadiusCm));
    const float visualHeightCm = float(std::max<int32_t>(
        tree.heightCm, tree.heightCm + static_cast<int32_t>(profile.leafRadiusCm)));
    const float pixelsX = float(width / 2 - gutter) / halfWidthCm;
    const float pixelsY = float(height - 2 * gutter) / visualHeightCm;
    constexpr double tau = 6.283185307179586476925286766559;

    // Pixel masks use direct RGBA8 writes. Transparent texels stay black,
    // and each view receives its own padded tile (bilinear filtering cannot
    // sample into an adjacent view at the quad's inset UVs).
    for (uint32_t frame = 0; frame < views; ++frame) {
        const double angle = tau * double(frame) / double(views);
        const float c = static_cast<float>(std::cos(angle));
        const float s = static_cast<float>(std::sin(angle));
        const int tileLeft = static_cast<int>(frame * width);
        const int W = static_cast<int>(width), H = static_cast<int>(height);
        auto xScreen = [&](int32_t x, int32_t z) {
            return W / 2 + static_cast<int>(std::lround(
                (float(x) * c - float(z) * s) * pixelsX));
        };
        auto yScreen = [&](int32_t y) {
            return H - 1 - static_cast<int>(gutter) -
                   static_cast<int>(std::lround(float(y) * pixelsY));
        };
        auto disk = [&](int cx, int cy, int radius,
                        uint8_t red, uint8_t green, uint8_t blue) {
            radius = std::clamp(radius, 1, std::max(W, H));
            const int minX = std::max(0, cx - radius);
            const int maxX = std::min(W - 1, cx + radius);
            const int minY = std::max(0, cy - radius);
            const int maxY = std::min(H - 1, cy + radius);
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    const int dx = x - cx, dy = y - cy;
                    if (dx * dx + dy * dy > radius * radius) continue;
                    const size_t at = (static_cast<size_t>(y) * atlas.width +
                                       static_cast<size_t>(tileLeft + x)) * 4;
                    atlas.rgba[at] = red;
                    atlas.rgba[at + 1] = green;
                    atlas.rgba[at + 2] = blue;
                    atlas.rgba[at + 3] = 255;
                }
            }
        };
        // Bark first, canopy second: foreground foliage naturally covers
        // branch silhouettes. Same deterministic palette for each viewpoint.
        for (const TreeBranch& branch : tree.branches) {
            const int x0 = xScreen(branch.x0, branch.z0);
            const int y0 = yScreen(branch.y0);
            const int x1 = xScreen(branch.x1, branch.z1);
            const int y1 = yScreen(branch.y1);
            const int steps = std::max(1, std::max(std::abs(x1 - x0),
                                                   std::abs(y1 - y0)));
            for (int step = 0; step <= steps; ++step) {
                const int x = x0 + (x1 - x0) * step / steps;
                const int y = y0 + (y1 - y0) * step / steps;
                const int32_t radiusCm = branch.startRadiusCm +
                    (branch.endRadiusCm - branch.startRadiusCm) * step / steps;
                disk(x, y, std::max(1, static_cast<int>(std::lround(
                     float(radiusCm) * pixelsX))), 103, 64, 38);
            }
        }
        for (const LeafCluster& leaf : tree.leaves) {
            const uint8_t v = static_cast<uint8_t>(leaf.variation % 29);
            disk(xScreen(leaf.xCm, leaf.zCm), yScreen(leaf.yCm),
                 std::max(1, static_cast<int>(std::lround(
                     float(leaf.radiusCm) * std::min(pixelsX, pixelsY)))),
                 static_cast<uint8_t>(34 + v), static_cast<uint8_t>(88 + v),
                 static_cast<uint8_t>(27 + v / 2));
        }
    }

    // Use one model and one texture for *all* viewpoints. A fixed vector of
    // per-azimuth material overrides only changes the UV transform, which
    // Renderer3D already uploads/batches as normal PBR material state.
    out.viewMaterials.reserve(views);
    for (uint32_t frame = 0; frame < views; ++frame) {
        render::Material m = out.model->materials[0];
        // Half-texel inset prevents filtering into adjacent atlas tiles.
        const float atlasWidth = float(width * views);
        m.uvOffset.x = (float(frame * width) + 0.5f) / atlasWidth;
        m.uvOffset.y = 0.5f / float(height);
        m.uvScale.x = (float(width) - 1.0f) / atlasWidth;
        m.uvScale.y = (float(height) - 1.0f) / float(height);
        out.viewMaterials.push_back({std::move(m)});
    }
    return out;
}

// Nearest captured azimuth relative to the *tree's local yaw*.
// The quad itself always faces the camera; the captured view determines
// which side of the deterministic original tree is visible on the card.
inline uint32_t selectArborImpostorAzimuth(uint32_t instanceYawMilliDegrees,
                                          int64_t treeXcm, int64_t treeZcm,
                                          int64_t cameraXcm, int64_t cameraZcm,
                                          uint32_t views) {
    if ((views != 4 && views != 8 && views != 16) ||
        instanceYawMilliDegrees >= 360000 ||
        treeXcm < -kWorldLimitCm || treeXcm > kWorldLimitCm ||
        treeZcm < -kWorldLimitCm || treeZcm > kWorldLimitCm ||
        cameraXcm < -kWorldLimitCm || cameraXcm > kWorldLimitCm ||
        cameraZcm < -kWorldLimitCm || cameraZcm > kWorldLimitCm)
        throw std::invalid_argument("TerraWeave impostor selection");
    constexpr double tau = 6.283185307179586476925286766559;
    // Avoid floating-point subtraction of large absolute world coordinates.
    const double dx = double(cameraXcm - treeXcm);
    const double dz = double(cameraZcm - treeZcm);
    const double treeYaw = double(instanceYawMilliDegrees) * tau / 360000.0;
    double relative = std::atan2(dx, dz) - treeYaw;
    relative = std::fmod(relative, tau);
    if (relative < 0) relative += tau;
    return static_cast<uint32_t>(std::floor(
        relative * double(views) / tau + 0.5)) % views;
}

} // namespace aiverse::terraweave
