// TerraWeave Horizon HLOD: one coarse terrain mesh and a shared forest-cluster
// silhouette atlas covering several kilometres. CPU-only deterministic bake.
// Forest cover is evaluated at coarse resolution, NOT one tree at a time.
#pragma once
#include "engine/world/TerraWeave.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct HorizonSettings {
    int32_t middleTileCm = 12800;       // one 128m forest cluster
    uint32_t middleTilesPerSide = 16;   // 2.0km across (ring 0.25..1km)
    int32_t distantTileCm = 51200;      // one 512m forest cluster
    uint32_t distantTilesPerSide = 24;  // 12.3km across (ring 1..6km)
    int32_t closeForestEndCm = 25000;  // individual tree renderer owns this
    int32_t middleForestEndCm = 100000;
    int32_t terrainBeginCm = 110000;   // no proxy terrain over near authored ground
    uint32_t terrainResolution = 48;   // 48x48 quads for all 12km
    uint32_t atlasTilePixels = 48;     // ~8MB with default region sizes
    int32_t canopyHeightCm = 1800;    // forest crown silhouette thickness
    bool distantTerrain = true;       // opt-in manifest, disabled unless requested
};

struct HorizonCluster {
    int64_t centerXcm = 0, centerZcm = 0;
    int32_t tileSizeCm = 0;
    int32_t bottomCm = 0, topCm = 0;
    uint32_t atlasX = 0, atlasY = 0; // top-left pixel of tile silhouette
    uint32_t atlasSize = 0;
    uint8_t coverage = 0;             // 0..255, rasterized forest mask
};

struct HorizonTerrainVertex {
    int64_t xCm = 0, zCm = 0;
    int32_t heightCm = 0;
    uint32_t colorRgba = 0xffffffffu;
};

struct HorizonBake {
    int64_t originXcm = 0, originZcm = 0; // subtract before casting to float
    int32_t sideCm = 0;
    uint32_t terrainResolution = 0;
    int32_t terrainBeginCm = 0;
    int64_t cameraXcm = 0, cameraZcm = 0;
    uint32_t atlasWidth = 0, atlasHeight = 0;
    std::vector<uint8_t> atlasRgba; // one atlas, shared by all forest cluster cards
    std::vector<HorizonTerrainVertex> terrainVertices;
    std::vector<HorizonCluster> clusters;
};

namespace horizon_detail {
inline uint32_t checkedSquareRootCeil(uint32_t count) {
    uint32_t x=1;
    while (uint64_t(x)*x<count) ++x;
    return x;
}
inline uint32_t rgba(uint32_t r,uint32_t g,uint32_t b) {
    return 0xff000000u | ((b&255u)<<16) | ((g&255u)<<8) | (r&255u);
}
inline uint32_t biomeColor(Biome biome, int32_t heightCm, uint64_t noise) {
    uint32_t r=83, g=127, b=63;
    switch(biome) {
        case Biome::Ocean: r=42;g=74;b=117;break;
        case Biome::Tundra: r=131;g=137;b=126;break;
        case Biome::Desert: r=193;g=169;b=114;break;
        case Biome::Grassland: r=105;g=145;b=72;break;
        case Biome::Forest: r=52;g=99;b=50;break;
        case Biome::Wetland: r=68;g=110;b=73;break;
        case Biome::Alpine: r=116;g=121;b=116;break;
    }
    if (heightCm>240000) {r=196;g=199;b=194;}
    const int k=int((noise>>24)%21)-10;
    const auto add=[k](uint32_t c)->uint32_t {
        return static_cast<uint32_t>(std::clamp(int(c)+k,0,255));
    };
    return rgba(add(r),add(g),add(b));
}
inline uint32_t canopyStrength(Biome b) {
    switch(b) {
        case Biome::Forest: return 230;
        case Biome::Wetland: return 180;
        case Biome::Grassland: return 85;
        case Biome::Tundra: return 30;
        default: return 0;
    }
}
} // namespace horizon_detail

