#pragma once
// PRISMA: genre-independent image post-processing API.
// PostFx.h and PostFx.cpp remain as implementation/compatibility files.
// Serialized "postfx" and editor "postfx_*" operations are intentionally stable.
#include "PostFx.h"
#include "PrismaVolumes.h"

namespace aiverse::render {
using PrismaSettings = PostFxSettings;
using PrismaEffect = PostFxEffect;
using PrismaQuality = PostFxQuality;
using PrismaStats = PostFxStats;
using PrismaCostClass = PostFxCostClass;
using PrismaType = PostFxType;
} // namespace aiverse::render
