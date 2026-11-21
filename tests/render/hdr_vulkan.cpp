// Headless Vulkan host for the HDR / GI validation scene (Linux, Android).
// Same HdrScene as the Metal and GL hosts: the scene owns beginFrame/endFrame and
// reads its pixels back with captureNextFrame from the offscreen back buffer.
// Exit 77 when no Vulkan driver exists: a skip, not a pass.
#include "HdrScene.h"
#include "engine/gfx/vulkan/Vulkan.h"
#include <cstdio>
int aiverseMain(int, char**) {
    std::string error;
    auto dev=aiverse::gfx::createVulkanDevice(aiverse::gfx::VulkanWindow{},&error);
    if(!dev){std::printf("HDR Vulkan: skipped (%s)\n",error.c_str());return 77;}
    std::printf("HDR Vulkan device: %s\n",dev->info().name.c_str());
    aiverse::test::HdrScene scene;scene.init(*dev);
    for(int i=0;i<400&&!scene.captured&&!scene.failed;++i)scene.draw(*dev,960,640);
    if(!scene.captured&&!scene.failed)std::fprintf(stderr,"HDR Vulkan: FAIL no capture (%s)\n",dev->lastError().c_str());
    scene.shutdown(*dev);
    return scene.failed||!scene.captured?1:0;
}
#ifdef __ANDROID__
// PlatformAndroid enters through android_main; a pushed test binary needs main.
int main(int argc,char** argv){return aiverseMain(argc,argv);}
#endif
