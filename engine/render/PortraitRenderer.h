// Shared offscreen 3D capture and animated model preview. No game or genre dependencies.
// Call capture() outside an active gfx pass, then sample texture() after it returns.
// Device and uploaded models must outlive this object. shutdown() precedes device shutdown.
#pragma once
#include <algorithm>
#include <array>
#include <utility>
#include <vector>
#include "Animation.h"
#include "Renderer3D.h"

namespace aiverse::render {

class PortraitRenderer {
public:
    PortraitRenderer() = default;
    PortraitRenderer(const PortraitRenderer&) = delete;
    PortraitRenderer& operator=(const PortraitRenderer&) = delete;

    bool init(gfx::Device& dev, int size = 256) {
        shutdown();
        if (size <= 0) return false;
        device_ = &dev;
        if (!renderer_.init(dev)) { device_ = nullptr; return false; }
        renderer_.setShadowQuality(ShadowQuality::Low);
        gfx::TextureDesc td;
        td.width = td.height = size;
        td.renderTarget = true;
        td.label = "model preview color";
        color_ = dev.createTexture(td);
        td.format = gfx::PixelFormat::Depth;
        td.filter = gfx::Filter::Nearest;
        td.label = "model preview depth";
        depth_ = dev.createTexture(td);
        if (!color_.valid() || !depth_.valid()) { shutdown(); return false; }
        size_ = size;
        return true;
    }
    void shutdown() {
        if (!device_) return;
        renderer_.shutdown();
        if (color_.valid()) device_->destroyTexture(color_);
        if (depth_.valid()) device_->destroyTexture(depth_);
        color_ = depth_ = {};
        size_ = 0;
        device_ = nullptr;
    }

    template<class Submit>
    bool capture(const Camera& camera, Submit&& submit,
                 std::array<float, 4> clear = {0.16f, 0.18f, 0.22f, 1.0f}) {
        if (!device_ || !color_.valid() || !depth_.valid()) return false;
        renderer_.beginScene(camera, size_, size_);
        std::forward<Submit>(submit)(renderer_);
        gfx::PassDesc pass;
        pass.colorTarget = color_;
        pass.depthTarget = depth_;
        std::copy(clear.begin(), clear.end(), pass.clearColor);
        renderer_.render(pass);
        device_->endPass();
        return true;
    }

    bool renderPose(const Model& model, const Pose& pose, float yaw = 0.5f) {
        if (!device_ || pose.size() != model.nodes.size()) return false;
        std::vector<mat4> world;
        worldMatrices(model, pose, world);
        Camera camera;
        camera.target = (model.boundsMin + model.boundsMax) * 0.5f;
        const auto extent = model.boundsMax - model.boundsMin;
        camera.distance = std::max({extent.x, extent.y, extent.z, 0.3f}) * 2.1f;
        camera.minDistance = 0.01f;
        camera.pitch = 0.15f;
        camera.yaw = yaw;
        camera.fovY = 0.6f;
        renderer_.lighting.skyColor = {0.8f, 0.85f, 0.9f};
        renderer_.lighting.groundColor = {0.5f, 0.5f, 0.5f};
        return capture(camera, [&](Renderer3D& renderer) {
            ModelDraw draw;
            draw.model = &model;
            draw.nodeWorld = &world;
            draw.pose = &pose;
            draw.team = {0.1f, 0.4f, 0.9f};
            renderer.drawModel(draw);
        }, {0.12f, 0.14f, 0.18f, 1.0f});
    }

    gfx::TextureHandle texture() const { return color_; }
    gfx::TextureHandle depthTexture() const { return depth_; }
    gfx::Device* device() const { return device_; }
    int size() const { return size_; }
    Renderer3D& renderer() { return renderer_; }
    const std::string& lastError() const { return renderer_.modelError(); }

private:
    gfx::Device* device_ = nullptr;
    Renderer3D renderer_;
    gfx::TextureHandle color_, depth_;
    int size_ = 0;
};

} // namespace aiverse::render