inline void validateHorizonSettings(const HorizonSettings& s) {
    if (s.middleTileCm<1000 || s.middleTileCm>200000 ||
        s.distantTileCm<s.middleTileCm || s.distantTileCm>400000 ||
        s.middleTilesPerSide<4 || s.middleTilesPerSide>32 ||
        s.distantTilesPerSide<4 || s.distantTilesPerSide>32 ||
        (s.middleTilesPerSide&1u) || (s.distantTilesPerSide&1u) ||
        s.closeForestEndCm<0 || s.middleForestEndCm<=s.closeForestEndCm ||
        s.terrainBeginCm<0 || s.atlasTilePixels<16 ||
        s.atlasTilePixels>64 || s.terrainResolution<8 ||
        s.terrainResolution>96 || s.canopyHeightCm<100 ||
        s.canopyHeightCm>10000)
        throw std::invalid_argument("TerraWeave Horizon settings");
    const uint64_t tiles=uint64_t(s.middleTilesPerSide)*s.middleTilesPerSide+
                         uint64_t(s.distantTilesPerSide)*s.distantTilesPerSide;
    const uint64_t side=uint64_t(horizon_detail::checkedSquareRootCeil(
        static_cast<uint32_t>(tiles)))*s.atlasTilePixels;
    if (side>2048 || side*side*4>16u*1024u*1024u)
        throw std::invalid_argument("TerraWeave Horizon atlas memory budget");
    if (int64_t(s.middleTileCm)*s.middleTilesPerSide/2 <
            s.middleForestEndCm-s.middleTileCm ||
        int64_t(s.distantTileCm)*s.distantTilesPerSide/2 <
            s.middleForestEndCm + s.distantTileCm)
        throw std::invalid_argument("TerraWeave Horizon rings do not cover ranges");
}

