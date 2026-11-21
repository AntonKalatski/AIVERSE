// NVIDIA DLSS (Super Resolution) provider for PRISMA's external upscaler route.
// Compiled only with -DAIVERSE_DLSS_SDK_DIR=<DLSS SDK> on Windows with the Vulkan backend
// (CMakeLists.txt); otherwise PrismaDlssStub.cpp is linked. See PrismaDlss.h.
//
// Contract with Prisma (PrismaUpscaleInputs): colour = linear HDR RGBA16F at render size,
// depth = device depth [0,1] (not reversed), velocity RG = current-minus-previous screen UV
// with y up, output = RGBA16F storage image at output size. Every image rests in
// VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; the output is moved to GENERAL for the
// evaluation and back. Evaluation records into the engine's current frame command buffer.
#include "PrismaDlss.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>

namespace aiverse::render {
namespace {

constexpr uint32_t kShaderRead = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
constexpr uint32_t kGeneral = VK_IMAGE_LAYOUT_GENERAL;

NVSDK_NGX_PerfQuality_Value qualityFor(int renderW, int outputW) {
    const float ratio = float(renderW) / float(outputW > 0 ? outputW : 1);
    if (ratio >= 0.99f) return NVSDK_NGX_PerfQuality_Value_DLAA;
    if (ratio >= 0.62f) return NVSDK_NGX_PerfQuality_Value_MaxQuality;    // 0.667
    if (ratio >= 0.54f) return NVSDK_NGX_PerfQuality_Value_Balanced;      // 0.58
    if (ratio >= 0.42f) return NVSDK_NGX_PerfQuality_Value_MaxPerf;       // 0.5
    return NVSDK_NGX_PerfQuality_Value_UltraPerformance;                  // 0.333
}

NVSDK_NGX_Resource_VK resourceFor(const gfx::NativeVulkanImage& image, bool readWrite) {
    VkImageSubresourceRange range{};
    range.aspectMask = image.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0; range.levelCount = 1;
    range.baseArrayLayer = 0; range.layerCount = 1;
    return NVSDK_NGX_Create_ImageView_Resource_VK(static_cast<VkImageView>(image.view),
        static_cast<VkImage>(image.image), range, static_cast<VkFormat>(image.format),
        unsigned(image.width), unsigned(image.height), readWrite);
}

class DlssNgx final : public PrismaExternalUpscaler {
public:
    const char* id() const override { return "dlss"; }

    bool supported(gfx::Device& dev, std::string& reason) override {
        if (dev.info().backend != gfx::Backend::Vulkan) { reason = "DLSS needs the Vulkan backend (AIVERSE_GFX=vulkan)"; return false; }
        if (failed_) { reason = failure_; return false; }
        if (!ready_ && !initialize(dev)) { reason = failure_; return false; }
        return true;
    }

