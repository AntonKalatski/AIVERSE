// GPU uploader for parsed KTX2 / DDS arrays, cubes and volumes.
// Keeps the existing Model sampler2D path unchanged. This helper belongs to
// rendering/presentation; never use GPU textures as deterministic ECS state.
#pragma once
#include "TextureCodec.h"
#include "engine/gfx/Gfx.h"

#include <algorithm>

#include <string>
#include <vector>

namespace aiverse::render {
struct TextureUploadOptions {
    bool trustShaderSrgbDecode = false; // compressed sRGB UNORM formats need shader decode
    bool forceCpuDecode = false;        // for debugging and GPU compression fallback
    bool shaderReconstructsBc5NormalZ = false; // opt-in; BC5 native sampling returns RG only
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
        (in.depth > 1 && (in.faces != 1 || in.arrayLayers != 1))) {
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
    td.dimension = in.depth > 1 ? gfx::TextureDimension::Texture3D :
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
        if (in.vkFormat != 131 && in.vkFormat != 132) compressed = gfx::PixelFormat::BC1;
        break;
    case BlockFormat::BC3: compressed = gfx::PixelFormat::BC3; break;
    case BlockFormat::BC4: compressed = gfx::PixelFormat::BC4; break;
    case BlockFormat::BC5: compressed = gfx::PixelFormat::BC5; break;
    case BlockFormat::EAC_R11: compressed = gfx::PixelFormat::EAC_R11; break;
    case BlockFormat::BC7: compressed = gfx::PixelFormat::BC7; break;
    case BlockFormat::ETC2_RGB8: compressed = gfx::PixelFormat::ETC2_RGB8; break;
    case BlockFormat::ETC2_RGBA8: compressed = gfx::PixelFormat::ETC2_RGBA8; break;
    case BlockFormat::ASTC_4x4: compressed = gfx::PixelFormat::ASTC_4x4; break;
    case BlockFormat::ASTC_6x6: compressed = gfx::PixelFormat::ASTC_6x6; break;
    case BlockFormat::None:
        break; // rgba8 raw payload
    }
    const bool native = !opt.forceCpuDecode &&
        compressed != gfx::PixelFormat::RGBA8 &&
        (in.depth == 1 && !in.oneDimensional) &&
        (!in.srgb || opt.trustShaderSrgbDecode) &&
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

    // Decode each 2D slice of each mip into packed RGBA8, keeping the original
    // layer/face/z order so native array and volume uploads preserve all images.
    std::vector<std::vector<uint8_t>> decoded(in.levels.size());
    for (size_t level = 0; level < in.levels.size(); ++level) {
        const int count = levelImageCount(in, int(level));
        const size_t stride = size_t(levelWidth(in, int(level))) *
                              size_t(levelHeight(in, int(level))) * 4;
        if (stride > (size_t(512) << 20) / size_t(count)) {
            error = "decoded texture mip exceeds memory budget";
            return {};
        }
        auto& output = decoded[level];
        output.resize(stride * size_t(count));
        for (int slice = 0; slice < count; ++slice) {
            std::vector<uint8_t> rgba;
            if (!decodeBlocksSlice(in, int(level), slice, rgba) ||
                rgba.size() != stride) {
                error = "container CPU fallback unavailable for this format (ASTC requires native GPU)";
                return {};
            }
            std::copy(rgba.begin(), rgba.end(),
                      output.begin() + std::ptrdiff_t(stride * size_t(slice)));
        }
    }
    // The native attempt may have filled the same descriptor with compressed
    // levelData and levelCount. Clear that upload contract before creating the
    // uncompressed fallback: Vulkan accepts explicit levelData for *any*
    // format and would otherwise interpret BC bytes as RGBA pixels. Metal and
    // GL would also lose the authored mip allocation when mipLevels stayed 0.
    td.format = in.srgb ? gfx::PixelFormat::RGBA8_sRGB : gfx::PixelFormat::RGBA8;
    td.levelCount = 0;
    td.levelData = nullptr;
    td.levelSize = nullptr;
    td.mipLevels = int(in.levels.size());
    td.data = decoded[0].data();
    gfx::TextureHandle tex = dev.createTexture(td);
    if (!tex.valid()) {
        error = dev.lastError();
        if (error.empty()) error = "RHI could not allocate texture container image";
        return {};
    }
    // The initial allocation consumes level zero; upload remaining manually
    // to preserve authored mips rather than regenerating them from level zero.
    for (size_t level = 1; level < decoded.size(); ++level) {
        const int width = levelWidth(in, int(level));
        const int height = levelHeight(in, int(level));
        const int slices = levelImageCount(in, int(level));
        const size_t stride = size_t(width) * size_t(height) * 4;
        for (int slice = 0; slice < (in.depth > 1 ? 1 : slices); ++slice) {
            gfx::TextureUploadDesc u;
            u.mipLevel = int(level);
            u.arrayLayer = slice;
            u.width = width;
            u.height = height;
            u.depth = in.depth > 1 ? levelDepth(in, int(level)) : 1;
            u.rowPitchBytes = width * 4;
            u.slicePitchBytes = width * height * 4;
            u.data = decoded[level].data() + stride * size_t(slice);
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
