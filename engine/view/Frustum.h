// Shared four-sided camera frustum for View presentation culling.
// Generic to chunked flat surfaces, planets, scene editors and all genres.
// Near/far excluded intentionally for differing renderer clip-depth ranges.
#pragma once
#include "engine/render/Math3D.h"
#include <array>
#include <cmath>
namespace aiverse::view {
struct SideFrustum {
    std::array<render::float4,4> planes{};
    static SideFrustum fromViewProjection(const render::mat4& vp,
                                          bool normalized=false) {
        SideFrustum result;
        for(int i=0;i<4;++i) {
            const int row=i/2;const float sign=i%2?-1.f:1.f;
            auto& plane=result.planes[size_t(i)];
            plane={vp.at(3,0)+sign*vp.at(row,0),
                   vp.at(3,1)+sign*vp.at(row,1),
                   vp.at(3,2)+sign*vp.at(row,2),
                   vp.at(3,3)+sign*vp.at(row,3)};
            if(normalized) {
                const float len=std::sqrt(plane.x*plane.x+
                    plane.y*plane.y+plane.z*plane.z);
                if(std::isfinite(len)&&len>1.e-12f)
                    plane={plane.x/len,plane.y/len,plane.z/len,plane.w/len};
                else plane={0,0,0,0}; // degenerate -> conservative visible
            }
        }
        return result;
    }
    bool visibleAABB(render::float3 mn,render::float3 mx) const {
        for(const auto& plane:planes) {
            const render::float3 q{plane.x>=0?mx.x:mn.x,
                plane.y>=0?mx.y:mn.y,plane.z>=0?mx.z:mn.z};
            if(plane.x*q.x+plane.y*q.y+plane.z*q.z+plane.w<0.f)
                return false;
        }
        return true;
    }
    bool visibleSphere(render::float3 c,float radius) const {
        if(!std::isfinite(radius)||radius<0.f)return false;
        for(const auto& plane:planes) {
            const float norm=std::sqrt(plane.x*plane.x+
                plane.y*plane.y+plane.z*plane.z);
            if(plane.x*c.x+plane.y*c.y+plane.z*c.z+plane.w<
                -radius*norm)return false;
        }
        return true;
    }
};
} // namespace aiverse::view