    bool evaluate(gfx::Device& dev, const PrismaUpscaleInputs& in, std::string& error) override {
        if (!ready_ && !initialize(dev)) { error = failure_; return false; }
        gfx::NativeVulkanContext ctx;
        if (!dev.nativeVulkanContext(ctx)) { error = "no Vulkan command buffer outside a render pass"; return false; }
        gfx::NativeVulkanImage color, depth, velocity, output;
        if (!dev.nativeVulkanImage(in.color, color) || !dev.nativeVulkanImage(in.depth, depth) ||
            !dev.nativeVulkanImage(in.output, output)) { error = "native Vulkan images unavailable"; return false; }
        if (!output.storage) { error = "DLSS output is not a storage image"; return false; }
        const bool haveVelocity = in.velocity.valid() && dev.nativeVulkanImage(in.velocity, velocity);
        auto cmd = static_cast<VkCommandBuffer>(ctx.commandBuffer);

        View& view = views_[in.viewId];
        if (!view.handle || view.renderW != in.renderWidth || view.renderH != in.renderHeight ||
            view.outputW != in.outputWidth || view.outputH != in.outputHeight) {
            if (view.handle) { NVSDK_NGX_VULKAN_ReleaseFeature(view.handle); view.handle = nullptr; }
            NVSDK_NGX_DLSS_Create_Params create{};
            create.Feature.InWidth = unsigned(in.renderWidth);
            create.Feature.InHeight = unsigned(in.renderHeight);
            create.Feature.InTargetWidth = unsigned(in.outputWidth);
            create.Feature.InTargetHeight = unsigned(in.outputHeight);
            create.Feature.InPerfQualityValue = qualityFor(in.renderWidth, in.outputWidth);
            // Linear HDR colour; motion vectors at render size and computed with the jittered
            // projection of the current frame; depth is not reversed.
            create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
                                          NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                                          NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
            create.InEnableOutputSubrects = false;
            const NVSDK_NGX_Result r = NGX_VULKAN_CREATE_DLSS_EXT(cmd, 1, 1, &view.handle, params_, &create);
            if (NVSDK_NGX_FAILED(r) || !view.handle) {
                view.handle = nullptr;
                error = "NGX_VULKAN_CREATE_DLSS_EXT failed (0x" + hex(unsigned(r)) + ")";
                return false;
            }
            view.renderW = in.renderWidth; view.renderH = in.renderHeight;
            view.outputW = in.outputWidth; view.outputH = in.outputHeight;
        }

        NVSDK_NGX_Resource_VK colorRes = resourceFor(color, false);
        NVSDK_NGX_Resource_VK depthRes = resourceFor(depth, false);
        NVSDK_NGX_Resource_VK velocityRes = haveVelocity ? resourceFor(velocity, false) : NVSDK_NGX_Resource_VK{};
        NVSDK_NGX_Resource_VK outputRes = resourceFor(output, true);

        NVSDK_NGX_VK_DLSS_Eval_Params eval{};
        eval.Feature.pInColor = &colorRes;
        eval.Feature.pInOutput = &outputRes;
        eval.Feature.InSharpness = in.sharpness;
        eval.pInDepth = &depthRes;
        eval.pInMotionVectors = haveVelocity ? &velocityRes : nullptr;
        // Prisma jitters the projection by +j render pixels (x right, y up); DLSS takes the
        // sample offset in y-down pixels. AIVERSE_DLSS_JITTER_SIGN=-1 flips it if a driver /
        // SDK version expects the opposite convention.
        const float sign = jitterSign();
        eval.InJitterOffsetX = -in.jitterPixelsX * sign;
        eval.InJitterOffsetY = in.jitterPixelsY * sign;
        eval.InRenderSubrectDimensions.Width = unsigned(in.renderWidth);
        eval.InRenderSubrectDimensions.Height = unsigned(in.renderHeight);
        eval.InReset = (in.reset || !haveVelocity) ? 1 : 0;
        // Velocity is current-minus-previous UV, y up; DLSS wants previous-minus-current in
        // pixels, y down: (-W, +H).
        eval.InMVScaleX = -float(in.renderWidth);
        eval.InMVScaleY = float(in.renderHeight);
        eval.InFrameTimeDeltaInMsec = in.frameSeconds * 1000.0f;
        eval.InPreExposure = 1.0f;
        eval.InExposureScale = 1.0f;

        dev.nativeVulkanImageBarrier(in.output, kShaderRead, kGeneral);
        const NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSS_EXT(cmd, view.handle, params_, &eval);
        dev.nativeVulkanImageBarrier(in.output, kGeneral, kShaderRead);
        if (NVSDK_NGX_FAILED(r)) {
            error = "NGX_VULKAN_EVALUATE_DLSS_EXT failed (0x" + hex(unsigned(r)) + ")";
            return false;
        }
        return true;
    }

    void releaseView(uint64_t viewId) override {
        auto it = views_.find(viewId);
        if (it == views_.end()) return;
        if (it->second.handle) NVSDK_NGX_VULKAN_ReleaseFeature(it->second.handle);
        views_.erase(it);
    }

    void shutdown(gfx::Device& dev) override {
        (void)dev;
        for (auto& [id, view] : views_) if (view.handle) NVSDK_NGX_VULKAN_ReleaseFeature(view.handle);
        views_.clear();
        if (params_) { NVSDK_NGX_VULKAN_DestroyParameters(params_); params_ = nullptr; }
        if (ready_) NVSDK_NGX_VULKAN_Shutdown1(static_cast<VkDevice>(device_));
        ready_ = false;
        device_ = nullptr;
    }

private:
    struct View {
        NVSDK_NGX_Handle* handle = nullptr;
        int renderW = 0, renderH = 0, outputW = 0, outputH = 0;
    };

