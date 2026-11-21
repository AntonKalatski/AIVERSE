// Build without the NVIDIA NGX SDK: the "dlss" effect has no provider (see PrismaDlss.h).
#include "PrismaDlss.h"

namespace aiverse::render {
bool prismaDlssCompiled() { return false; }
void prismaDlssRequestVulkanExtensions() {}
std::shared_ptr<PrismaExternalUpscaler> makePrismaDlss() { return nullptr; }
} // namespace aiverse::render
