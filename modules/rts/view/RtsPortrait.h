// RTS adapter only: unit/type/material lookup stays here; the offscreen renderer is generic.
// The shared engine/render/PortraitRenderer.h also works from FPS, native games and Studio.
#pragma once
#include <memory>
#include <vector>
#include "RtsView.h"
#include "engine/render/PortraitRenderer.h"

namespace aiverse::game {

class Portrait {
public:
    bool init(gfx::Device& dev, int size = 256);
    void shutdown();
    void render(RtsView& view, uint16_t type, uint8_t owner, double time, float yaw = -1);
    void renderPose(const render::Model&, const render::Pose&, float yaw = 0.5f);
    void renderMaterial(RtsView& view, const Json& material, int shape, double time);
    gfx::TextureHandle texture() const { return preview_.texture(); }
    int size() const { return preview_.size(); }
    const std::string& lastError() const { return preview_.lastError(); }

private:
    render::PortraitRenderer preview_;
    std::unique_ptr<render::Model> shapes_[2];
    std::vector<render::Material> materials_;
};

} // namespace aiverse::game
