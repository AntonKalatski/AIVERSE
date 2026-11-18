// Optional Basis Universal / UASTC transcoding bridge for KTX2 containers.
// The actual codec must be provided by the host application. This boundary
// validates both the source header/index and the resulting mip/face layout.
#pragma once
#include "TextureAsset.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <string_view>

namespace aiverse::render {

using Ktx2Transcoder = std::function<bool(std::string_view encoded,
                                         CompressedImage& decoded,
                                         std::string& error)>;

namespace ktx2_bridge_detail {
inline uint32_t u32(std::string_view bytes, size_t at) {
    return uint32_t(uint8_t(bytes[at])) |
           uint32_t(uint8_t(bytes[at+1])) << 8 |
           uint32_t(uint8_t(bytes[at+2])) << 16 |
           uint32_t(uint8_t(bytes[at+3])) << 24;
}
inline uint64_t u64(std::string_view bytes, size_t at) {
    return uint64_t(u32(bytes,at)) | (uint64_t(u32(bytes,at+4)) << 32);
}
inline bool knownFormat(BlockFormat f) {
    switch (f) {
    case BlockFormat::None:
    case BlockFormat::ETC2_RGB8: case BlockFormat::ETC2_RGBA8:
    case BlockFormat::EAC_R11: case BlockFormat::BC1:
    case BlockFormat::BC3: case BlockFormat::BC4: case BlockFormat::BC5:
    case BlockFormat::BC7: case BlockFormat::BC6H_UFloat:
    case BlockFormat::BC6H_SFloat:
    case BlockFormat::ASTC_4x4: case BlockFormat::ASTC_6x6:
        return true;
    }
    return false;
}
struct Source {
    int width=0, height=0, depth=1, layers=1, faces=1;
    int mips=0;
    bool oneD=false, volume=false, basis=false;
};
inline bool inspect(std::string_view input, Source& out, std::string& err) {
    const auto fail = [&](const char* text) { err=text; return false; };
    static constexpr uint8_t identifier[12] =
        {0xAB,'K','T','X',' ','2','0',0xBB,13,10,26,10};
    if (input.size()<80 || input.size()>(size_t(512)<<20) ||
        std::memcmp(input.data(),identifier,12)!=0)
        return fail("KTX2: invalid signature or container size budget");
    const uint32_t format=u32(input,12);
    const uint32_t width=u32(input,20);
    const uint32_t srcH=u32(input,24);
    const uint32_t srcD=u32(input,28);
    const uint32_t srcLayers=u32(input,32);
    const uint32_t faces=u32(input,36);
    const uint32_t rawMips=u32(input,40);
    const uint32_t scheme=u32(input,44);
    const uint32_t height=std::max(1u,srcH);
    const uint32_t depth=std::max(1u,srcD);
    const uint32_t layers=std::max(1u,srcLayers);
    const bool oneD=srcH==0, volume=srcD>0;
    if (scheme>2 || (scheme==1 && format!=0))
        return fail("KTX2: contradictory or unsupported supercompression");
    if (width<1 || width>16384 || height>16384 || depth>16384 ||
        layers>2048 || (faces!=1 && faces!=6) ||
        (oneD && (volume || faces!=1 || srcLayers>1)) ||
        (volume && (faces!=1 || srcLayers>1)) ||
        (faces==6 && (oneD || width!=height)))
        return fail("KTX2: invalid header dimensions/texture topology");
    const uint32_t mips=std::max(1u,rawMips);
    int maxMips=1;
    for(uint32_t n=std::max({width,height,depth});n>1;n>>=1) ++maxMips;
    if(mips>uint32_t(maxMips) || mips>31 ||
       uint64_t(80)+uint64_t(mips)*24u>input.size())
        return fail("KTX2: invalid or truncated level index");
    const uint64_t indexEnd=80ull+uint64_t(mips)*24u;
    for(uint32_t mip=0;mip<mips;++mip) {
        const size_t at=80u+size_t(mip)*24u;
        const uint64_t offset=u64(input,at), compressed=u64(input,at+8);
        const uint64_t expanded=u64(input,at+16);
        if(offset<indexEnd || offset>input.size() || compressed==0 ||
           compressed>uint64_t(input.size())-offset || expanded==0 ||
           expanded>(uint64_t(512)<<20))
            return fail("KTX2: level payload/index out of bounds");
    }
    out.width=int(width);
    out.height=int(height);
    out.depth=int(depth);
    out.layers=int(layers);
    out.faces=int(faces);
    out.mips=int(mips);
    out.oneD=oneD;
    out.volume=volume;
    out.basis=format==0 || scheme==1;
    return true;
}
} // namespace ktx2_bridge_detail

// Canonical GPU upload layout: exact tightly packed mip-major data,
// including every face, array layer and volume slice. No partially
// transcoded image or arbitrary enum is allowed through to the renderer.
inline bool validateTranscodedImage(const CompressedImage& image,
                                    std::string& err) {
    const auto fail=[&](const char* text) { err=text; return false; };
    if (!ktx2_bridge_detail::knownFormat(image.format) ||
        image.width<1 || image.height<1 || image.depth<1 ||
        image.width>16384 || image.height>16384 || image.depth>16384 ||
        image.arrayLayers<1 || image.arrayLayers>2048 ||
        (image.faces!=1 && image.faces!=6) ||
        (image.threeDimensional &&
          (image.oneDimensional || image.faces!=1 || image.arrayLayers!=1)) ||
        (image.oneDimensional &&
          (image.height!=1 || image.depth!=1 ||
           image.faces!=1 || image.arrayLayers!=1)) ||
        (image.depth>1 && (image.faces!=1 || image.arrayLayers!=1)) ||
        (image.faces==6 && (image.width!=image.height || image.depth!=1)) ||
        image.arrayLayers>std::numeric_limits<int>::max()/image.faces ||
        image.levels.empty())
        return fail("KTX2 transcoder: invalid dimensions, topology or format");
    const int maxMips=gfx::maxTextureMipLevels(
        image.width,image.height,image.depth);
    if (image.levels.size()>size_t(maxMips) || image.levels.size()>31)
        return fail("KTX2 transcoder: invalid mip chain");
    size_t aggregate=0;
    const size_t maxBytes=size_t(512)<<20;
    for(size_t mip=0;mip<image.levels.size();++mip) {
        const size_t one=levelByteSize(image.format,
                         levelWidth(image,int(mip)),levelHeight(image,int(mip)));
        const int n=levelImageCount(image,int(mip));
        if(!one || n<1 || one>maxBytes/size_t(n))
            return fail("KTX2 transcoder: invalid mip byte stride");
        const size_t expected=one*size_t(n);
        if(image.levels[mip].size()!=expected ||
           aggregate>maxBytes-expected)
            return fail("KTX2 transcoder: mip payload size/budget mismatch");
        aggregate+=expected;
    }
    err.clear();
    return true;
}

inline bool parseKtx2WithTranscoder(std::string_view encoded,
                                    const Ktx2Transcoder& transcoder,
                                    CompressedImage& out, std::string& error) {
    out={};error.clear();
    ktx2_bridge_detail::Source source;
    if(!ktx2_bridge_detail::inspect(encoded,source,error))return false;
    if(!source.basis) {
        if(!parseKtx2(std::string(encoded),out,error))return false;
        if(!validateTranscodedImage(out,error)){out={};return false;}
        return true;
    }
    if(!transcoder) {
        error="KTX2 BasisLZ/UASTC codec not installed";
        return false;
    }
    try {
        if(!transcoder(encoded,out,error)){
            out={};
            if(error.empty())error="KTX2 BasisLZ/UASTC transcoding failed";
            return false;
        }
    } catch (...) {
        out={};
        error="KTX2 transcoder threw an exception";
        return false;
    }
    // A pluggable transcoder must not reinterpret the source dimensionality,
    // lose authored mip levels or omit array/cube faces.
    if(out.width!=source.width || out.height!=source.height ||
       out.depth!=source.depth || out.arrayLayers!=source.layers ||
       out.faces!=source.faces || out.oneDimensional!=source.oneD ||
       out.threeDimensional!=source.volume ||
       out.levels.size()!=size_t(source.mips) ||
       !validateTranscodedImage(out,error)) {
        out={};
        if(error.empty()) error="KTX2 transcoder changed the source topology";
        return false;
    }
    error.clear();
    return true;
}

inline gfx::TextureHandle uploadKtx2WithTranscoder(
        gfx::Device& device, std::string_view encoded,
        const Ktx2Transcoder& codec, const TextureUploadOptions& options,
        std::string& error) {
    CompressedImage decoded;
    if(!parseKtx2WithTranscoder(encoded,codec,decoded,error))return {};
    return uploadContainerTexture(device,decoded,options,error);
}

} // namespace aiverse::render
