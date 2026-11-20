// Opt-in native alpine height source. Integer world-space samples, no project solver.
#pragma once
#include "engine/world/TerraWeave.h"
#include "engine/core/Fixed.h"
namespace aiverse::terraweave {
struct AlpinePeak {
    int64_t xCm=0,zCm=0;
    int32_t radiusXcm=10000,radiusZcm=16000,heightCm=12000;
    int32_t shearPermille=0;
};
inline int32_t alpineHeightCm(const TerrainSettings& terrain,
        const std::vector<AlpinePeak>& peaks,int64_t x,int64_t z,
        int32_t valleyHalfWidthCm=0,int32_t valleyBlendCm=10000) {
    if(x< -kWorldLimitCm||x>kWorldLimitCm||z< -kWorldLimitCm||z>kWorldLimitCm)throw std::out_of_range("Terra alpine sample");
    int64_t mountain=0;
    if(peaks.size()>32)throw std::length_error("Terra alpine peak budget");
    for(const auto& p:peaks){
        if(p.xCm< -kWorldLimitCm||p.xCm>kWorldLimitCm||p.zCm< -kWorldLimitCm||p.zCm>kWorldLimitCm||p.radiusXcm<1000||p.radiusXcm>100000||p.radiusZcm<1000||p.radiusZcm>100000||p.heightCm<0||p.heightCm>100000||std::abs(int64_t(p.shearPermille))>1000)
            throw std::invalid_argument("Terra alpine profile");
        const int64_t dx=x-p.xCm+(z-p.zCm)*p.shearPermille/1000,dz=z-p.zCm;
        if(std::abs(dx)>p.radiusXcm||std::abs(dz)>p.radiusZcm)continue;
        const int64_t nx=dx*65536/p.radiusXcm,nz=dz*65536/p.radiusZcm;
        const auto r=fxSqrt(Fixed::fromRaw(nx*nx/65536+nz*nz/65536)).raw;
        if(r>=65536)continue;
        // Narrow ridgelines, unequal flanks and nested gullies, not circular domes.
        int64_t ridge=0,weight=32768;
        for(int i=0;i<5;++i){
            const auto n=std::abs(detail::noise(x+dz/3,z-dx/5,std::max(150,6000>>i),terrain.seed+uint64_t(i)*7919));
            ridge+=(65536-n)*weight/65536;weight/=2;
        }
        const auto envelope=65536-r;
        const auto relief=int64_t(p.heightCm)*envelope/65536*(35000+ridge/2)/65536;
        mountain=std::max(mountain,relief);
    }
    if(valleyHalfWidthCm<0||valleyHalfWidthCm>100000||valleyBlendCm<100||valleyBlendCm>100000)throw std::invalid_argument("Terra alpine valley");
    if(valleyHalfWidthCm){const int64_t t=std::clamp<int64_t>((std::abs(x)-valleyHalfWidthCm)*65536/valleyBlendCm,0,65536);const auto blend=t*t/65536*(196608-2*t)/65536;mountain=mountain*blend/65536;}
    return int32_t(std::clamp<int64_t>(int64_t(heightAtCm(terrain,x,z))+mountain,-1000000,1000000));
}
} // namespace aiverse::terraweave
