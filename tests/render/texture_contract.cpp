#include "engine/gfx/Gfx.h"
#include <cstdio>

using namespace aiverse::gfx;
namespace {
int failures = 0;
void check(bool yes, const char* what) {
    if (!yes) { ++failures; std::printf("FAIL: %s\n", what); }
}
TextureDesc size2D(int w = 128, int h = 64) {
    TextureDesc d;
    d.width = w; d.height = h;
    return d;
}
}
int main() {
    auto d = size2D();
    check(validateTextureDesc(d) == TextureDescError::None, "legacy 2D");
    check(d.dimension == TextureDimension::Texture2D && d.depth == 1 &&
          d.arrayLayers == 1 && d.sampleCount == 1 && d.mipLevels == 0, "legacy defaults");
    d.width = 0;
    check(validateTextureDesc(d) == TextureDescError::InvalidExtent, "zero width");
    d = size2D(); d.dimension = TextureDimension::Texture1D; d.height = 1;
    check(validateTextureDesc(d) == TextureDescError::None, "1D descriptor");
    d.height = 2;
    check(validateTextureDesc(d) == TextureDescError::InvalidDimension, "1D rejects height");
    d = size2D(); d.dimension = TextureDimension::Texture3D; d.depth = 9;
    check(validateTextureDesc(d) == TextureDescError::None, "3D descriptor");
    d.arrayLayers = 2;
    check(validateTextureDesc(d) == TextureDescError::InvalidDimension, "3D disallows array layers");
    d = size2D(); d.dimension = TextureDimension::Texture2DArray; d.arrayLayers = 9;
    check(validateTextureDesc(d) == TextureDescError::None, "2DArray descriptor");
    d = size2D(128, 128); d.dimension = TextureDimension::Cube; d.arrayLayers = 6;
    check(validateTextureDesc(d) == TextureDescError::None, "cube descriptor");
    d.arrayLayers = 5;
    check(validateTextureDesc(d) == TextureDescError::InvalidDimension, "cube requires six faces");
    d = size2D(128, 128); d.dimension = TextureDimension::CubeArray; d.arrayLayers = 12;
    check(validateTextureDesc(d) == TextureDescError::None, "cube array descriptor");
    d.arrayLayers = 7;
    check(validateTextureDesc(d) == TextureDescError::InvalidDimension, "cube array layer count");
    d = size2D(); d.mipLevels = maxTextureMipLevels(d.width, d.height) + 1;
    check(validateTextureDesc(d) == TextureDescError::InvalidMipCount, "mip cap");
    d = size2D(); d.renderTarget = true; d.sampleCount = 4;
    check(validateTextureDesc(d) == TextureDescError::None, "MSAA render target descriptor");
    d.mipmaps = true;
    check(validateTextureDesc(d) == TextureDescError::InvalidUsage, "MSAA mipmaps rejected");
    d = size2D(); d.compare = true;
    check(validateTextureDesc(d) == TextureDescError::InvalidUsage, "depth comparison needs depth");
    d.format = PixelFormat::Depth;
    check(validateTextureDesc(d) == TextureDescError::None, "depth comparison");
    d.storage = true;
    check(validateTextureDesc(d) == TextureDescError::InvalidUsage, "depth storage rejected");
    // View subresources must remain in the parent's mip/layer bounds.
    d = size2D(64,64); d.mipLevels = 7;
    TextureViewDesc v; v.baseMipLevel = 1; v.mipLevelCount = 4;
    check(validateTextureViewDesc(d,v), "valid 2D mip range");
    v.mipLevelCount = 7;
    check(!validateTextureViewDesc(d,v), "invalid 2D mip range");
    d = size2D(64,64); d.dimension = TextureDimension::Texture2DArray; d.arrayLayers = 8;
    v = TextureViewDesc{}; v.dimension = TextureDimension::Texture2DArray;
    v.baseArrayLayer = 6; v.arrayLayerCount = 2;
    check(validateTextureViewDesc(d,v), "array view near end");
    v.arrayLayerCount = 3;
    check(!validateTextureViewDesc(d,v), "array view past end");
    v.dimension = TextureDimension::Texture2D; v.arrayLayerCount = 1;
    check(validateTextureViewDesc(d,v), "single array slice as 2D");
    d = size2D(64,64); d.dimension = TextureDimension::CubeArray; d.arrayLayers = 12;
    v = TextureViewDesc{}; v.dimension = TextureDimension::Cube;
    v.baseArrayLayer = 6; v.arrayLayerCount = 6;
    check(validateTextureViewDesc(d,v), "cube view from cube array");
    v.baseArrayLayer = 5;
    check(!validateTextureViewDesc(d,v), "cube view must align to six faces");
    d = size2D(); d.format = PixelFormat::Depth;
    v = TextureViewDesc{}; v.aspect = TextureAspect::Depth;
    check(validateTextureViewDesc(d,v), "depth aspect");
    v.aspect = TextureAspect::Color;
    check(!validateTextureViewDesc(d,v), "reject color view of depth");
    SamplerDesc sampler;
    check(validateSamplerDesc(sampler), "default sampler");
    sampler.maxLod = -1.0f;
    check(!validateSamplerDesc(sampler), "invalid sampler LOD");
    sampler.maxLod = 8.0f; sampler.maxAnisotropy = 0.0f;
    check(!validateSamplerDesc(sampler), "invalid anisotropy");

    PipelineDesc oldPipeline;
    check(oldPipeline.colorAttachmentCount == 1 &&
          oldPipeline.additionalColorFormats[0] == PixelFormat::RGBA8,
          "legacy pipeline retains one color output");
    RenderTargetPassDesc rp;
    check(!validateRenderTargetPassDesc(rp), "empty offscreen pass is invalid");
    rp.colorCount = 1; rp.colors[0].texture.id = 1;
    check(validateRenderTargetPassDesc(rp), "single color attachment");
    rp.colorCount = 2; rp.colors[1].texture.id = 2;
    check(validateRenderTargetPassDesc(rp), "two distinct color attachments");
    rp.colors[1].texture.id = 1;
    check(!validateRenderTargetPassDesc(rp), "reject simultaneous writes to same subresource");
    rp.colors[1].mipLevel = 1;
    check(validateRenderTargetPassDesc(rp), "distinct mip subresources are structurally legal");
    rp.depth.texture.id = 3; rp.clearDepth = 0.5f;
    check(validateRenderTargetPassDesc(rp), "color plus depth");
    rp.depth.texture.id = 1;
    check(!validateRenderTargetPassDesc(rp), "reject color and depth alias");
    rp.depth.texture.id = 3; rp.colorCount = 0;
    check(validateRenderTargetPassDesc(rp), "depth-only pass");
    rp.depth.mipLevel = -1;
    check(!validateRenderTargetPassDesc(rp), "reject invalid depth mip");
    rp.depth.mipLevel = 0; rp.clearDepth = 2.0f;
    check(!validateRenderTargetPassDesc(rp), "reject invalid depth clear value");
    rp = RenderTargetPassDesc{}; rp.colorCount = 5;
    check(!validateRenderTargetPassDesc(rp), "reject color count > 4");
    rp = RenderTargetPassDesc{}; rp.colorCount = 1; rp.colors[0].texture.id = 2;
    rp.colors[0].resolveTexture.id = 3;
    check(validateRenderTargetPassDesc(rp), "valid optional resolve descriptor");
    rp.colors[0].resolveTexture.id = 2;
    check(!validateRenderTargetPassDesc(rp), "reject resolve to same subresource");
    rp.colors[0].resolveTexture.id = 3; rp.colors[0].resolveMipLevel = -1;
    check(!validateRenderTargetPassDesc(rp), "reject negative resolve mip");
    rp.colors[0].resolveMipLevel = 0; rp.depth.resolveTexture.id = 4;
    check(!validateRenderTargetPassDesc(rp), "reject unsupported depth resolve");
    d = size2D(8,8); d.renderTarget = true; d.sampleCount = 4;
    check(validateTextureDesc(d) == TextureDescError::None, "MSAA color target");
    d.compare = true; d.format = PixelFormat::Depth;
    check(validateTextureDesc(d) == TextureDescError::InvalidUsage, "MSAA depth compare must be explicit");
    d.compare = false; d.format = PixelFormat::BC7;
    check(validateTextureDesc(d) == TextureDescError::InvalidUsage, "MSAA compressed texture disallowed");
    d.format = PixelFormat::RGBA8; d.sampleCount = 3;
    check(validateTextureDesc(d) == TextureDescError::InvalidSampleCount, "reject nonstandard MSAA samples");
    std::printf("Texture descriptor contract: %d failures\n", failures);
    return failures ? 1 : 0;
}
