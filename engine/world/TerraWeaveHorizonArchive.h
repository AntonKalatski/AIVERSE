// TerraWeave Horizon static-world archive: portable little-endian, versioned,
// strictly bounded CPU bake bytes. The engine does not perform file I/O here.
// Native projects decide how to store and retrieve these blobs (pack/asset/CDN).
#pragma once
#include "engine/world/TerraWeaveHorizon.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {
namespace horizon_archive_detail {

inline constexpr uint32_t kMagic=0x31485754u; // "TWH1" in little endian
inline constexpr uint32_t kVersion=1;
inline constexpr size_t kHeaderBytes=24; // magic,version,fingerprint,checksum
inline constexpr size_t kMaxBytes=20u*1024u*1024u;

inline void put(std::vector<uint8_t>& out,uint64_t v,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i)
        out.push_back(static_cast<uint8_t>(v>>(i*8)));
}
inline uint64_t get(std::span<const uint8_t> bytes,size_t& cursor,unsigned count) {
    if(cursor>bytes.size()||count>bytes.size()-cursor)
        throw std::invalid_argument("TerraWeave Horizon archive truncated");
    uint64_t v=0;
    for(unsigned i=0;i<count;++i)v|=uint64_t(bytes[cursor++])<<(i*8);
    return v;
}
inline void put32(std::vector<uint8_t>& out,uint32_t v){put(out,v,4);}
inline void put64(std::vector<uint8_t>& out,uint64_t v){put(out,v,8);}
inline void putI32(std::vector<uint8_t>& out,int32_t v) {
    put32(out,std::bit_cast<uint32_t>(v));
}
inline void putI64(std::vector<uint8_t>& out,int64_t v) {
    put64(out,std::bit_cast<uint64_t>(v));
}
inline uint32_t get32(std::span<const uint8_t> in,size_t& at) {
    return static_cast<uint32_t>(get(in,at,4));
}
inline uint64_t get64(std::span<const uint8_t> in,size_t& at) {
    return get(in,at,8);
}
inline int32_t getI32(std::span<const uint8_t> in,size_t& at) {
    return std::bit_cast<int32_t>(get32(in,at));
}
inline int64_t getI64(std::span<const uint8_t> in,size_t& at) {
    return std::bit_cast<int64_t>(get64(in,at));
}
inline uint64_t checksum(std::span<const uint8_t> bytes) {
    uint64_t hash=14695981039346656037ULL;
    for(const uint8_t value:bytes) {
        hash^=value;
        hash*=1099511628211ULL;
    }
    return hash;
}
inline void validate(const HorizonBake& b) {
    if(b.atlasWidth==0||b.atlasWidth!=b.atlasHeight||
       b.atlasWidth>2048||
       b.atlasRgba.size()!=size_t(b.atlasWidth)*b.atlasHeight*4u||
       b.atlasRgba.size()>16u*1024u*1024u||
       b.terrainResolution>96u||
       b.sideCm<=0||b.sideCm>kWorldLimitCm||
       b.terrainBeginCm<0||b.terrainBeginCm>kWorldLimitCm||
       b.originXcm<-kWorldLimitCm||b.originXcm>kWorldLimitCm||
       b.originZcm<-kWorldLimitCm||b.originZcm>kWorldLimitCm||
       b.cameraXcm<-kWorldLimitCm||b.cameraXcm>kWorldLimitCm||
       b.cameraZcm<-kWorldLimitCm||b.cameraZcm>kWorldLimitCm||
       b.terrainVertices.size()>10000||b.clusters.size()>2048)
        throw std::invalid_argument("TerraWeave Horizon archive bounds");
    if(!b.terrainVertices.empty() &&
       (b.terrainResolution==0||
        b.terrainVertices.size()!=size_t(b.terrainResolution+1)*
                                    size_t(b.terrainResolution+1)))
        throw std::invalid_argument("TerraWeave Horizon archive terrain grid");
    for(const auto& v:b.terrainVertices)
        if(v.xCm<-kWorldLimitCm||v.xCm>kWorldLimitCm||
           v.zCm<-kWorldLimitCm||v.zCm>kWorldLimitCm||
           v.heightCm<-kWorldLimitCm||v.heightCm>kWorldLimitCm)
            throw std::invalid_argument("TerraWeave Horizon archive terrain vertex");
    for(const auto& c:b.clusters)
        if(c.centerXcm<-kWorldLimitCm||c.centerXcm>kWorldLimitCm||
           c.centerZcm<-kWorldLimitCm||c.centerZcm>kWorldLimitCm||
           c.tileSizeCm<=0||c.tileSizeCm>400000||
           c.bottomCm>=c.topCm||
           c.atlasSize==0||c.atlasSize>64||
           c.atlasX>b.atlasWidth-c.atlasSize||
           c.atlasY>b.atlasHeight-c.atlasSize||
           c.atlasSideX>b.atlasWidth-c.atlasSize||
           c.atlasSideY>b.atlasHeight-c.atlasSize)
            throw std::invalid_argument("TerraWeave Horizon archive cluster");
}
} // namespace horizon_archive_detail