    static std::string hex(unsigned v) { char b[16]; std::snprintf(b, sizeof b, "%08x", v); return b; }
    static float jitterSign() {
        const char* e = std::getenv("AIVERSE_DLSS_JITTER_SIGN");
        return e && std::strcmp(e, "-1") == 0 ? -1.0f : 1.0f;
    }

    bool initialize(gfx::Device& dev) {
        gfx::NativeVulkanContext ctx;
        if (!dev.nativeVulkanContext(ctx)) { failure_ = "Vulkan context unavailable (outside a frame)"; return false; }
        // NGX keeps logs / its model cache under this folder (user writable, next to the exe).
        const wchar_t* dataPath = L".";
        const char* projectId = std::getenv("AIVERSE_DLSS_PROJECT_ID");
        const NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID(
            projectId && *projectId ? projectId : "8f3a2c1e-5b7d-4e9a-a1c2-0d4e6f8a9b10",
            NVSDK_NGX_ENGINE_TYPE_CUSTOM, "AIVERSE", dataPath,
            static_cast<VkInstance>(ctx.instance), static_cast<VkPhysicalDevice>(ctx.physicalDevice),
            static_cast<VkDevice>(ctx.device),
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(ctx.getInstanceProcAddr), nullptr);
        if (NVSDK_NGX_FAILED(r)) return fail("NVSDK_NGX_VULKAN_Init failed (0x" + hex(unsigned(r)) +
                                             "): no RTX GPU, old driver or missing nvngx_dlss.dll");
        device_ = ctx.device;
        ready_ = true;
        if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetCapabilityParameters(&params_)) || !params_)
            return fail("NGX capability parameters unavailable");
        int available = 0, needsDriver = 0;
        NVSDK_NGX_Parameter_GetI(params_, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
        if (needsDriver) return fail("DLSS needs a newer NVIDIA driver");
        if (NVSDK_NGX_FAILED(NVSDK_NGX_Parameter_GetI(params_, NVSDK_NGX_Parameter_SuperSampling_Available, &available)) || !available)
            return fail("DLSS Super Resolution is not available on this GPU");
        return true;
    }
    bool fail(std::string why) {
        failure_ = std::move(why);
        failed_ = true;
        std::fprintf(stderr, "[prisma] DLSS: %s\n", failure_.c_str());
        return false;
    }

    bool ready_ = false, failed_ = false;
    std::string failure_ = "DLSS not initialized";
    void* device_ = nullptr;
    NVSDK_NGX_Parameter* params_ = nullptr;
    std::map<uint64_t, View> views_;
};

} // namespace

bool prismaDlssCompiled() { return true; }

void prismaDlssRequestVulkanExtensions() {
    unsigned instanceCount = 0, deviceCount = 0;
    const char** instanceExts = nullptr;
    const char** deviceExts = nullptr;
    auto& req = gfx::vulkanExtensionRequests();
    if (NVSDK_NGX_SUCCEED(NVSDK_NGX_VULKAN_RequiredExtensions(&instanceCount, &instanceExts, &deviceCount, &deviceExts))) {
        for (unsigned i = 0; i < instanceCount; ++i) req.instance.emplace_back(instanceExts[i]);
        for (unsigned i = 0; i < deviceCount; ++i) req.device.emplace_back(deviceExts[i]);
    } else {
        // Documented NGX requirements of the DLSS SDK 3.x.
        req.device.insert(req.device.end(), {"VK_NVX_binary_import", "VK_NVX_image_view_handle", "VK_KHR_push_descriptor"});
    }
}

std::shared_ptr<PrismaExternalUpscaler> makePrismaDlss() { return std::make_shared<DlssNgx>(); }

} // namespace aiverse::render

namespace {
// The Vulkan device is created by the platform layer before any renderer exists; register
// NGX's extensions at program start so the backend enables them.
const bool kDlssExtensionsRequested = (aiverse::render::prismaDlssRequestVulkanExtensions(), true);
} // namespace
