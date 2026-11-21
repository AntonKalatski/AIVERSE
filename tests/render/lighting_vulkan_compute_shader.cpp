#include "engine/gfx/vulkan/VkShaderCompiler.h"
#include "engine/render/GiGpuVulkanShaders.h"
#include "engine/render/LumaWeaveVulkanShaders.h"
#include "tests/AlwaysCheck.h"
#include <cstdio>
int main() {
    std::vector<uint32_t> words;std::string error;
    const bool ok=aiverse::gfx::vk::compileComputeProgram(aiverse::render::gi_gpu::kVulkanSmallScene,words,error);
    if(!ok)std::fprintf(stderr,"%s\n",error.c_str());
    AIVERSE_TEST_CHECK(ok && words.size()>5 && words[0]==0x07230203);
    // LumaWeave screen kernels (direct, GGX, denoise, PBR denoise).
    namespace lv=aiverse::render::luma_vk;
    for(const char* source:{lv::kDirect,lv::kDirectPbr,lv::kDenoise,lv::kDenoisePbr}) {
        std::vector<uint32_t> luma;std::string lumaError;
        const bool built=aiverse::gfx::vk::compileComputeProgram(source,luma,lumaError);
        if(!built)std::fprintf(stderr,"Luma Vulkan kernel: %s\n",lumaError.c_str());
        AIVERSE_TEST_CHECK(built && luma.size()>5);
    }
    AIVERSE_TEST_CHECK(!aiverse::gfx::vk::compileComputeProgram("invalid shader",words,error));
    AIVERSE_TEST_CHECK(words.empty() && !error.empty());
}