// Deterministic configuration identity; authored terrain masks require an
// independently stable content hash (0 means purely procedural).
inline uint64_t horizonArchiveFingerprint(
        const TerrainSettings& terrain,const BiomeSettings& biomes,
        const HorizonSettings& settings,uint64_t authoredContentHash=0) {
    uint64_t h=14695981039346656037ULL;
    const auto add=[&](uint64_t value){h^=value;h*=1099511628211ULL;};
    add(horizon_archive_detail::kVersion);
    add(terrain.seed);
    add(static_cast<uint32_t>(terrain.baseHeightCm));
    add(static_cast<uint32_t>(terrain.reliefCm));
    add(static_cast<uint32_t>(terrain.basePeriodCm));
    add(terrain.octaves);
    add(biomes.seed);
    add(static_cast<uint32_t>(biomes.seaLevelCm));
    add(static_cast<uint32_t>(biomes.baseTemperature));
    add(static_cast<uint32_t>(biomes.baseHumidity));
    add(static_cast<uint32_t>(biomes.climatePeriodCm));
    add(static_cast<uint32_t>(biomes.variation));
    add(static_cast<uint32_t>(biomes.lapsePer100m));
    add(static_cast<uint32_t>(settings.middleTileCm));
    add(settings.middleTilesPerSide);
    add(static_cast<uint32_t>(settings.distantTileCm));
    add(settings.distantTilesPerSide);
    add(static_cast<uint32_t>(settings.closeForestEndCm));
    add(static_cast<uint32_t>(settings.middleForestEndCm));
    add(static_cast<uint32_t>(settings.terrainBeginCm));
    add(settings.terrainResolution);
    add(settings.atlasTilePixels);
    add(static_cast<uint32_t>(settings.canopyHeightCm));
    add(settings.crownDetail);
    add(settings.orientationViews);
    add(static_cast<uint32_t>(settings.nearBlendCm));
    add(static_cast<uint32_t>(settings.tierBlendCm));
    add(settings.distantTerrain?1u:0u);
    add(authoredContentHash);
    return h?h:1u;
}

inline std::vector<uint8_t> encodeHorizonArchive(
        const HorizonBake& bake,uint64_t fingerprint) {
    using namespace horizon_archive_detail;
    validate(bake);
    if(!fingerprint)
        throw std::invalid_argument("TerraWeave Horizon archive fingerprint");
    std::vector<uint8_t> out;
    out.reserve(kHeaderBytes+bake.atlasRgba.size()+
                bake.clusters.size()*60+bake.terrainVertices.size()*24+100);
    put32(out,kMagic);
    put32(out,kVersion);
    put64(out,fingerprint);
    put64(out,0); // content checksum, backfilled after payload
    putI64(out,bake.originXcm);putI64(out,bake.originZcm);
    putI32(out,bake.sideCm);put32(out,bake.terrainResolution);
    putI32(out,bake.terrainBeginCm);
    putI64(out,bake.cameraXcm);putI64(out,bake.cameraZcm);
    put32(out,bake.atlasWidth);put32(out,bake.atlasHeight);
    put32(out,static_cast<uint32_t>(bake.terrainVertices.size()));
    put32(out,static_cast<uint32_t>(bake.clusters.size()));
    out.insert(out.end(),bake.atlasRgba.begin(),bake.atlasRgba.end());
    for(const auto& v:bake.terrainVertices) {
        putI64(out,v.xCm);putI64(out,v.zCm);
        putI32(out,v.heightCm);put32(out,v.colorRgba);
    }
    for(const auto& c:bake.clusters) {
        putI64(out,c.centerXcm);putI64(out,c.centerZcm);
        putI32(out,c.tileSizeCm);putI32(out,c.bottomCm);
        putI32(out,c.topCm);put32(out,c.atlasX);
        put32(out,c.atlasY);put32(out,c.atlasSideX);
        put32(out,c.atlasSideY);put32(out,c.atlasSize);
        put32(out,c.coverage);
    }
    if(out.size()>kMaxBytes)
        throw std::invalid_argument("TerraWeave Horizon archive exceeds budget");
    const uint64_t hash=checksum(
        std::span<const uint8_t>(out).subspan(kHeaderBytes));
    for(unsigned i=0;i<8;++i)
        out[16+i]=static_cast<uint8_t>(hash>>(i*8));
    return out;
}

