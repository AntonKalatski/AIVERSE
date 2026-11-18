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
    uint32_t crownDetail = 2;         // 1..3 CPU-only crown silhouette quality
    uint32_t orientationViews = 1;    // 1 cheap shared, 2 orthogonal canopy projections
    int32_t nearBlendCm = 6000;       // masked silhouette fade-in before near trees end
    int32_t tierBlendCm = 16000;      // stochastic mid-to-far overlap around 1 km
    bool distantTerrain = true;       // opt-in manifest, disabled unless requested
};

struct HorizonCluster {
    int64_t centerXcm = 0, centerZcm = 0;
    int32_t tileSizeCm = 0;
    int32_t bottomCm = 0, topCm = 0;
    uint32_t atlasX = 0, atlasY = 0; // first directional silhouette
    uint32_t atlasSideX = 0, atlasSideY = 0; // 90-degree silhouette if enabled
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
inline uint32_t smoothCoverage(int64_t value, int64_t start, int64_t end) {
    if (end<=start) return value>=end ? 255u : 0u;
    if (value<=start) return 0u;
    if (value>=end) return 255u;
    // Q16 smoothstep; deterministic integer CPU baker.
    const int64_t t=(value-start)*65536/(end-start);
    const int64_t t2=t*t/65536;
    const int64_t eased=t2*(3*65536-2*t)/65536;
    return static_cast<uint32_t>(eased*255/65536);
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
        s.canopyHeightCm>10000 || s.crownDetail<1 || s.crownDetail>3 ||
        (s.orientationViews!=1 && s.orientationViews!=2) ||
        s.nearBlendCm<0 || s.nearBlendCm>s.closeForestEndCm ||
        int64_t(s.nearBlendCm)>s.middleForestEndCm-s.closeForestEndCm ||
        s.tierBlendCm<0 ||
        int64_t(s.tierBlendCm)>s.middleForestEndCm-s.closeForestEndCm)
        throw std::invalid_argument("TerraWeave Horizon settings");
    const uint64_t tiles=(uint64_t(s.middleTilesPerSide)*s.middleTilesPerSide+
                          uint64_t(s.distantTilesPerSide)*s.distantTilesPerSide)*
                          s.orientationViews;
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

// Worst-case resident atlas/mesh budget, independent of sampled biome and
// without claiming any particular GPU driver's measured draw-call count.
struct HorizonBudget {
    size_t atlasBytes = 0;
    size_t maxVertices = 0;
    size_t maxTriangles = 0;
    size_t maxForestClusters = 0;
};

inline HorizonBudget estimateHorizonBudget(const HorizonSettings& s) {
    validateHorizonSettings(s);
    const size_t clusters=size_t(s.middleTilesPerSide)*s.middleTilesPerSide+
                          size_t(s.distantTilesPerSide)*s.distantTilesPerSide;
    const size_t atlasSide=
        size_t(horizon_detail::checkedSquareRootCeil(
            static_cast<uint32_t>(clusters*s.orientationViews)))*
            s.atlasTilePixels;
    const size_t terrainVertices=s.distantTerrain ?
        size_t(s.terrainResolution+1)*(s.terrainResolution+1):0;
    const size_t terrainTriangles=s.distantTerrain ?
        size_t(s.terrainResolution)*s.terrainResolution*2:0;
    // Two crossed cards per cluster, each with 4 vertices and 2 triangles.
    return {atlasSide*atlasSide*4,terrainVertices+clusters*8,
            terrainTriangles+clusters*4,clusters};
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
    const uint32_t atlasSide=horizon_detail::checkedSquareRootCeil(
                                 possible*s.orientationViews)*s.atlasTilePixels;
    out.atlasWidth=atlasSide;out.atlasHeight=atlasSide;
    out.atlasRgba.assign(size_t(atlasSide)*atlasSide*4,0);
    out.clusters.reserve(possible);

    const auto addLayer = [&](int32_t cell,uint32_t count,int32_t minDistance,
                              int32_t maxDistance,uint64_t salt,bool middle) {
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
                    dist2>=int64_t(maxDistance)*maxDistance)continue;
                // Spatial alpha-to-coverage: each cluster's silhouette
                // progressively appears/disappears through world-stable
                // pixel dithering, without one draw per cluster or a
                // new alpha-blended shader. The near geometry retains
                // ownership up to closeForestEndCm.
                const int64_t dist=static_cast<int64_t>(std::sqrt(
                    static_cast<double>(dist2)));
                uint32_t visibility=255;
                if(middle) {
                    const uint32_t begin=horizon_detail::smoothCoverage(
                        dist,s.closeForestEndCm-s.nearBlendCm,
                        s.closeForestEndCm);
                    const uint32_t end=255u-horizon_detail::smoothCoverage(
                        dist,s.middleForestEndCm-s.tierBlendCm/2,
                        s.middleForestEndCm+(s.tierBlendCm-s.tierBlendCm/2));
                    visibility=begin*end/255u;
                } else {
                    visibility=horizon_detail::smoothCoverage(
                        dist,s.middleForestEndCm-s.tierBlendCm/2,
                        s.middleForestEndCm+(s.tierBlendCm-s.tierBlendCm/2));
                }
                if(!visibility)continue;
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
                // A canopy is a mixture of small overlapping crowns, not
                // one vertical rectangle per image column. All shapes use
                // stable world-cell hashes and integer pixel coverage:
                // camera motion cannot change their profile until a whole
                // region is explicitly rebaked.
                const int32_t heightRange=std::max(1,top-bottom);
                const uint32_t p=s.atlasTilePixels;
                const uint32_t crownCount=3u+(p/6u)*s.crownDetail;
                for(uint32_t crown=0;crown<crownCount;++crown) {
                    const uint64_t h=detail::hash2(
                        gx*257+int64_t(crown),gz*131+int64_t(crown)/3,
                        terrain.seed^salt^0x4c45414643524f57ULL);
                    const uint32_t column=2u+static_cast<uint32_t>(
                        (h&65535ULL)*(p-4u)/65536ULL);
                    const int64_t sampleX=gx*cell+int64_t(column)*cell/(p-1);
                    const auto subBiome=climateAt(terrain,biomes,sampleX,z).biome;
                    const uint32_t coverage=horizon_detail::canopyStrength(subBiome);
                    if (((h>>16)&255ULL)>=coverage)continue;
                    const int32_t surface=heightAtCm(terrain,sampleX,z);
                    const int32_t crownCm=std::max<int32_t>(220,
                        int64_t(s.canopyHeightCm)*
                        (68+int32_t((h>>27)%60))/100);
                    const int32_t heightCenter=surface+crownCm*66/100-bottom;
                    const int32_t imageY=int32_t(p-1)-
                        int32_t(int64_t(heightCenter)*p/heightRange);
                    const int32_t rx=std::max<int32_t>(2,int32_t(p/11u)+
                        int32_t((h>>40)%4));
                    const int32_t ry=std::clamp<int32_t>(
                        int32_t(int64_t(crownCm)*p*52/
                                (int64_t(heightRange)*100)),2,
                        std::max<int32_t>(2,int32_t(p/3u)));
                    const bool needleShape=subBiome==Biome::Tundra ||
                        (subBiome==Biome::Forest&&((h>>47)&3ULL)==0);
                    const int32_t loX=std::max<int32_t>(1,int32_t(column)-rx);
                    const int32_t hiX=std::min<int32_t>(int32_t(p)-2,
                                                        int32_t(column)+rx);
                    const int32_t loY=std::max<int32_t>(1,imageY-ry);
                    const int32_t hiY=std::min<int32_t>(int32_t(p)-2,imageY+ry);
                    for(int32_t row=loY;row<=hiY;++row) {
                        const int32_t dy=row-imageY;
                        // Conifer tapers at the top while broadleaf crowns
                        // remain rounded/elliptical.
                        const int32_t width=needleShape ?
                            std::max<int32_t>(1,rx*(dy+ry+1)/(2*ry+1)) : rx;
                        for(int32_t col=loX;col<=hiX;++col) {
                            const int32_t dx=col-int32_t(column);
                            const int64_t ellipse=
                                int64_t(dx)*dx*4096/(int64_t(width)*width)+
                                int64_t(dy)*dy*4096/(int64_t(ry)*ry);
                            if(ellipse>4096)continue;
                            // One-pixel soft coverage for alpha mask filtering.
                            const uint32_t alpha=ellipse<=3277 ? 255u :
                                static_cast<uint32_t>(
                                    (4096-ellipse)*255/819);
                            if(alpha<128u)continue;
                            // Stable world-space pixel threshold. A single
                            // shared atlas remains sufficient; coverage in
                            // the transition band has no extra geometry.
                            if(visibility<255u &&
                                (detail::hash2(gx*4096+col,gz*4096+row,
                                    terrain.seed^salt^0x4449544845524c4fULL)
                                    &255ULL)>=visibility)continue;
                            const size_t at=(size_t(py+uint32_t(row))*
                                atlasSide+px+uint32_t(col))*4;
                            const int32_t shade=std::clamp<int32_t>(
                                12-int32_t(ellipse/260)+
                                (imageY-row)*3+int32_t((h>>52)%17)-8,
                                -30,40);
                            const int32_t baseGreen=subBiome==Biome::Tundra
                                ? 92 : subBiome==Biome::Wetland ? 102 : 89;
                            const uint8_t green=static_cast<uint8_t>(
                                std::clamp(baseGreen+shade,40,158));
                            const uint8_t red=static_cast<uint8_t>(
                                std::clamp(28+shade/3,14,68));
                            const uint8_t blue=static_cast<uint8_t>(
                                std::clamp(25+shade/4,16,65));
                            // Painter's order is stable; silhouettes overlap
                            // naturally and do not add card geometry.
                            if(alpha>=out.atlasRgba[at+3] ||
                                ((h>>55)&1ULL)==0) {
                                out.atlasRgba[at]=red;
                                out.atlasRgba[at+1]=green;
                                out.atlasRgba[at+2]=blue;
                                out.atlasRgba[at+3]=static_cast<uint8_t>(alpha);
                            }
                        }
                    }
                    // Occasional thin exposed trunk helps break the flat
                    // "hedge wall" outline, without generating real trees.
                    if((h&3ULL)==0) {
                        const int32_t groundRow=int32_t(p-1)-
                            int32_t(int64_t(surface-bottom)*p/heightRange);
                        const int32_t trunkStart=std::max(imageY,0);
                        for(int32_t row=trunkStart;row<=std::min<int32_t>(
                                groundRow,int32_t(p)-2);++row) {
                            const size_t at=(size_t(py+uint32_t(row))*
                                atlasSide+px+column)*4;
                            if(out.atlasRgba[at+3])continue;
                            out.atlasRgba[at]=47;
                            out.atlasRgba[at+1]=37;
                            out.atlasRgba[at+2]=24;
                            out.atlasRgba[at+3]=255;
                        }
                    }
                }
                out.clusters.push_back({x,z,cell,bottom,top,px,py,p,
                    static_cast<uint8_t>(density)});
            }
    };
    addLayer(s.middleTileCm,s.middleTilesPerSide,
             std::max(0,s.closeForestEndCm-s.nearBlendCm),
             s.middleForestEndCm+s.tierBlendCm/2,
             0x9c6f54574d49444dULL,true);
    const int32_t farRadius=static_cast<int32_t>(
        int64_t(s.distantTilesPerSide)*s.distantTileCm/2);
    addLayer(s.distantTileCm,s.distantTilesPerSide,
             s.middleForestEndCm-s.tierBlendCm/2,
             farRadius,0x9c6f54574641524dULL,false);

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
