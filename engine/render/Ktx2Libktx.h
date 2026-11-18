// Optional production KTX2 ETC1S/BasisLZ + UASTC LDR decoding using Khronos
// libktx. No vendored codec: enable AIVERSE_TEXTURE_WITH_LIBKTX in CMake and
// link the actual libktx library. Never interpret an unsupported block format
// as raw pixels. All output still passes the Ktx2Transcode.h layout validator.
#pragma once

#include "Ktx2Transcode.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#if defined(AIVERSE_TEXTURE_WITH_LIBKTX)
#include <ktx.h>
#endif

namespace aiverse::render {

struct Ktx2LibktxOptions {
    bool preferNativeCompression = true;
    // Set false for assets that must remain GPU compressed. If no compatible
    // transcode target exists, the decoder fails rather than dropping alpha.
    bool allowRgba8Fallback = true;
};

#if defined(AIVERSE_TEXTURE_WITH_LIBKTX)
namespace ktx2_libktx_detail {
struct Target {
    ktx_transcode_fmt_e transcode;
    BlockFormat format;
    uint32_t vkLinear = 0, vkSrgb = 0;
};
inline bool choose(const gfx::Device& device, bool srgb,
                   Ktx2LibktxOptions options, Target& target) {
    if (options.preferNativeCompression) {
        const struct Choice {
            gfx::PixelFormat linear,srgb;
            ktx_transcode_fmt_e transcode;
            BlockFormat format;
            uint32_t vkLinear,vkSrgb;
        } candidates[] = {
            {gfx::PixelFormat::BC7,gfx::PixelFormat::BC7_sRGB,
             KTX_TTF_BC7_RGBA,BlockFormat::BC7,145u,146u},
            {gfx::PixelFormat::ASTC_4x4,gfx::PixelFormat::ASTC_4x4_sRGB,
             KTX_TTF_ASTC_4x4_RGBA,BlockFormat::ASTC_4x4,157u,158u},
            {gfx::PixelFormat::ETC2_RGBA8,gfx::PixelFormat::ETC2_RGBA8_sRGB,
             KTX_TTF_ETC2_RGBA,BlockFormat::ETC2_RGBA8,151u,152u},
            {gfx::PixelFormat::BC3,gfx::PixelFormat::BC3_sRGB,
             KTX_TTF_BC3_RGBA,BlockFormat::BC3,137u,138u}
        };
        for (const auto& c : candidates) {
            if (!device.supports(srgb ? c.srgb : c.linear)) continue;
            target = {c.transcode,c.format,c.vkLinear,c.vkSrgb};
            return true;
        }
    }
    if (!options.allowRgba8Fallback) return false;
    // RGBA32 is a Basis transcoding target name for RGBA8 texels.
    target = {KTX_TTF_RGBA32,BlockFormat::None,37u,43u};
    return true;
}
} // namespace ktx2_libktx_detail
#endif

// The caller must keep device alive until parseKtx2WithTranscoder or
// uploadKtx2WithTranscoder returns. Callback execution is synchronous.
inline Ktx2Transcoder makeLibktxBasisTranscoder(
        const gfx::Device& device, Ktx2LibktxOptions options = {}) {
#if defined(AIVERSE_TEXTURE_WITH_LIBKTX)
    return [&device,options](std::string_view encoded,
                             CompressedImage& out,std::string& error)->bool {
        out={};
        error.clear();
        if (encoded.empty() ||
            encoded.size() > (size_t(512)<<20) ||
            encoded.size() > std::numeric_limits<ktx_size_t>::max()) {
            error="libktx: invalid or excessive encoded KTX2 size";
            return false;
        }
        ktxTexture2* raw=nullptr;
        const auto result=ktxTexture2_CreateFromMemory(
            reinterpret_cast<const ktx_uint8_t*>(encoded.data()),
            ktx_size_t(encoded.size()),KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,&raw);
        if (result!=KTX_SUCCESS || !raw) {
            if(raw)ktxTexture2_Destroy(raw);
            error="libktx: could not parse Basis/UASTC KTX2 image";
            return false;
        }
        std::unique_ptr<ktxTexture2,decltype(&ktxTexture2_Destroy)>
            tex(raw,&ktxTexture2_Destroy);
        if (!ktxTexture2_NeedsTranscoding(tex.get())) {
            error="libktx: KTX2 image does not require Basis transcoding";
            return false;
        }

        // The bridge validates the original source topology independently.
        const bool srgb=ktxTexture2_GetOETF(tex.get())==KHR_DF_TRANSFER_SRGB;
        ktx2_libktx_detail::Target target{};
        if (!ktx2_libktx_detail::choose(device,srgb,options,target)) {
            error="libktx: no compatible compressed GPU format and RGBA8 fallback disabled";
            return false;
        }
        if(ktxTexture2_TranscodeBasis(tex.get(),target.transcode,0)!=KTX_SUCCESS) {
            error="libktx: BasisLZ/UASTC transcode failed (unsupported source or target)";
            return false;
        }
        const ktxTexture* base=reinterpret_cast<ktxTexture*>(tex.get());
        const ktx_uint8_t* bytes=ktxTexture_GetData(
            const_cast<ktxTexture*>(base));
        const size_t dataSize=size_t(ktxTexture_GetDataSize(
            const_cast<ktxTexture*>(base)));
        if(!bytes || !dataSize || dataSize>(size_t(512)<<20)) {
            error="libktx: transcoded KTX2 image data missing or too large";
            return false;
        }

        CompressedImage decoded;
        decoded.width=int(tex->baseWidth);
        decoded.height=int(tex->baseHeight ? tex->baseHeight : 1);
        decoded.depth=int(tex->baseDepth ? tex->baseDepth : 1);
        decoded.arrayLayers=int(tex->numLayers ? tex->numLayers : 1);
        decoded.faces=int(tex->numFaces);
        decoded.oneDimensional=tex->numDimensions==1;
        decoded.threeDimensional=tex->numDimensions==3;
        decoded.srgb=srgb;
        decoded.format=target.format;
        decoded.vkFormat=srgb ? target.vkSrgb : target.vkLinear;
        decoded.levels.resize(size_t(tex->numLevels));
        if (decoded.width<1 || decoded.width>16384 ||
            decoded.height<1 || decoded.height>16384 ||
            decoded.depth<1 || decoded.depth>16384 ||
            decoded.arrayLayers<1 || decoded.arrayLayers>2048 ||
            (decoded.faces!=1 && decoded.faces!=6) ||
            decoded.levels.empty() || decoded.levels.size()>31) {
            error="libktx: transcoded texture dimensions are unsupported";
            return false;
        }

        size_t total=0;
        for(size_t mip=0;mip<decoded.levels.size();++mip) {
            const size_t one=levelByteSize(
                decoded.format,levelWidth(decoded,int(mip)),
                levelHeight(decoded,int(mip)));
            const int depth=levelDepth(decoded,int(mip));
            const size_t count=size_t(decoded.arrayLayers) *
                               size_t(decoded.faces) * size_t(depth);
            if(!one || !count || one>(size_t(512)<<20)/count ||
               total>(size_t(512)<<20)-one*count) {
                error="libktx: transcoded mip payload memory budget exceeded";
                return false;
            }
            const size_t required=one*count;
            auto& level=decoded.levels[mip];
            level.reserve(required);
            for(int layer=0;layer<decoded.arrayLayers;++layer)
                for(int face=0;face<decoded.faces;++face)
                    for(int slice=0;slice<depth;++slice) {
                        // For 3D KTX2 the faceSlice argument is the z-slice;
                        // for arrays/cubes it is the face index.
                        const uint32_t faceSlice=uint32_t(
                            decoded.threeDimensional ? slice : face);
                        ktx_size_t offset=0;
                        if(ktxTexture2_GetImageOffset(
                               tex.get(),ktx_uint32_t(mip),
                               ktx_uint32_t(layer),faceSlice,&offset)!=KTX_SUCCESS ||
                           size_t(offset)>dataSize ||
                           one>dataSize-size_t(offset)) {
                            error="libktx: invalid transcoded mip/face offset";
                            return false;
                        }
                        const auto* start=bytes+size_t(offset);
                        level.insert(level.end(),start,start+one);
                    }
            total+=required;
        }
        out=std::move(decoded);
        return true;
    };
#else
    (void)device;(void)options;
    return [](std::string_view,CompressedImage& out,std::string& error) {
        out={};
        error="KTX2 libktx codec is unavailable: enable AIVERSE_TEXTURE_WITH_LIBKTX";
        return false;
    };
#endif
}

// Uses the exact existing source-header and post-transcode topology checks.
// Native GPU formats are chosen only when Device::supports() confirms them.
inline gfx::TextureHandle uploadKtx2WithLibktx(
    gfx::Device& device,std::string_view encoded,
    const TextureUploadOptions& upload,std::string& error,
    Ktx2LibktxOptions options={}) {
    return uploadKtx2WithTranscoder(device,encoded,
             makeLibktxBasisTranscoder(device,options),upload,error);
}

} // namespace aiverse::render
