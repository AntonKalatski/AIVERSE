#pragma once
#include "Math3D.h"
#include "../gfx/Gfx.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <array>
#include <cstdint>

namespace aiverse::render {
struct PostFrame;
// Cosmetic world-space data. Never authoritative visibility or PHYSIS state.
enum class FogShape { Sphere, Obb };
struct FogRegion {
    FogShape shape=FogShape::Sphere;
    float3 center{};
    float radius=10;
    float3 halfExtents{10,10,10};
    quat orientation{};
    float feather=1; // inward edge fade, world units
};
struct FogVolume {
    uint32_t id=0; // nonzero, unique within fog volumes
    FogRegion region;
    float density=.1f; // extinction per world unit
    float3 color{.8f,.85f,.9f}; // linear scattering albedo
};
struct FogReveal {
    uint32_t id=0; // separate namespace from fog volumes
    FogRegion region;
    float strength=1; // 0 unchanged, 1 fully clear
};
struct LocalFogSettings {
    bool enabled=false;
    float maxDistance=1000, ambient=.5f, anisotropy=.3f;
};
float fogRegionWeight(const FogRegion&,float3 point);
// Validated, bounded runtime ownership. Failed edits leave previous state intact.
class LocalFogScene {
public:
    static constexpr size_t capacity=8;
    bool upsert(FogVolume);
    bool upsert(FogReveal);
    bool removeVolume(uint32_t);
    bool removeReveal(uint32_t);
    void clear(){*this={};}
    const auto& volumes()const{return volumes_;}
    const auto& reveals()const{return reveals_;}
    float densityAt(float3)const;
private:
    std::array<FogVolume,capacity> volumes_{};
    std::array<FogReveal,capacity> reveals_{};
};
class LocalFog {
public:
    void shutdown(gfx::Device&);
    bool draw(gfx::Device&,const PostFrame&,const LocalFogSettings&,const LocalFogScene&,int quality,
              gfx::TextureHandle source,gfx::TextureHandle depth,gfx::TextureHandle target,
              gfx::TextureHandle scratchDepth,gfx::BufferHandle triangle);
    const std::string& lastError()const{return error_;}
private:
    gfx::ShaderHandle shader_;
    gfx::PipelineHandle pipe_;
    bool failed_=false;
    std::string error_;
};
}
