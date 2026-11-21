// NVIDIA DLSS for PRISMA's "dlss" effect, through the NVIDIA NGX SDK (Windows, Vulkan, RTX).
// The SDK is proprietary and never vendored: configure with
//   cmake -DAIVERSE_DLSS_SDK_DIR=<path to the unpacked DLSS SDK> ...
// (its include/ and lib/Windows_x86_64/ folders; nvngx_dlss.dll is copied next to the client).
// Without the SDK these functions are inert and the effect reports why it did not run.
//
//   render::prismaDlssRequestVulkanExtensions();      // before the Vulkan device is created
//   ...
//   if (auto dlss = render::makePrismaDlss()) prisma.setExternalUpscaler(dlss);
#pragma once
#include <memory>
#include "Prisma.h"

namespace aiverse::render {
bool prismaDlssCompiled();
void prismaDlssRequestVulkanExtensions();
std::shared_ptr<PrismaExternalUpscaler> makePrismaDlss();
} // namespace aiverse::render
