// Optional full OpenEXR 3.x library reader for native AIVERSE textures.
// Supports common lossless/lossy scanline compressions (ZIP/ZIPS/PIZ/B44/
// PXR24/DWAA etc. according to the installed OpenEXR library), and
// first-level tiled RGBA when supported by OpenEXR's scanline facade.
// Without the system package, keep the existing narrow uncompressed reader.
// Never include OpenEXR headers or link its libraries unless explicitly enabled.
#pragma once
#include "ExrTexture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#if defined(AIVERSE_TEXTURE_WITH_OPENEXR)
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <Imath/ImathBox.h>
#endif

namespace aiverse::render {

#if defined(AIVERSE_TEXTURE_WITH_OPENEXR)
namespace exr_library_detail {
class SpanInputStream final : public OPENEXR_IMF_NAMESPACE::IStream {
public:
    explicit SpanInputStream(std::string_view bytes)
        : OPENEXR_IMF_NAMESPACE::IStream("AIVERSE memory EXR"),
          bytes_(bytes) {}
    bool read(char out[],int count) override {
        if(count<0 || size_t(count)>bytes_.size()-position_)
            throw std::runtime_error("EXR memory stream: truncated read");
        if(count) std::memcpy(out,bytes_.data()+position_,size_t(count));
        position_+=size_t(count);
        return position_<bytes_.size();
    }
    uint64_t tellg() override {return uint64_t(position_);}
    void seekg(uint64_t offset) override {
        if(offset>bytes_.size())throw std::runtime_error("EXR memory stream: out-of-range seek");
        position_=size_t(offset);
    }
    void clear() override {}
private:
    std::string_view bytes_;
    size_t position_=0;
};
} // namespace exr_library_detail
#endif

inline bool parseOpenExrWithLibrary(std::string_view encoded,
                                   RadianceImage& out,std::string& error) {
    out={};error.clear();
    if(encoded.size()<16 || encoded.size()>(size_t(512)<<20)) {
        error="EXR: invalid container length or 512MiB encoded budget";
        return false;
    }
#if defined(AIVERSE_TEXTURE_WITH_OPENEXR)
    try {
        exr_library_detail::SpanInputStream stream(encoded);
        OPENEXR_IMF_NAMESPACE::RgbaInputFile file(stream);
        const auto bounds=file.dataWindow();
        const int64_t width=int64_t(bounds.max.x)-int64_t(bounds.min.x)+1;
        const int64_t height=int64_t(bounds.max.y)-int64_t(bounds.min.y)+1;
        if(width<1 || height<1 || width>16384 || height>16384 ||
           width*height*8>(int64_t(512)<<20) ||
           bounds.min.x < -16384 || bounds.min.y < -16384 ||
           bounds.max.x > 32767 || bounds.max.y > 32767) {
            error="EXR: dataWindow dimensions or origin exceed supported limits";
            return false;
        }
        // The OpenEXR frame-buffer API maps world dataWindow pixel (x,y)
        // using base + x*xStride + y*yStride. Form the offset address in
        // integer arithmetic to avoid forming a C++ out-of-bounds pointer;
        // the library only accesses positions mapping into the actual
        // allocated contiguous pixel array.
        std::vector<OPENEXR_IMF_NAMESPACE::Rgba> rgba(
            size_t(width)*size_t(height));
        const int64_t delta=int64_t(bounds.min.x)+
                            int64_t(bounds.min.y)*width;
        const uintptr_t data=reinterpret_cast<uintptr_t>(rgba.data());
        const uintptr_t shift=uintptr_t(std::llabs(delta))*
                              sizeof(OPENEXR_IMF_NAMESPACE::Rgba);
        if ((delta>0 && shift>data) ||
            (delta<0 && shift>std::numeric_limits<uintptr_t>::max()-data)) {
            error="EXR: frame-buffer origin offset overflows address range";
            return false;
        }
        auto* base=reinterpret_cast<OPENEXR_IMF_NAMESPACE::Rgba*>(
            delta>=0 ? data-shift : data+shift);
        file.setFrameBuffer(base,1,size_t(width));
        file.readPixels(bounds.min.y,bounds.max.y);

        RadianceImage decoded;
        decoded.width=int(width);decoded.height=int(height);
        decoded.rgba16f.resize(size_t(width)*size_t(height)*4u);
        for(size_t i=0;i<rgba.size();++i) {
            const float c[4]={float(rgba[i].r),float(rgba[i].g),
                              float(rgba[i].b),float(rgba[i].a)};
            for(int k=0;k<4;++k) {
                if (!std::isfinite(c[k])) {
                    error="EXR: nonfinite channel value after decompression";
                    return false;
                }
                decoded.rgba16f[i*4u+size_t(k)]=exr_detail::signedHalf(c[k]);
            }
        }
        out=std::move(decoded);
        return true;
    } catch (const std::exception& exception) {
        error=std::string("OpenEXR: ")+exception.what();
        return false;
    } catch (...) {
        error="OpenEXR: unexpected decoder exception";
        return false;
    }
#else
    // Small uncompressed EXR files remain supported in all builds. A
    // compressed EXR will report the existing explicit unsupported error.
    return parseOpenExr(encoded,out,error);
#endif
}

inline gfx::TextureHandle loadOpenExrWithLibrary(
    gfx::Device& device,std::string_view encoded,std::string& error) {
    RadianceImage image;
    if(!parseOpenExrWithLibrary(encoded,image,error))return {};
    return uploadRadianceHdr(device,image,error);
}

} // namespace aiverse::render
