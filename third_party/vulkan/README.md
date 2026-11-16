# Vulkan headers (vendored)

- Upstream: https://github.com/KhronosGroup/Vulkan-Headers, tag `vulkan-sdk-1.4.363.0`
  (commit 6802bb4733b63ed5efd3adb308a6c885ef180ea1)
- Licence: `Apache-2.0 OR MIT`; used here under MIT (`LICENSE.txt`).
- Kept: `vk_platform.h`, `vulkan_core.h`, the platform headers `vulkan_xlib.h`, `vulkan_win32.h`,
  `vulkan_android.h` and `vk_video/` (included by `vulkan_core.h`). No loader: the engine loads
  `libvulkan` / `vulkan-1.dll` at run time (`engine/gfx/vulkan/VkApi.h`).
