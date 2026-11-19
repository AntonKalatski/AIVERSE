// AIVERSE physical virtual-page mip generation. Pure CPU, no RHI or genre coupling.
// RGBA8 mipmaps are downsampled in premultiplied-alpha linear light to avoid
// dark fringes and sRGB gamma artifacts. The input page already includes gutters;
// consumers must ensure gutter texels correspond to neighbor/edge content.
#pragma once
#include "Gfx.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace aiverse::gfx {
struct VirtualTextureMipView {
    const void* data = nullptr;
    size_t bytes = 0;
};
struct VirtualTextureMipLevel {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

inline bool buildVirtualTexturePhysicalMips(
    const void* base, size_t baseBytes, int physicalPageSize,
    int levels, bool srgb, std::vector<VirtualTextureMipLevel>& output) {
    output.clear();
    if (!base || physicalPageSize < 1 || physicalPageSize > 4096 ||
        levels < 1 || levels > maxTextureMipLevels(physicalPageSize,physicalPageSize) ||
        size_t(physicalPageSize)*size_t(physicalPageSize)*4u != baseBytes)
        return false;
    std::vector<VirtualTextureMipLevel> chain;
    chain.reserve(size_t(levels));
    VirtualTextureMipLevel first;
    first.width=first.height=physicalPageSize;
    const uint8_t* src=static_cast<const uint8_t*>(base);
    first.rgba.assign(src,src+baseBytes);
    chain.push_back(std::move(first));
    const auto decode = [srgb](uint8_t value)->double {
        const double s=double(value)/255.0;
        if (!srgb) return s;
        return s<=0.04045 ? s/12.92 : std::pow((s+0.055)/1.055,2.4);
    };
    const auto encode = [srgb](double value)->uint8_t {
        const double v=std::clamp(value,0.0,1.0);
        const double c=srgb ? (v<=0.0031308 ? 12.92*v :
                       1.055*std::pow(v,1.0/2.4)-0.055) : v;
        return uint8_t(std::clamp(int(std::lround(c*255.0)),0,255));
    };
    for (int mip=1; mip<levels; ++mip) {
        const VirtualTextureMipLevel& previous=chain.back();
        VirtualTextureMipLevel current;
        current.width=std::max(1,previous.width>>1);
        current.height=std::max(1,previous.height>>1);
        current.rgba.resize(size_t(current.width)*size_t(current.height)*4u);
        for(int y=0;y<current.height;++y)
            for(int x=0;x<current.width;++x) {
                double totalA=0.0, rgb[3]={0.0,0.0,0.0};
                for(int dy=0;dy<2;++dy)
                    for(int dx=0;dx<2;++dx) {
                        const int sy=std::min(previous.height-1,y*2+dy);
                        const int sx=std::min(previous.width-1,x*2+dx);
                        const uint8_t* p=previous.rgba.data()+
                            (size_t(sy)*size_t(previous.width)+size_t(sx))*4u;
                        const double a=double(p[3])/255.0;
                        totalA+=a;
                        for(int c=0;c<3;++c) rgb[c]+=a*decode(p[c]);
                    }
                uint8_t* out=current.rgba.data()+
                    (size_t(y)*size_t(current.width)+size_t(x))*4u;
                for(int c=0;c<3;++c)
                    out[c]=encode(totalA>0.0 ? rgb[c]/totalA : 0.0);
                out[3]=uint8_t(std::clamp(int(std::lround(totalA*255.0/4.0)),0,255));
            }
        chain.push_back(std::move(current));
    }
    output=std::move(chain);
    return true;
}
} // namespace aiverse::gfx
