// CPU-side normalized visibility adapter for repeat/mirrored VT material UVs.
// Makes bounded half-open rectangles for VirtualTexturePrefetchPlanner.
// Shader page feedback is preferred when available; this is an optional
// CPU visibility projection helper for offline/impostor/low-end scenes.
#pragma once
#include "VirtualTexturePrefetch.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace aiverse::gfx {
namespace prefetch_wrap_detail {
using Range = std::pair<double,double>;

inline double wrappedCoordinate(double coordinate, SamplerAddressMode address) {
    if (address == SamplerAddressMode::Repeat)
        return coordinate-std::floor(coordinate);
    if (address == SamplerAddressMode::MirrorRepeat) {
        const double half=coordinate*0.5;
        return 1.0-std::abs((half-std::floor(half))*2.0-1.0);
    }
    return std::clamp(coordinate,0.0,1.0);
}

// Each bounded interval maps to at most three monotonic intervals for
// mirror, two for repeat, one for clamp. Output is in normalized UV space.
inline bool split(double a,double b,SamplerAddressMode mode,
                  std::vector<Range>& ranges) {
    ranges.clear();
    if (!std::isfinite(a) || !std::isfinite(b) || b <= a ||
        std::abs(a)>1e8 || std::abs(b)>1e8 ||
        mode > SamplerAddressMode::MirrorRepeat) return false;
    if (mode == SamplerAddressMode::ClampToEdge) {
        const double start=std::clamp(a,0.0,1.0);
        const double end=std::clamp(b,0.0,1.0);
        if (end>start) ranges.emplace_back(start,end);
        return true;
    }
    const double period=mode==SamplerAddressMode::Repeat ? 1.0 : 2.0;
    if (b-a >= period) {
        ranges.emplace_back(0.0,1.0);
        return true;
    }
    // Split at every integer wrap/mirror seam before applying fract.
    // This also handles negative UVs and a mixed U/V repeat policy.
    double cursor=a;
    for (int step=0; step<4 && cursor<b; ++step) {
        const double end=std::min(b,std::floor(cursor)+1.0);
        if (end<=cursor) return false;
        const double x=wrappedCoordinate(cursor,mode);
        // Exact integer endpoints: Repeat wraps to 0, but the left
        // interval approaches 1 from below. Preserve that upper edge.
        const double y=(mode==SamplerAddressMode::Repeat &&
                        end==std::floor(end)) ?
                        1.0 : wrappedCoordinate(end,mode);
        const double lo=std::min(x,y), hi=std::max(x,y);
        if (hi>lo) ranges.emplace_back(lo,hi);
        cursor=end;
    }
    return cursor>=b;
}
} // namespace prefetch_wrap_detail

// Safe transactional transformation. False means no partial output, including
// overflow, malformed regions or unsupported address modes. The caller then
// keeps previously resident GPU pages; it must NOT submit partial prefetch.
inline bool normalizeWrappedVirtualTextureRegions(
    const std::vector<VirtualTextureVisibleRegion>& input,
    SamplerAddressMode addressU,SamplerAddressMode addressV,
    std::vector<VirtualTextureVisibleRegion>& output,
    size_t maxOutputRegions=256) {

    output.clear();
    if (maxOutputRegions<1 || maxOutputRegions>4096 ||
        input.size()>maxOutputRegions ||
        addressU>SamplerAddressMode::MirrorRepeat ||
        addressV>SamplerAddressMode::MirrorRepeat) return false;
    std::vector<VirtualTextureVisibleRegion> normalized;
    normalized.reserve(input.size());
    for (const auto& view : input) {
        std::vector<prefetch_wrap_detail::Range> us,vs;
        if (!prefetch_wrap_detail::split(view.u0,view.u1,addressU,us) ||
            !prefetch_wrap_detail::split(view.v0,view.v1,addressV,vs) ||
            view.layer<0 || view.desiredMip<0)
            return false;
        if (us.size()>0 && vs.size()>0 &&
            us.size()>maxOutputRegions/vs.size())
            return false;
        const size_t needed=us.size()*vs.size();
        if (needed>maxOutputRegions-normalized.size()) return false;
        for (const auto& u : us)
            for (const auto& v : vs) {
                auto rectangle=view;
                rectangle.u0=u.first; rectangle.u1=u.second;
                rectangle.v0=v.first; rectangle.v1=v.second;
                normalized.push_back(rectangle);
            }
    }
    output=std::move(normalized);
    return true;
}
} // namespace aiverse::gfx
