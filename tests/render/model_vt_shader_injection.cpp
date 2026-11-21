// The VT base-colour injection must find its anchors in the real model
// shaders (it silently produced no VT pipeline after the base fetch moved
// to the parallax-shifted opticalUv).
#include "engine/render/ModelVirtualTextureSampling.h"
#include "engine/render/ModelShaders.h"
#include "tests/AlwaysCheck.h"
#include <string>
using namespace aiverse::render;
int main() {
    for(bool trilinear:{false,true}) {
        std::string fs=shaders::kModelFS, msl=shaders::kModelMSL;
        AIVERSE_TEST_CHECK(injectModelVirtualTextureSampling(fs,msl,13,14,false,trilinear));
        AIVERSE_TEST_CHECK(fs.find("vec4 tb = texture(tex1, opticalUv);")==std::string::npos);
        AIVERSE_TEST_CHECK(msl.find("float4 tb = tex1.sample(s1, opticalUv);")==std::string::npos);
        AIVERSE_TEST_CHECK(fs.find("opticalUv")!=std::string::npos && msl.find("[[texture(14)]]")!=std::string::npos);
    }
    return 0;
}
