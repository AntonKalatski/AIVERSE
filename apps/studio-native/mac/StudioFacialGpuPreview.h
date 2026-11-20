// Real MotionWeave facial preview rendered through the existing native
// Renderer3D/Metal surface. The Studio UI never synthesizes 3D imagery.
// One Animator owns the base pose; FacialControlRig modifies that pose once.
#pragma once
#include "apps/studio-native/StudioFacialPreviewState.h"
#include "engine/gfx/Gfx.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/RenderDeviceLifetime.h"
#include <cstdint>
#include <memory>

namespace aiverse::studio {
class StudioFacialGpuPreview final {
public:
    explicit StudioFacialGpuPreview(gfx::Device& device);
    ~StudioFacialGpuPreview();
    StudioFacialGpuPreview(const StudioFacialGpuPreview&)=delete;
    StudioFacialGpuPreview& operator=(const StudioFacialGpuPreview&)=delete;

    void frame(gfx::Device& device,render::Renderer3D& renderer,
               render::RTSCamera& camera,int width,int height,
               const StudioFacialPreviewState& request);
    // Explicit GPU-safe shutdown, before StudioMetalScene destroys device.
    // On unproven completion, ownership is deliberately quarantined.
    void shutdown(gfx::Device& device);
    uint64_t renderedGeneration() const noexcept{return renderedGeneration_;}
private:
    struct Resident;
    void retireCurrent(gfx::Device& device);
    void load(gfx::Device& device,render::RTSCamera& camera,
              const StudioFacialPreviewState& request);
    std::unique_ptr<render::RenderDeviceLifetime> deviceLifetime_;
    std::unique_ptr<Resident> live_;
    uint64_t currentGeneration_=0;
    uint64_t renderedGeneration_=0;
};
} // namespace aiverse::studio
