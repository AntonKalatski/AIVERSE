// GPU uploader for parsed KTX2 / DDS arrays, cubes and volumes.
// Keeps the existing Model sampler2D path unchanged. This helper belongs to
// rendering/presentation; never use GPU textures as deterministic ECS state.
#pragma once
#include "TextureCodec.h"
#include "engine/gfx/Gfx.h"

#include <algorithm>
#include <cstddef>
#include <limits>

#include <string>
#include <vector>

namespace aiverse::render {
struct TextureUploadOptions {
    bool trustShaderSrgbDecode = false; // compressed sRGB UNORM formats need shader decode
    bool forceCpuDecode = false;        // for debugging and GPU compression fallback
    bool shaderReconstructsBc5NormalZ = false; // opt-in; BC5 native sampling returns RG only
    // Upper bound on all retained RGBA8 mip/face/layer bytes during CPU
    // fallback. The native compressed path does not consume this budget.
    // Zero disables CPU fallback; enlarge explicitly for large trusted assets.
    size_t maxCpuDecodedBytes = size_t(512) << 20;
};

// TextureHandle is owned by the caller. Returns invalid and sets error on
// unsupported dimensions/formats, corrupt input, or GPU allocation/update failure.
// Container blocks and CPU data remain owned by CompressedImage; all data is consumed
// synchronously before returning. Anisotropy/sampler state is configured separately.
inline gfx::TextureHandle uploadContainerTexture(gfx::Device& dev, const CompressedImage& in,
                                           const TextureUploadOptions& opt, std::string& error) {
    error.clear();
    if (in.width < 1 || in.height < 1 || in.depth < 1 ||
        in.arrayLayers < 1 || (in.faces != 1 && in.faces != 6) ||
        in.levels.empty() || in.levels.size() > 31 ||
        (in.faces == 6 && (in.width != in.height || in.depth != 1)) ||
        (in.depth > 1 && (in.faces != 1 || in.arrayLayers != 1)) ||
        (in.threeDimensional && (in.oneDimensional || in.faces != 1 ||
                                 in.arrayLayers != 1)) ||
        // Multiplication below is in signed int: validate it before use.
        in.arrayLayers > std::numeric_limits<int>::max() / in.faces ||
        // A manually constructed malformed 1D resource must not turn into 3D.
        (in.oneDimensional && (in.height != 1 || in.depth != 1 ||
                               in.arrayLayers != 1 || in.faces != 1))) {
        error = "invalid texture container dimensions or mip metadata";
        return {};
    }
    gfx::TextureDesc td;
    td.width = in.width;
    td.height = in.height;
    td.depth = in.depth;
    td.arrayLayers = in.arrayLayers * in.faces;
    td.mipLevels = int(in.levels.size());
    td.filter = gfx::Filter::Linear;
    td.dimension = (in.threeDimensional || in.depth > 1) ?
                   gfx::TextureDimension::Texture3D :
                   in.oneDimensional ? gfx::TextureDimension::Texture1D :
                   in.faces == 6 ? (in.arrayLayers == 1 ? gfx::TextureDimension::Cube :
                                                          gfx::TextureDimension::CubeArray) :
                   in.arrayLayers > 1 ? gfx::TextureDimension::Texture2DArray :
                                        gfx::TextureDimension::Texture2D;
    if (gfx::validateTextureDesc(td) != gfx::TextureDescError::None) {
        error = "container dimensions are not representable by the RHI";
        return {};
    }
    for (size_t l = 0; l < in.levels.size(); ++l) {
        const size_t slice = levelByteSize(in.format, levelWidth(in, int(l)),
                                            levelHeight(in, int(l)));
        const size_t count = size_t(levelImageCount(in, int(l)));
        if (!slice || !count || slice > in.levels[l].size() / count) {
            error = "truncated array/cubemap/volume mip payload";
            return {};
        }
    }
    gfx::PixelFormat compressed = gfx::PixelFormat::RGBA8;
    switch (in.format) {
    case BlockFormat::BC1:
        // BC1_RGB's transparent-index semantics differ on GPUs using BC1_RGBA.
        if (in.vkFormat != 131 && in.vkFormat != 132)
            compressed = in.srgb ? gfx::PixelFormat::BC1_sRGB : gfx::PixelFormat::BC1;
        break;
    case BlockFormat::BC3: compressed = in.srgb ? gfx::PixelFormat::BC3_sRGB : gfx::PixelFormat::BC3; break;
    case BlockFormat::BC4: compressed = gfx::PixelFormat::BC4; break;
    case BlockFormat::BC5: compressed = gfx::PixelFormat::BC5; break;
    case BlockFormat::EAC_R11: compressed = gfx::PixelFormat::EAC_R11; break;
    case BlockFormat::BC7: compressed = in.srgb ? gfx::PixelFormat::BC7_sRGB : gfx::PixelFormat::BC7; break;
    case BlockFormat::BC6H_UFloat: compressed = gfx::PixelFormat::BC6H_UFloat; break;
    case BlockFormat::BC6H_SFloat: compressed = gfx::PixelFormat::BC6H_SFloat; break;
    case BlockFormat::ETC2_RGB8: compressed = in.srgb ? gfx::PixelFormat::ETC2_RGB8_sRGB : gfx::PixelFormat::ETC2_RGB8; break;
    case BlockFormat::ETC2_RGBA8: compressed = in.srgb ? gfx::PixelFormat::ETC2_RGBA8_sRGB : gfx::PixelFormat::ETC2_RGBA8; break;
    case BlockFormat::ASTC_4x4: compressed = in.srgb ? gfx::PixelFormat::ASTC_4x4_sRGB : gfx::PixelFormat::ASTC_4x4; break;
    case BlockFormat::ASTC_6x6: compressed = in.srgb ? gfx::PixelFormat::ASTC_6x6_sRGB : gfx::PixelFormat::ASTC_6x6; break;
    case BlockFormat::None:
        break; // rgba8 raw payload
    }
    const bool native = !opt.forceCpuDecode &&
        compressed != gfx::PixelFormat::RGBA8 &&
        (in.depth == 1 && !in.oneDimensional && !in.threeDimensional) &&
        (!in.srgb || compressed == gfx::PixelFormat::BC1_sRGB ||
         compressed == gfx::PixelFormat::BC3_sRGB ||
         compressed == gfx::PixelFormat::BC7_sRGB ||
         compressed == gfx::PixelFormat::ETC2_RGB8_sRGB ||
         compressed == gfx::PixelFormat::ETC2_RGBA8_sRGB ||
         compressed == gfx::PixelFormat::ASTC_4x4_sRGB ||
         compressed == gfx::PixelFormat::ASTC_6x6_sRGB ||
         opt.trustShaderSrgbDecode) &&
        (in.format != BlockFormat::BC5 || opt.shaderReconstructsBc5NormalZ) &&
        dev.supports(compressed);

    if (native) {
        std::vector<const void*> ptrs;
        std::vector<size_t> sizes;
        for (const auto& mip : in.levels) {
            ptrs.push_back(mip.data());
            sizes.push_back(mip.size());
        }
        td.format = compressed;
        td.mipLevels = 0;            // levelCount supplies the compressed mip chain
        td.levelCount = int(ptrs.size());
        td.levelData = ptrs.data();
        td.levelSize = sizes.data();
        gfx::TextureHandle tex = dev.createTexture(td);
        if (tex.valid()) return tex;
        // On an unsupported native compressed allocation try the CPU fallback,
        // except ASTC which intentionally has no CPU decoder in this engine.
    }

    // Preflight the FULL decoded mip chain before allocating any RGBA8
    // image. Per-mip limits alone allow many individually legal levels to
    // exhaust memory together. Division-based checks avoid size_t overflow
    // for malformed caller-constructed container metadata.
    constexpr size_t kMaxDecodedMipBytes = size_t(512) << 20;
    std::vector<size_t> decodedMipBytes;
    decodedMipBytes.reserve(in.levels.size());
    size_t totalDecodedBytes = 0;
    for (size_t level = 0; level < in.levels.size(); ++level) {
        const int images = levelImageCount(in, int(level));
        const int width = levelWidth(in, int(level));
        const int height = levelHeight(in, int(level));
        if (images < 1 || width < 1 || height < 1) {
            error = "invalid decoded texture mip geometry";
            return {};
        }
        const size_t w = size_t(width), h = size_t(height);
        if (w > kMaxDecodedMipBytes / 4 ||
            h > (kMaxDecodedMipBytes / 4) / w ||
            w * h * 4 > kMaxDecodedMipBytes / size_t(images)) {
            error = "decoded texture mip exceeds memory budget";
            return {};
        }
        const size_t bytes = w * h * 4 * size_t(images);
        if (totalDecodedBytes > opt.maxCpuDecodedBytes ||
            bytes > opt.maxCpuDecodedBytes - totalDecodedBytes) {
            error = "decoded texture chain exceeds CPU fallback byte budget";
            return {};
        }
        totalDecodedBytes += bytes;
        decodedMipBytes.push_back(bytes);
    }

    // The native attempt may have filled the descriptor with compressed
    // levelData and levelCount. Clear that upload contract BEFORE creating
    // the uncompressed fallback, keeping the authored mip count.
    td.format = in.srgb ? gfx::PixelFormat::RGBA8_sRGB : gfx::PixelFormat::RGBA8;
    td.levelCount = 0;
    td.levelData = nullptr;
    td.levelSize = nullptr;
    td.mipLevels = int(in.levels.size());
    td.data = nullptr;

    // Decode only one entire mip at a time. The base level is consumed
    // synchronously by createTexture; subsequent levels are consumed by
    // updateTextureRegion. Holding all decoded mips simultaneously could
    // multiply peak RAM usage despite the logical chain budget above.
    gfx::TextureHandle tex{};
    std::vector<uint8_t> decodedMip;
    for (size_t level = 0; level < in.levels.size(); ++level) {
        const int width = levelWidth(in, int(level));
        const int height = levelHeight(in, int(level));
        const int slices = levelImageCount(in, int(level));
        const size_t stride = size_t(width) * size_t(height) * 4;
        decodedMip.resize(decodedMipBytes[level]);
        for (int slice = 0; slice < slices; ++slice) {
            std::vector<uint8_t> rgba;
            if (!decodeBlocksSlice(in, int(level), slice, rgba) ||
                rgba.size() != stride) {
                error = "container CPU fallback unavailable for this format (ASTC/BC6H require native GPU)";
                if (tex.valid()) dev.destroyTexture(tex);
                return {};
            }
            std::copy(rgba.begin(), rgba.end(),
                      decodedMip.begin() + std::ptrdiff_t(stride * size_t(slice)));
        }

        if (level == 0) {
            // Each native backend consumes all base faces/layers/volume slices
            // synchronously; the source buffer can be reused for the next mip.
            td.data = decodedMip.data();
            tex = dev.createTexture(td);
            td.data = nullptr;
            if (!tex.valid()) {
                error = dev.lastError();
                if (error.empty()) error = "RHI could not allocate texture container image";
                return {};
            }
            continue;
        }

        // Keep authored mipmaps rather than generating from level zero.
        // One 3D upload spans the depth slices; arrays/cubemaps upload each
        // physical layer/face separately.
        for (int slice = 0; slice < (in.depth > 1 ? 1 : slices); ++slice) {
            gfx::TextureUploadDesc u;
            u.mipLevel = int(level);
            u.arrayLayer = slice;
            u.width = width;
            u.height = height;
            u.depth = in.depth > 1 ? levelDepth(in, int(level)) : 1;
            u.rowPitchBytes = width * 4;
            u.slicePitchBytes = width * height * 4;
            u.data = decodedMip.data() + stride * size_t(slice);
            if (!dev.updateTextureRegion(tex, u)) {
                error = dev.lastError();
                if (error.empty()) error = "container mip or subresource GPU upload failed";
                dev.destroyTexture(tex);
                return {};
            }
        }
    }
    return tex;
}
inline gfx::TextureHandle uploadKtx2Image(gfx::Device& device, const CompressedImage& image,
                                           const TextureUploadOptions& options, std::string& error) {
    return uploadContainerTexture(device, image, options, error);
}
inline gfx::TextureHandle uploadDdsImage(gfx::Device& device, const CompressedImage& image,
                                         const TextureUploadOptions& options, std::string& error) {
    return uploadContainerTexture(device, image, options, error);
}

} // namespace aiverse::render
