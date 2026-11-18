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
#include <vector>

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


// Khronos libktx supports the UASTC_HDR -> RGBA_HALF transcoder target.
// This is intentionally separate from CompressedImage: its CPU fallback
// contract is RGBA8, which would lose all HDR range and sign information.
// Every mip and array/cube/volume slice is uploaded as linear RGBA16F.
inline gfx::TextureHandle uploadKtx2UastcHdrWithLibktx(
    gfx::Device& device,std::string_view encoded,std::string& error) {
    error.clear();
#if defined(AIVERSE_TEXTURE_WITH_LIBKTX)
    ktx2_bridge_detail::Source src;
    if (!ktx2_bridge_detail::inspect(encoded,src,error)) return {};
    if (!src.basis || encoded.size()>size_t(512)<<20 ||
        !device.supports(gfx::PixelFormat::RGBA16F)) {
        error="KTX2 HDR: invalid Basis source or RGBA16F GPU support unavailable";
        return {};
    }
    ktxTexture2* raw=nullptr;
    const KTX_error_code status=ktxTexture2_CreateFromMemory(
        reinterpret_cast<const ktx_uint8_t*>(encoded.data()),
        ktx_size_t(encoded.size()),KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,&raw);
    if (status!=KTX_SUCCESS || !raw) {
        if(raw)ktxTexture2_Destroy(raw);
        error="KTX2 HDR: libktx could not load the UASTC HDR container";
        return {};
    }
    std::unique_ptr<ktxTexture2,decltype(&ktxTexture2_Destroy)>
        texture(raw,&ktxTexture2_Destroy);
    if (!ktxTexture2_NeedsTranscoding(texture.get()) ||
        !ktxTexture2_IsHDR(texture.get()) ||
        ktxTexture2_GetOETF(texture.get())==KHR_DF_TRANSFER_SRGB) {
        error="KTX2 HDR: only linear UASTC_HDR Basis textures are accepted";
        return {};
    }
    if (ktxTexture2_TranscodeBasis(texture.get(),
                                   KTX_TTF_RGBA_HALF,0)!=KTX_SUCCESS) {
        error="KTX2 HDR: native UASTC_HDR to RGBA_HALF transcode failed";
        return {};
    }
    if (int(texture->baseWidth)!=src.width ||
        int(texture->baseHeight ? texture->baseHeight : 1)!=src.height ||
        int(texture->baseDepth ? texture->baseDepth : 1)!=src.depth ||
        int(texture->numLayers ? texture->numLayers : 1)!=src.layers ||
        int(texture->numFaces)!=src.faces ||
        int(texture->numLevels)!=src.mips ||
        (texture->numDimensions==1)!=src.oneD ||
        (texture->numDimensions==3)!=src.volume) {
        error="KTX2 HDR: transcoding changed original texture topology";
        return {};
    }
    const ktxTexture* base=reinterpret_cast<const ktxTexture*>(texture.get());
    const uint8_t* bytes=ktxTexture_GetData(const_cast<ktxTexture*>(base));
    const size_t size=size_t(ktxTexture_GetDataSize(
        const_cast<ktxTexture*>(base)));
    if (!bytes || !size || size>(size_t(512)<<20)) {
        error="KTX2 HDR: missing or oversized transcoded half pixels";
        return {};
    }

    gfx::TextureDesc desc;
    desc.width=src.width;desc.height=src.height;desc.depth=src.depth;
    desc.arrayLayers=src.layers*src.faces;
    desc.format=gfx::PixelFormat::RGBA16F;
    desc.mipLevels=src.mips;
    desc.dimension=src.volume ? gfx::TextureDimension::Texture3D :
        src.oneD ? gfx::TextureDimension::Texture1D :
        src.faces==6 ? (src.layers==1 ? gfx::TextureDimension::Cube :
                          gfx::TextureDimension::CubeArray) :
        src.layers==1 ? gfx::TextureDimension::Texture2D :
                        gfx::TextureDimension::Texture2DArray;
    if (gfx::validateTextureDesc(desc)!=gfx::TextureDescError::None ||
        !device.supportsTextureDimension(desc.dimension)) {
        error="KTX2 HDR: unsupported GPU dimension for UASTC HDR resource";
        return {};
    }
    size_t total=0;
    for (int level=0;level<src.mips;++level) {
        const size_t width=size_t(std::max(1,src.width>>level));
        const size_t height=size_t(std::max(1,src.height>>level));
        const size_t depth=size_t(std::max(1,src.depth>>level));
        const size_t count=size_t(src.layers)*size_t(src.faces)*
                           (src.volume ? depth : 1u);
        if (width>(size_t(512)<<20)/8/height ||
            width*height*8>(size_t(512)<<20)/count ||
            total>(size_t(512)<<20)-width*height*8*count) {
            error="KTX2 HDR: expanded mip chain exceeds 512MiB budget";
            return {};
        }
        const size_t imageBytes=width*height*8;
        for (int layer=0;layer<src.layers;++layer)
            for (int face=0;face<src.faces;++face)
                for (int slice=0;slice<(src.volume ? int(depth) : 1);++slice) {
                    const uint32_t faceSlice=uint32_t(
                        src.volume ? slice : face);
                    ktx_size_t offset=0;
                    if (ktxTexture2_GetImageOffset(texture.get(),
                             ktx_uint32_t(level),ktx_uint32_t(layer),
                             faceSlice,&offset)!=KTX_SUCCESS ||
                        size_t(offset)>size || imageBytes>size-size_t(offset)) {
                        error="KTX2 HDR: truncated half-float slice or invalid offset";
                        return {};
                    }
                }
        total+=imageBytes*count;
    }
    gfx::TextureHandle output=device.createTexture(desc);
    if (!output.valid()) {
        error=device.lastError();
        if(error.empty())error="KTX2 HDR: GPU RGBA16F allocation failed";
        return {};
    }
    for (int level=0;level<src.mips;++level) {
        const int width=std::max(1,src.width>>level);
        const int height=std::max(1,src.height>>level);
        const int depth=std::max(1,src.depth>>level);
        for (int layer=0;layer<src.layers;++layer)
            for (int face=0;face<src.faces;++face)
                for (int slice=0;slice<(src.volume ? depth : 1);++slice) {
                    ktx_size_t offset=0;
                    const uint32_t faceSlice=uint32_t(src.volume?slice:face);
                    if (ktxTexture2_GetImageOffset(texture.get(),
                           ktx_uint32_t(level),ktx_uint32_t(layer),
                           faceSlice,&offset)!=KTX_SUCCESS) {
                        device.destroyTexture(output);
                        error="KTX2 HDR: mip offset changed between validation and upload";
                        return {};
                    }
                    gfx::TextureUploadDesc update;
                    update.mipLevel=level;
                    update.arrayLayer=src.volume ? 0 : layer*src.faces+face;
                    update.z=src.volume ? slice : 0;
                    update.width=width;
                    update.height=height;
                    update.depth=1;
                    update.rowPitchBytes=width*8;
                    update.slicePitchBytes=width*height*8;
                    update.data=bytes+size_t(offset);
                    if (!device.updateTextureRegion(output,update)) {
                        device.destroyTexture(output);
                        error=device.lastError();
                        if(error.empty())error="KTX2 HDR: GPU half-float subresource upload failed";
                        return {};
                    }
                }
    }
    return output;
#else
    (void)device;(void)encoded;
    error="KTX2 UASTC HDR requires AIVERSE_TEXTURE_WITH_LIBKTX=ON";
    return {};
#endif
}

} // namespace aiverse::render
