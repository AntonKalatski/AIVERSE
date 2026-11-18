// NEREID -> Prisma: explicit per-frame read-only optical adapter.
// Use on the presentation thread before Renderer3D::render(...,&postFx).
// Never register a second water ECS sampler or create independent post passes.
#pragma once
#include "engine/world/WorldScene.h"
#include "engine/render/PostFx.h"

namespace aiverse::world {

inline void updateNereidPrismaUnderwater(
        const WorldScene& scene,render::PostFx& prisma,
        Vec3 cameraWorldPosition,float deltaSeconds) {
    const auto water=scene.nereidUnderwaterAt(cameraWorldPosition);
    render::PostFxUnderwater input;
    input.submerged=water.submerged;
    input.tintR=water.deepR;
    input.tintG=water.deepG;
    input.tintB=water.deepB;
    input.absorption=water.absorption;
    input.depthMetres=float(water.depthMetres);
    prisma.setUnderwater(input,deltaSeconds);
}
} // namespace aiverse::world
