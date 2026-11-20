// SpectraFX camera-independent render input. Any native scene can provide
// eye/forward/basis/projection without instantiating a genre camera.
// No gameplay, editor, simulation or RHI dependency.
#pragma once

#include <cmath>
#include "../render/Math3D.h"

namespace aiverse::fx {

struct SpectraFxView {
    render::float3 eye{0,0,0};
    render::float3 forward{0,0,-1};
    render::float3 right{1,0,0};
    render::float3 up{0,1,0};
    render::mat4 viewProj{};
    float projectionYScale=1.0f; // abs(projection[1][1]); used for CPU size budgets
    int width=1,height=1;

    bool valid() const {
        auto finite=[](render::float3 v) {
            return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
        };
        return width>0 && height>0 && std::isfinite(projectionYScale) &&
               projectionYScale>0 && finite(eye) && finite(forward) &&
               finite(right) && finite(up) &&
               render::length(forward)>1e-6f &&
               render::length(right)>1e-6f && render::length(up)>1e-6f;
    }

    // Compatibility for existing camera classes exposing eye/view/proj.
    // New cameras need not implement this interface: fill SpectraFxView
    // directly from their world/view/projection data.
    template<class Camera>
    static SpectraFxView fromCamera(const Camera& camera,
                                    const render::mat4& viewProj,
                                    int viewportWidth,int viewportHeight) {
        const auto view=camera.view();
        const auto projection=camera.proj(1.0f,false);
        SpectraFxView out;
        out.eye=camera.eye();
        out.forward={-view.at(2,0),-view.at(2,1),-view.at(2,2)};
        out.right={view.at(0,0),view.at(0,1),view.at(0,2)};
        out.up={view.at(1,0),view.at(1,1),view.at(1,2)};
        out.viewProj=viewProj;
        out.projectionYScale=std::abs(projection.at(1,1));
        out.width=viewportWidth;
        out.height=viewportHeight;
        return out;
    }
};
} // namespace aiverse::fx