inline HorizonBake decodeHorizonArchive(
        std::span<const uint8_t> bytes,uint64_t expectedFingerprint) {
    using namespace horizon_archive_detail;
    if(bytes.size()<kHeaderBytes||bytes.size()>kMaxBytes)
        throw std::invalid_argument("TerraWeave Horizon archive byte count");
    size_t at=0;
    if(get32(bytes,at)!=kMagic||get32(bytes,at)!=kVersion)
        throw std::invalid_argument("TerraWeave Horizon archive format/version");
    if(!expectedFingerprint||get64(bytes,at)!=expectedFingerprint)
        throw std::invalid_argument("TerraWeave Horizon archive stale fingerprint");
    if(get64(bytes,at)!=checksum(bytes.subspan(kHeaderBytes)))
        throw std::invalid_argument("TerraWeave Horizon archive checksum");
    HorizonBake b;
    b.originXcm=getI64(bytes,at);b.originZcm=getI64(bytes,at);
    b.sideCm=getI32(bytes,at);b.terrainResolution=get32(bytes,at);
    b.terrainBeginCm=getI32(bytes,at);
    b.cameraXcm=getI64(bytes,at);b.cameraZcm=getI64(bytes,at);
    b.atlasWidth=get32(bytes,at);b.atlasHeight=get32(bytes,at);
    const uint32_t terrainCount=get32(bytes,at);
    const uint32_t clusterCount=get32(bytes,at);
    if(!b.atlasWidth||b.atlasWidth!=b.atlasHeight||
       b.atlasWidth>2048||terrainCount>10000||clusterCount>2048)
        throw std::invalid_argument("TerraWeave Horizon archive declared sizes");
    const size_t pixelBytes=size_t(b.atlasWidth)*b.atlasHeight*4;
    if(pixelBytes>16u*1024u*1024u||
       at>bytes.size()||pixelBytes>bytes.size()-at)
        throw std::invalid_argument("TerraWeave Horizon archive atlas truncated");
    b.atlasRgba.assign(bytes.begin()+static_cast<std::ptrdiff_t>(at),
                       bytes.begin()+static_cast<std::ptrdiff_t>(at+pixelBytes));
    at+=pixelBytes;
    b.terrainVertices.reserve(terrainCount);
    for(uint32_t i=0;i<terrainCount;++i) {
        HorizonTerrainVertex v;
        v.xCm=getI64(bytes,at);v.zCm=getI64(bytes,at);
        v.heightCm=getI32(bytes,at);v.colorRgba=get32(bytes,at);
        b.terrainVertices.push_back(v);
    }
    b.clusters.reserve(clusterCount);
    for(uint32_t i=0;i<clusterCount;++i) {
        HorizonCluster c;
        c.centerXcm=getI64(bytes,at);
        c.centerZcm=getI64(bytes,at);
        c.tileSizeCm=getI32(bytes,at);
        c.bottomCm=getI32(bytes,at);
        c.topCm=getI32(bytes,at);
        c.atlasX=get32(bytes,at);
        c.atlasY=get32(bytes,at);
        c.atlasSideX=get32(bytes,at);
        c.atlasSideY=get32(bytes,at);
        c.atlasSize=get32(bytes,at);
        const uint32_t cov=get32(bytes,at);
        if(cov>255)throw std::invalid_argument(
            "TerraWeave Horizon archive coverage");
        c.coverage=static_cast<uint8_t>(cov);
        b.clusters.push_back(c);
    }
    if(at!=bytes.size())
        throw std::invalid_argument("TerraWeave Horizon archive trailing bytes");
    validate(b);
    return b;
}

} // namespace aiverse::terraweave
