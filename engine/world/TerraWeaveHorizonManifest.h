// WorldScene opt-in configuration for TerraWeave Horizon HLOD.
// Keeps JSON parsing out of the reusable terrain/silhouette generator.
#pragma once
#include "engine/world/WorldAssets.h"
#include "engine/render/TerraWeaveHorizonScene.h"
#include "engine/world/TerraWeaveForestScene.h"
#include <memory>
#include <cstdint>
#include <climits>

namespace aiverse::world {

inline std::unique_ptr<terraweave::TerraWeaveHorizonScene>
horizonSceneFromManifest(
    const Json& manifest,
    const terraweave::TerraWeaveForestScene* forest = nullptr) {
    if (!manifest.has("terraweaveHorizon")) return {};
    const Json& j=manifest["terraweaveHorizon"];
    if (!j.isObject())
        throw JsonError("world.terraweaveHorizon: expected object");

    terraweave::TerrainSettings terrain =
        forest ? forest->settings().terrain : terraweave::TerrainSettings{};
    terraweave::BiomeSettings biomes =
        forest ? forest->settings().biomes : terraweave::BiomeSettings{};
    terrain.seed=static_cast<uint64_t>(worldInteger(
        j,"terrainSeed",static_cast<int64_t>(terrain.seed),0,INT32_MAX));
    terrain.baseHeightCm=static_cast<int32_t>(worldInteger(
        j,"baseHeightCm",terrain.baseHeightCm,-1000000,1000000));
    terrain.reliefCm=static_cast<int32_t>(worldInteger(
        j,"reliefCm",terrain.reliefCm,0,1000000));
    terrain.basePeriodCm=static_cast<int32_t>(worldInteger(
        j,"basePeriodCm",terrain.basePeriodCm,1,10000000));
    terrain.octaves=static_cast<uint32_t>(worldInteger(
        j,"octaves",terrain.octaves,1,12));
    biomes.seed=static_cast<uint64_t>(worldInteger(
        j,"biomeSeed",static_cast<int64_t>(biomes.seed),0,INT32_MAX));
    biomes.seaLevelCm=static_cast<int32_t>(worldInteger(
        j,"seaLevelCm",biomes.seaLevelCm,-1000000,1000000));

    terraweave::HorizonSettings settings;
    settings.middleTileCm=static_cast<int32_t>(worldInteger(
        j,"middleTileCm",settings.middleTileCm,1000,200000));
    settings.middleTilesPerSide=static_cast<uint32_t>(worldInteger(
        j,"middleTilesPerSide",settings.middleTilesPerSide,4,32));
    settings.distantTileCm=static_cast<int32_t>(worldInteger(
        j,"distantTileCm",settings.distantTileCm,1000,400000));
    settings.distantTilesPerSide=static_cast<uint32_t>(worldInteger(
        j,"distantTilesPerSide",settings.distantTilesPerSide,4,32));
    // Align the handover with the actual detail-forest radius, avoiding
    // an uncovered annulus when both optional modules are enabled.
    const int32_t nearForestEnd = forest ?
        forest->settings().view.cullRangeCm : settings.closeForestEndCm;
    settings.closeForestEndCm=static_cast<int32_t>(worldInteger(
        j,"closeForestEndCm",nearForestEnd,0,10000000));
    settings.middleForestEndCm=static_cast<int32_t>(worldInteger(
        j,"middleForestEndCm",settings.middleForestEndCm,0,10000000));
    settings.terrainBeginCm=static_cast<int32_t>(worldInteger(
        j,"terrainBeginCm",settings.terrainBeginCm,0,10000000));
    settings.terrainResolution=static_cast<uint32_t>(worldInteger(
        j,"terrainResolution",settings.terrainResolution,8,96));
    settings.atlasTilePixels=static_cast<uint32_t>(worldInteger(
        j,"atlasTilePixels",settings.atlasTilePixels,16,64));
    settings.canopyHeightCm=static_cast<int32_t>(worldInteger(
        j,"canopyHeightCm",settings.canopyHeightCm,100,10000));
    settings.crownDetail=static_cast<uint32_t>(worldInteger(
        j,"crownDetail",settings.crownDetail,1,3));
    settings.orientationViews=static_cast<uint32_t>(worldInteger(
        j,"orientationViews",settings.orientationViews,1,2));
    settings.nearBlendCm=static_cast<int32_t>(worldInteger(
        j,"nearBlendCm",settings.nearBlendCm,0,10000000));
    settings.tierBlendCm=static_cast<int32_t>(worldInteger(
        j,"tierBlendCm",settings.tierBlendCm,0,10000000));
    settings.distantTerrain=j.getBool("distantTerrain",true);
    terraweave::validateHorizonSettings(settings);
    return std::make_unique<terraweave::TerraWeaveHorizonScene>(
        terrain,biomes,settings);
}

} // namespace aiverse::world
