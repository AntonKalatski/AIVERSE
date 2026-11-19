// Particle effects in the game client: one ParticleSystem + the RTS event bridge, fed from the
// match each frame. Kept in its own file so the client only calls a handful of methods.
#pragma once
#include "engine/fx/ParticleSystem.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/TextureCache.h"
#include "modules/rts/fx/RtsFx.h"
#include "modules/rts/view/RtsView.h"

namespace aiverse::client {

class ClientFx {
public:
    void init(gfx::Device& dev, const game::Rules& rules);
    void shutdown();
    void startMatch(Simulation& sim, uint8_t viewer, const game::RtsView& view, const game::MapGrid& grid);
    void stopMatch();
    // Planet maps: particles in the camera body's local space (RtsView::planetFxSpace); call before update().
    void setPlanetView(const game::RtsView* view);
    // After Renderer3D::beginScene (needs its viewProj); dt = 0 while paused.
    void update(Simulation& sim, const game::RtsScene& scene, const render::RTSCamera& cam, const render::mat4& viewProj,
                int fbW, int fbH, float dt);
    // After update(), before Renderer3D::render(): lights carried by particles ("light" module).
    void addLights(render::LightSystem& lights);
    // After Renderer3D::render(): draws into the still open main pass.
    void render(const render::Lighting& lighting);

    int quality() const { return int(ps_.quality()); }
    void setQuality(int q) { ps_.setQuality(fx::FxQuality(q < 0 ? 0 : q > 3 ? 3 : q)); }
    fx::ParticleSystem& particles() { return ps_; }
    // This host owns particle GPU buffers/atlas AND separate sprite cache.
    // The particle batches only borrow sprite textures from this cache.
    render::GpuOwnedMemoryStats ownedGpuMemoryStats() const {
        const auto particle = ps_.ownedGpuMemoryStats();
        const auto images = textures_.ownedGpuMemoryStats();
        render::GpuOwnedMemoryStats result=particle;
        result.textureKnownBytes=
            render::GpuOwnedMemoryCounter::saturatingAdd(
                result.textureKnownBytes,images.textureKnownBytes);
        result.bufferKnownBytes=
            render::GpuOwnedMemoryCounter::saturatingAdd(
                result.bufferKnownBytes,images.bufferKnownBytes);
        result.textures+=images.textures;
        result.buffers+=images.buffers;
        result.unknownTextures+=images.unknownTextures;
        result.unknownBuffers+=images.unknownBuffers;
        result.multiVersionBuffers+=images.multiVersionBuffers;
        return result;
    }

private:
    fx::ParticleSystem ps_;
    game::RtsFx bridge_;
    render::TextureCache textures_;   // sprite textures of effects: the same loader (and atlas) as sprites
    bool attached_ = false;
    std::vector<fx::FxPointLight> lightBuf_;
    const game::RtsView* planet_ = nullptr;   // planet maps: the view placing effects (null on flat maps)
    int planetBody_ = -1;
    render::mat4 space_;
};

} // namespace aiverse::client
