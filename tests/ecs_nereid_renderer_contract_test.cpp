// NEREID stage 7C: CPU-only renderer bridge contract, no GPU device needed.
#include "engine/render/NereidWaterRenderer.h"
#include "engine/render/NereidSurfaceResident.h"
#include "engine/render/Shaders.h"
#include "engine/render/NereidWaterReflectionPolicy.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse;
static void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int main() {
    lib::nereid::WaterSurfaceVertex shore;
    shore.shore=1.f;
    lib::nereid::WaterSurfaceVertex deep;
    const auto a=render::nereidWaterVertexColor(shore);
    const auto b=render::nereidWaterVertexColor(deep);
    check(a!=b,"shore foam must affect vertex tint");
    const lib::nereid::WaterSurfaceStyle style{};
    check(render::validNereidWaterStyle(style),"valid default water style");
    auto bad=style;bad.opacity=1.f;
    check(!render::validNereidWaterStyle(bad),
          "opaque water breaks translucent reflection path");
    check((render::kDrawWaterSurface & render::kDrawUnlit)==0 &&
          (render::kDrawWaterSurface & render::kDrawNoCastShadow)==0,
          "NEREID draw flag conflicts with existing flags");
    check(std::string(render::shaders::kLitFS).find("u_params.x < -0.5")
          !=std::string::npos,"GLSL water flag missing");
    check(std::string(render::shaders::kLitMSL).find("u.params.x < -0.5")
          !=std::string::npos,"Metal water flag missing");
    check(render::sameWaterReflectionPlane(12.f,12.01f),
          "same-level water must share primary planar reflection");
    check(!render::sameWaterReflectionPlane(12.f,9.f),
          "water at a distinct elevation must not reuse a wrong mirror");
    check(!render::sameWaterReflectionPlane(12.f,12.03f),
          "water beyond plane epsilon must not use primary reflection");
    check(std::string(render::shaders::kLitFS).find(
          "u_params.x < -1.5 ? 0.0 : u_params.y")!=std::string::npos,
          "GLSL secondary-plane reflection suppression missing");
    check(std::string(render::shaders::kLitMSL).find(
          "u.params.x < -1.5 ? 0.0 : u.params.y")!=std::string::npos,
          "Metal secondary-plane reflection suppression missing");
    // Stable patch reuses a resident ocean mesh until the camera moves into
    // another grid cell; small camera shifts are not GPU upload triggers.
    lib::nereid::WaterSurfaceMeshOptions opts;
    opts.oceanPatchRadius=128.f;
    opts.cameraX=10.f;opts.cameraZ=-10.f;
    auto first=render::makeNereidSurfaceKey(7,true,opts);
    opts.cameraX=120.f;opts.cameraZ=-120.f;
    auto shifted=render::makeNereidSurfaceKey(7,true,opts);
    check(render::sameNereidSurfaceKey(first,shifted),
          "camera motion within an ocean patch must not trigger upload");
    opts.cameraX=129.f;
    auto moved=render::makeNereidSurfaceKey(7,true,opts);
    check(!render::sameNereidSurfaceKey(first,moved),
          "crossing an ocean cell must trigger rebuild");
    auto revised=render::makeNereidSurfaceKey(8,true,opts);
    check(!render::sameNereidSurfaceKey(moved,revised),
          "geometry revision must invalidate resident surface");
    opts.cameraX=10.f;
    auto boxA=render::makeNereidSurfaceKey(7,false,opts);
    opts.cameraX=10000.f;
    auto boxB=render::makeNereidSurfaceKey(7,false,opts);
    check(render::sameNereidSurfaceKey(boxA,boxB),
          "finite water body must ignore camera motion");
    auto recolored=style;recolored.foamStrength=0.2f;
    check(!render::sameNereidSurfaceKey(
          boxB,render::makeNereidSurfaceKey(7,false,opts,recolored)),
          "foam tint change must trigger upload");
    check(render::nereidPatchAnchor(-1.f,128.f)==-128.f,
          "negative camera coordinates must floor to correct patch");
    check(!std::isfinite(render::nereidPatchAnchor(0.f,0.f)),
          "invalid ocean radius must be rejected");
    std::cout<<"PASS NEREID renderer bridge + multi-height planar fallback contract\n";
}
