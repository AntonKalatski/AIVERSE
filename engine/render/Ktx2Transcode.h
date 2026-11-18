// Optional KTX2 BasisLZ/UASTC transcoder bridge for native game importers.
// Basis Universal is NOT embedded; an app supplies a licensed codec callback.
// Validate returned dimensions, slices, and byte ranges before GPU upload.
#pragma once
#include "TextureAsset.h"
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>

namespace aiverse::render {

using Ktx2Transcoder = std::function<bool(std::string_view encoded,
                                         CompressedImage& decoded,
                                         std::string& error)>;

inline bool validateTranscodedImage(const CompressedImage& image,
                                   std::string& error) {
    auto fail = [&](const char* message) { error=message; return false; };
    if (image.width < 1 || image.height < 1 || image.depth < 1 ||
        image.width > 16384 || image.height > 16384 || image.depth > 16384 ||
        image.arrayLayers < 1 || image.arrayLayers > 2048 ||
        (image.faces != 1 && image.faces != 6) ||
        (image.threeDimensional && (image.oneDimensional || image.faces != 1 ||
                                    image.arrayLayers != 1)) ||
        (image.oneDimensional && (image.height != 1 || image.depth != 1 ||
                                  image.faces != 1 || image.arrayLayers != 1)) ||
        (image.faces == 6 && (image.width != image.height || image.depth != 1)) ||
        (image.depth > 1 && (image.arrayLayers != 1 || image.faces != 1)) ||
        image.arrayLayers > std::numeric_limits<int>::max() / image.faces ||
        image.levels.empty())
        return fail("KTX2 transcoder produced invalid dimensions/layers");
    const int mipLimit = gfx::maxTextureMipLevels(
        image.width,image.height,image.depth);
    if (image.levels.size() > size_t(mipLimit) || image.levels.size() > 31)
        return fail("KTX2 transcoder produced too many mip levels");
    constexpr size_t maxLevel = size_t(512) << 20;
    for (size_t mip=0;mip<image.levels.size();++mip) {
        const size_t slice=levelByteSize(image.format,levelWidth(image,int(mip)),
                                        levelHeight(image,int(mip)));
        const int count=levelImageCount(image,int(mip));
        if (slice == 0 || count < 1 || slice > maxLevel/size_t(count) ||
            image.levels[mip].size() < slice*size_t(count))
            return fail("KTX2 transcoder produced truncated or oversized mip data");
    }
    error.clear();
    return true;
}

// Ordinary KTX2 continues through parseKtx2 unchanged. Basis input with
// vkFormat zero (ETC1S/UASTC) or BasisLZ supercompression requires a callback.
// The callback must produce complete mip-major data with all cube/array faces.
inline bool parseKtx2WithTranscoder(std::string_view encoded,
                                   const Ktx2Transcoder& transcoder,
                                   CompressedImage& output,
                                   std::string& error) {
    output = {};
    error.clear();
    constexpr size_t maxContainer = size_t(512) << 20;
    if (encoded.size() < 80 || encoded.size() > maxContainer ||
        !isKtx2(std::string(encoded.substr(0,12)))) {
        error = "KTX2 transcoder: invalid signature or container budget";
        return false;
    }
    auto read32 = [&](size_t offset) {
        return uint32_t(uint8_t(encoded[offset])) |
               (uint32_t(uint8_t(encoded[offset+1]))<<8) |
               (uint32_t(uint8_t(encoded[offset+2]))<<16) |
               (uint32_t(uint8_t(encoded[offset+3]))<<24);
    };
    const bool basis = read32(12) == 0 || read32(44) == 1;
    if (!basis) {
        if (!parseKtx2(std::string(encoded),output,error)) return false;
        return validateTranscodedImage(output,error);
    }
    if (!transcoder) {
        error = "KTX2 ETC1S/UASTC requires an installed Basis Universal transcoder";
        return false;
    }
    if (!transcoder(encoded,output,error)) {
        output = {};
        if (error.empty()) error = "KTX2 Basis transcoder failed";
        return false;
    }
    if (!validateTranscodedImage(output,error)) {
        output = {};
        return false;
    }
    return true;
}

// Combined decode/transcode+upload. Caller owns the returned GPU handle.
inline gfx::TextureHandle uploadKtx2WithTranscoder(
        gfx::Device& device, std::string_view encoded,
        const Ktx2Transcoder& transcoder, const TextureUploadOptions& options,
        std::string& error) {
    CompressedImage image;
    if (!parseKtx2WithTranscoder(encoded,transcoder,image,error)) return {};
    return uploadContainerTexture(device,image,options,error);
}

} // namespace aiverse::render