// Each call is deterministic for a snapped camera region, with fixed memory
// bounds. Bake on a worker in production; upload prepared models via the RHI.
inline HorizonBake bakeHorizon(const TerrainSettings& terrain,
                              const BiomeSettings& biomes,
                              const HorizonSettings& s,
                              int64_t cameraXcm, int64_t cameraZcm) {
    validateHorizonSettings(s);
    if (cameraXcm < -kWorldLimitCm || cameraXcm > kWorldLimitCm ||
        cameraZcm < -kWorldLimitCm || cameraZcm > kWorldLimitCm)
        throw std::out_of_range("TerraWeave Horizon observer");
    HorizonBake out;
    out.cameraXcm=cameraXcm;out.cameraZcm=cameraZcm;
    out.terrainResolution=s.terrainResolution;
    out.terrainBeginCm=s.terrainBeginCm;
    const int64_t farCell=s.distantTileCm;
    const int64_t farHalf=int64_t(s.distantTilesPerSide)/2;
    const int64_t cx=detail::floorDiv(cameraXcm,farCell);
    const int64_t cz=detail::floorDiv(cameraZcm,farCell);
    out.originXcm=(cx-farHalf)*farCell;
    out.originZcm=(cz-farHalf)*farCell;
    out.sideCm=static_cast<int32_t>(farCell*s.distantTilesPerSide);
    if (out.originXcm < -kWorldLimitCm+100 ||
        out.originZcm < -kWorldLimitCm+100 ||
        out.originXcm+out.sideCm>kWorldLimitCm-100 ||
        out.originZcm+out.sideCm>kWorldLimitCm-100)
        throw std::out_of_range("TerraWeave Horizon region extent");

    const uint32_t possible=s.middleTilesPerSide*s.middleTilesPerSide+
                            s.distantTilesPerSide*s.distantTilesPerSide;
    const uint32_t atlasSide=horizon_detail::checkedSquareRootCeil(possible)*
                             s.atlasTilePixels;
    out.atlasWidth=atlasSide;out.atlasHeight=atlasSide;
    out.atlasRgba.assign(size_t(atlasSide)*atlasSide*4,0);
    out.clusters.reserve(possible);

    const auto addLayer = [&](int32_t cell,uint32_t count,int32_t minDistance,
                              int32_t maxDistance,uint64_t salt) {
        const int64_t centerCellX=detail::floorDiv(cameraXcm,cell);
        const int64_t centerCellZ=detail::floorDiv(cameraZcm,cell);
        const int64_t half=count/2;
        for (uint32_t iz=0;iz<count;++iz)
            for (uint32_t ix=0;ix<count;++ix) {
                const int64_t gx=centerCellX+int64_t(ix)-half;
                const int64_t gz=centerCellZ+int64_t(iz)-half;
                const int64_t x=gx*cell+cell/2;
                const int64_t z=gz*cell+cell/2;
                const int64_t dx=x-cameraXcm,dz=z-cameraZcm;
                const int64_t dist2=dx*dx+dz*dz;
                if (dist2<int64_t(minDistance)*minDistance ||
                    dist2>int64_t(maxDistance)*maxDistance)continue;
                if (x-cell/2<-kWorldLimitCm+100 ||
                    z-cell/2<-kWorldLimitCm+100 ||
                    x+cell/2>kWorldLimitCm-100 ||
                    z+cell/2>kWorldLimitCm-100)continue;

                // Coarse vegetation sample: one evaluation per column. Never
                // instantiate individual distant trees or their ECS components.
                const auto biome=climateAt(terrain,biomes,x,z).biome;
                const uint32_t density=horizon_detail::canopyStrength(biome);
                if (!density)continue;
                const uint64_t tileHash=detail::hash2(gx,gz,terrain.seed^salt);
                if ((tileHash&255u)>=density)continue;
                int32_t low=std::numeric_limits<int32_t>::max();
                int32_t high=std::numeric_limits<int32_t>::min();
                for (int a=-1;a<=1;++a)for(int b=-1;b<=1;++b) {
                    const int32_t h=heightAtCm(terrain,x+a*cell/3,z+b*cell/3);
                    low=std::min(low,h);high=std::max(high,h);
                }
                const int32_t bottom=low-250;
                const int32_t top=high+s.canopyHeightCm+500;
                if (top-bottom>200000)continue; // prevent absurd giant billboard
                const uint32_t slot=static_cast<uint32_t>(out.clusters.size());
                const uint32_t px=(slot%(atlasSide/s.atlasTilePixels))*s.atlasTilePixels;
                const uint32_t py=(slot/(atlasSide/s.atlasTilePixels))*s.atlasTilePixels;
                const int32_t heightRange=std::max(1,top-bottom);
                const uint32_t p=s.atlasTilePixels;
                for(uint32_t col=1;col+1<p;++col) {
                    const int64_t columnX=gx*cell+
                        int64_t(col)*cell/(p-1);
                    const auto shadeBiome=climateAt(terrain,biomes,columnX,z).biome;
                    const uint32_t strength=horizon_detail::canopyStrength(shadeBiome);
                    const uint64_t var=detail::hash2(gx*256+col,gz,terrain.seed^salt);
                    if ((var&255u)>=strength)continue;
                    const int32_t ground=heightAtCm(terrain,columnX,z);
                    const int32_t canopy=std::max(200,s.canopyHeightCm*
                        (70+static_cast<int>((var>>16)%61))/100);
                    const int32_t yStart=ground+canopy*32/100-bottom;
                    const int32_t yTop=ground+canopy-bottom;
                    const int32_t first=std::clamp(int64_t(yStart)*p/heightRange,1LL,int64_t(p-2));
                    const int32_t last=std::clamp(int64_t(yTop)*p/heightRange,1LL,int64_t(p-2));
                    const uint8_t g=static_cast<uint8_t>(80+((var>>32)%40));
                    for(int32_t iy=first;iy<=last;++iy) {
                        const uint32_t ry=p-1-static_cast<uint32_t>(iy);
                        const size_t at=(size_t(py+ry)*atlasSide+px+col)*4;
                        out.atlasRgba[at]=static_cast<uint8_t>(25+(var>>8)%20);
                        out.atlasRgba[at+1]=g;
                        out.atlasRgba[at+2]=static_cast<uint8_t>(25+(var>>24)%20);
                        out.atlasRgba[at+3]=255;
                    }
                }
                out.clusters.push_back({x,z,cell,bottom,top,px,py,p,
                    static_cast<uint8_t>(density)});
            }
    };
    addLayer(s.middleTileCm,s.middleTilesPerSide,
             s.closeForestEndCm,s.middleForestEndCm,0x9c6f54574d49444dULL);
    const int32_t farRadius=static_cast<int32_t>(
        int64_t(s.distantTilesPerSide)*s.distantTileCm/2);
    addLayer(s.distantTileCm,s.distantTilesPerSide,
             s.middleForestEndCm,farRadius,0x9c6f54574641524dULL);

    if(s.distantTerrain) {
        const uint32_t n=s.terrainResolution;
        out.terrainVertices.reserve(size_t(n+1)*(n+1));
        for(uint32_t z=0;z<=n;++z)
            for(uint32_t x=0;x<=n;++x) {
                const int64_t wx=out.originXcm+int64_t(x)*out.sideCm/n;
                const int64_t wz=out.originZcm+int64_t(z)*out.sideCm/n;
                const int32_t h=heightAtCm(terrain,wx,wz);
                const auto biome=climateAt(terrain,biomes,wx,wz).biome;
                const uint32_t color=horizon_detail::biomeColor(
                    biome,h,detail::hash2(wx/51200,wz/51200,terrain.seed));
                out.terrainVertices.push_back({wx,wz,h,color});
            }
    }
    return out;
}

} // namespace aiverse::terraweave
