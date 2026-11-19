// Dump the ACTUAL concatenated Metal source strings used by LumaWeave and
// Hybrid Dynamic GI. Native acceptance runner pipes these to xcrun metal.
// This is a tool only, NOT a stand-in for runtime GPU visual/API validation.
#include "engine/render/GiGpuSmallSceneShaders.h"
#include "engine/render/LumaWeaveMetalShader.h"
#include "engine/render/LumaWeaveFullscreenMetalShader.h"
#include "engine/render/LumaWeavePbrMetalShader.h"
#include "engine/render/LumaWeaveDenoiseMetalShader.h"
#include "engine/render/LumaWeaveHdrCompositeShader.h"
#include "engine/render/ModelShaders.h"
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
struct Source {
    std::string_view key;
    const char* source;
    const char* definePrefix;
};
const Source sources[]={
    {"gi_compute",aiverse::render::gi_gpu::kMetalSmallScene,""},
    {"luma_receivers",AIVERSE_MSL_LUMAWEAVE_DIRECT,""},
    {"luma_screen",AIVERSE_MSL_LUMAWEAVE_FULLSCREEN,""},
    {"luma_screen_pbr",AIVERSE_MSL_LUMAWEAVE_SCREEN_PBR,""},
    {"luma_denoise",AIVERSE_MSL_LUMAWEAVE_DENOISE,""},
    {"luma_denoise_pbr",AIVERSE_MSL_LUMAWEAVE_DENOISE_PBR,""},
    {"luma_hdr_composite",AIVERSE_MSL_LUMAWEAVE_HDR_COMPOSITE,""},
    {"model_material",aiverse::render::shaders::kModelMSL,
        "#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n#define LUMAWEAVE_MATERIAL 1\n"},
    {"model_surface",aiverse::render::shaders::kModelMSL,
        "#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n#define LUMAWEAVE_SURFACE 1\n"},
    {"model_gi_dual",aiverse::render::shaders::kModelMSL,
        "#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n#define DYNAMIC_GI 1\n"
        "#define DYNAMIC_GI_VALIDITY 1\n#define DYNAMIC_GI_CASCADES 1\n"},
    {"model_gi_triple",aiverse::render::shaders::kModelMSL,
        "#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n#define DYNAMIC_GI 1\n"
        "#define DYNAMIC_GI_VALIDITY 1\n#define DYNAMIC_GI_CASCADES 1\n"
        "#define DYNAMIC_GI_TRIPLE 1\n"}
};
} // namespace

int main(int argc,char** argv) {
    if(argc==2 && std::strcmp(argv[1],"--list")==0) {
        for(const auto& item:sources)std::printf("%.*s\n",int(item.key.size()),item.key.data());
        return 0;
    }
    if(argc!=2) {
        std::fprintf(stderr,"usage: aiverse_lumaweave_shader_dump --list | <shader-key>\n");
        return 2;
    }
    for(const auto& item:sources) {
        if(item.key==argv[1]) {
            if(!item.source || !*item.source)return 3;
            std::fputs(item.definePrefix,stdout);
            std::fputs(item.source,stdout);
            return std::ferror(stdout)?4:0;
        }
    }
    std::fprintf(stderr,"unknown shader key: %s\n",argv[1]);
    return 2;
}
