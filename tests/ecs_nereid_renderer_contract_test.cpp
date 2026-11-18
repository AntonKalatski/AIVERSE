// NEREID stage 7C: CPU-only renderer bridge contract, no GPU device needed.
#include "engine/render/NereidWaterRenderer.h"
#include "engine/render/Shaders.h"
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
    std::cout<<"PASS NEREID renderer bridge colors and GLSL/MSL marker contract\n";
}
