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
    std::printf("Texture descriptor contract: %d failures\n", failures);
    return failures ? 1 : 0;
}
