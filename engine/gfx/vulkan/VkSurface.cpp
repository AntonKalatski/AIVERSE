// Platform surfaces, kept out of the device sources (window system headers define macros like None / Bool).
#include "VkSurface.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#include <vulkan/vulkan_android.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif

namespace aiverse::gfx::vk {

const char* surfaceExtension(VulkanWindow::Kind kind) {
    switch (kind) {
#if defined(_WIN32)
        case VulkanWindow::Kind::Win32: return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#elif defined(__ANDROID__)
        case VulkanWindow::Kind::Android: return VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
#elif defined(__linux__)
        case VulkanWindow::Kind::Xlib: return VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif
        default: return nullptr;
    }
}

VkResult createPlatformSurface(VkInstance instance, const VulkanWindow& w, VkSurfaceKHR* out) {
    switch (w.kind) {
#if defined(_WIN32)
        case VulkanWindow::Kind::Win32: {
            auto fn = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
            if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
            auto ci = vkStruct<VkWin32SurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
            ci.hinstance = static_cast<HINSTANCE>(w.display);
            ci.hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(w.window));
            return fn(instance, &ci, nullptr, out);
        }
#elif defined(__ANDROID__)
        case VulkanWindow::Kind::Android: {
            auto fn = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR"));
            if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
            auto ci = vkStruct<VkAndroidSurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR);
            ci.window = reinterpret_cast<ANativeWindow*>(static_cast<uintptr_t>(w.window));
            return fn(instance, &ci, nullptr, out);
        }
#elif defined(__linux__)
        case VulkanWindow::Kind::Xlib: {
            auto fn = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(vkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR"));
            if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
            auto ci = vkStruct<VkXlibSurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR);
            ci.dpy = static_cast<Display*>(w.display);
            ci.window = static_cast<Window>(w.window);
            return fn(instance, &ci, nullptr, out);
        }
#endif
        default: (void)instance; (void)out; return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

} // namespace aiverse::gfx::vk
