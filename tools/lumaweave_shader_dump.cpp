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
#include <string>

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
// The GLES3/WebGL2 model shader path uses the same GLSL bodies as the
// Vulkan renderer BEFORE SPIR-V translation. Export complete #version 300 es
// sources for strict offline GLSL ES3 and browser device shader validation.
struct PortableModelSource {
    const char* key;
    const char* defines;
};
const PortableModelSource portableModels[] = {
    {"model_gi_l1","#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n"
                   "#define DYNAMIC_GI 1\n"},
    {"model_gi_valid","#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n"
                      "#define DYNAMIC_GI 1\n#define DYNAMIC_GI_VALIDITY 1\n"},
    {"model_gi_dual","#define AIVERSE_INST 1\n#define AIVERSE_Q 2\n"
                     "#define DYNAMIC_GI 1\n#define DYNAMIC_GI_VALIDITY 1\n"
                     "#define DYNAMIC_GI_CASCADES 1\n"},
    {"model_gi_instanced","#define AIVERSE_INST 2\n#define AIVERSE_Q 2\n"
                          "#define DYNAMIC_GI 1\n"},
    {"model_gi_palette","#define AIVERSE_INST 2\n#define AIVERSE_Q 2\n"
                        "#define MATERIAL_PALETTE 1\n#define DYNAMIC_GI 1\n"}
};
} // namespace

int main(int argc,char** argv) {
    if(argc==2 && std::strcmp(argv[1],"--gles3-list")==0) {
        for(const auto& s:portableModels)std::printf("%s\n",s.key);
        return 0;
    }
    if(argc==3 && (!std::strcmp(argv[1],"--gles3-vertex") ||
                   !std::strcmp(argv[1],"--gles3-fragment"))) {
        const bool vertex=!std::strcmp(argv[1],"--gles3-vertex");
        for(const auto& s:portableModels)if(!std::strcmp(argv[2],s.key)) {
            const std::string source=std::string(
                "#version 300 es\nprecision highp float;\n"
                "precision highp int;\n")+
                s.defines+(vertex?aiverse::render::shaders::kModelVS:
                                 aiverse::render::shaders::kModelFS);
            std::fputs(source.c_str(),stdout);
            return std::ferror(stdout)?4:0;
        }
        std::fprintf(stderr,"unknown GLES3 model key: %s\n",argv[2]);
        return 2;
    }
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
