#pragma once
#include "Vulkan.h"
#include "VkApi.h"

namespace aiverse::gfx::vk {
// Instance extension of the window kind on this platform; nullptr when the kind isn't available here.
const char* surfaceExtension(VulkanWindow::Kind kind);
VkResult createPlatformSurface(VkInstance instance, const VulkanWindow& window, VkSurfaceKHR* out);
} // namespace aiverse::gfx::vk
