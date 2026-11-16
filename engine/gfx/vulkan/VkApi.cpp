#include "VkApi.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace aiverse::gfx::vk {

PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
#define AIVERSE_VK_DEFINE(name) PFN_##name name = nullptr;
AIVERSE_VK_GLOBAL_FUNCS(AIVERSE_VK_DEFINE)
AIVERSE_VK_INSTANCE_FUNCS(AIVERSE_VK_DEFINE)
AIVERSE_VK_SURFACE_FUNCS(AIVERSE_VK_DEFINE)
AIVERSE_VK_DEVICE_FUNCS(AIVERSE_VK_DEFINE)
AIVERSE_VK_SWAPCHAIN_FUNCS(AIVERSE_VK_DEFINE)
#undef AIVERSE_VK_DEFINE

bool loadLoader() {
    if (vkGetInstanceProcAddr) return true;
#if defined(_WIN32)
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) return false;
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(reinterpret_cast<void*>(GetProcAddress(lib, "vkGetInstanceProcAddr")));
#else
#if defined(__ANDROID__)
    void* lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#else
    void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
    if (!lib) return false;
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
#endif
    if (!vkGetInstanceProcAddr) return false;
#define AIVERSE_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(nullptr, #name));
    AIVERSE_VK_GLOBAL_FUNCS(AIVERSE_VK_LOAD)
#undef AIVERSE_VK_LOAD
    return vkCreateInstance != nullptr; // vkEnumerateInstanceVersion is missing on 1.0 loaders
}

bool loadInstanceFunctions(VkInstance instance) {
    bool ok = true;
#define AIVERSE_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name)); ok = ok && name;
    AIVERSE_VK_INSTANCE_FUNCS(AIVERSE_VK_LOAD)
#undef AIVERSE_VK_LOAD
#define AIVERSE_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
    AIVERSE_VK_SURFACE_FUNCS(AIVERSE_VK_LOAD)   // absent without VK_KHR_surface (offscreen devices)
#undef AIVERSE_VK_LOAD
    return ok;
}

bool loadDeviceFunctions(VkDevice device) {
    bool ok = true;
#define AIVERSE_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)); ok = ok && name;
    AIVERSE_VK_DEVICE_FUNCS(AIVERSE_VK_LOAD)
#undef AIVERSE_VK_LOAD
#define AIVERSE_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));
    AIVERSE_VK_SWAPCHAIN_FUNCS(AIVERSE_VK_LOAD)   // absent on headless devices
#undef AIVERSE_VK_LOAD
    return ok;
}

} // namespace aiverse::gfx::vk
