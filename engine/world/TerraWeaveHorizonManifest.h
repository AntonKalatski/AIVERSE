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
    const terraweave::TerraWeaveForestScene* forest = nullptr,
    AssetReader reader = {}) {
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

    const std::string quality=j.getStr("quality","balanced");
    terraweave::HorizonQualityPreset preset;
    if(quality=="minimal")preset=terraweave::HorizonQualityPreset::Minimal;
    else if(quality=="mobile")preset=terraweave::HorizonQualityPreset::Mobile;
    else if(quality=="balanced")preset=terraweave::HorizonQualityPreset::Balanced;
    else if(quality=="high")preset=terraweave::HorizonQualityPreset::High;
    else throw JsonError("world.terraweaveHorizon.quality: expected minimal|mobile|balanced|high");
    terraweave::HorizonSettings settings=terraweave::horizonSettingsForQuality(preset);
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
    settings.terrainSkirtDepthCm=static_cast<int32_t>(worldInteger(
        j,"terrainSkirtDepthCm",settings.terrainSkirtDepthCm,0,5000));
    settings.terrainGridSnapSteps=static_cast<uint32_t>(worldInteger(
        j,"terrainGridSnapSteps",settings.terrainGridSnapSteps,1,16));
    settings.innerTerrainLipCm=static_cast<int32_t>(worldInteger(
        j,"innerTerrainLipCm",settings.innerTerrainLipCm,0,200));
    settings.maxNearClipTriangles=static_cast<uint32_t>(worldInteger(
        j,"maxNearClipTriangles",settings.maxNearClipTriangles,0,8192));
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
    settings.maxCachedRegions=static_cast<uint32_t>(worldInteger(
        j,"maxCachedRegions",settings.maxCachedRegions,0,4));
    settings.maxCachedGpuRegions=static_cast<uint32_t>(worldInteger(
        j,"maxCachedGpuRegions",settings.maxCachedGpuRegions,0,3));
    settings.gpuCacheBudgetMiB=static_cast<uint32_t>(worldInteger(
        j,"gpuCacheBudgetMiB",settings.gpuCacheBudgetMiB,8,96));
    settings.regionTransitionMs=static_cast<uint32_t>(worldInteger(
        j,"regionTransitionMs",settings.regionTransitionMs,0,1000));
    settings.regionHysteresisCm=static_cast<int32_t>(worldInteger(
        j,"regionHysteresisCm",settings.regionHysteresisCm,0,100000));
    settings.nearBlendCm=static_cast<int32_t>(worldInteger(
        j,"nearBlendCm",settings.nearBlendCm,0,10000000));
    settings.tierBlendCm=static_cast<int32_t>(worldInteger(
        j,"tierBlendCm",settings.tierBlendCm,0,10000000));
    settings.distantTerrain=j.getBool("distantTerrain",true);
    terraweave::validateHorizonSettings(settings);
    auto scene=std::make_unique<terraweave::TerraWeaveHorizonScene>(
        terrain,biomes,settings);
    // Read-only prebaked assets are strictly opt-in. The game pack owns
    // access to the archive files; runtime never writes into content.
    const std::string archivePrefix=j.getStr("archivePrefix");
    if(!archivePrefix.empty()){
        if(!reader || !safeAssetPath(archivePrefix))
            throw JsonError("world.terraweaveHorizon.archivePrefix: invalid asset folder");
        auto store=std::make_shared<terraweave::HorizonArchiveStore>();
        store->authoredContentHash=static_cast<uint64_t>(worldInteger(
            j,"archiveContentHash",0,0,INT32_MAX));
        store->read=[reader,prefix=archivePrefix](
                int32_t cx,int32_t cz,std::vector<uint8_t>& out)->bool {
            // The only variable filename components are bounded integer
            // region coordinates; caller cannot supply a traversal path.
            const std::string asset=prefix+"/"+std::to_string(cx)+
                                    "_"+std::to_string(cz)+".twh";
            std::string bytes;
            if(!reader(asset,bytes))return false;
            if(bytes.size()>20u*1024u*1024u)return false;
            out.assign(bytes.begin(),bytes.end());
            return true;
        };
        scene->setArchiveStore(std::move(store));
    }
    return scene;
}

} // namespace aiverse::world
