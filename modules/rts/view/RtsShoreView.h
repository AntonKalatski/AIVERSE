// Shores in the view (docs/water.md § Look): the map's "waterNetwork" banks turned into static models — embankments
// (quay wall, coping, railing or stone parapet, paved promenade, lamps, benches, bollards, steps down to the water),
// cliffs (a rugged rock face from the water to a grassy top and a back slope, boulders and foam at the foot), beaches
// (sand running under the water, surf, umbrellas, loungers, lifeguard towers) and natural banks, with the transitions
// between them. The water surface itself is the terrain's (the baked '~' cells). Textures: procedural, or the game's
// (shore type "textures"); props: built-in shapes or the game's models. Drawn through Renderer3D::drawModel.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../../../engine/render/Model.h"
#include "../../../engine/render/Renderer3D.h"
#include "RtsWater.h"

namespace aiverse::game {

class RtsShoreView {
public:
    void init(gfx::Device& dev, const Json& shoreTypes, render::FileReader reader);
    void shutdown(gfx::Device& dev);
    void setMap(const Json& map);   // its "waterNetwork"; rebuilt on the next draw
    void invalidateGround() { dirty_ = true; }
    // heightAt in world units; waterY: the water surface (world units); w, h: the map size in cells
    void draw(render::Renderer3D& r, const std::function<float(float, float)>& heightAt, float waterY, int w, int h);
    bool active() const { return !net_.empty(); }
    const water::ShoreTypes& types() const { return types_; }
    const water::Network& network() const { return net_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

    // extra materials after the road kit's (RtsRoadView::Mat)
    enum Mat : int { kStone = 100, kRock, kSand, kFoam, kCanvas, kPaint, kShoreMatEnd };

private:
    struct PropModel { std::string path; std::unique_ptr<render::Model> model; };
    struct PropInstance { int model = -1; render::mat4 xf; };
    void rebuild(gfx::Device& dev, const std::function<float(float, float)>& heightAt, float waterY, int w, int h);
    void release(gfx::Device& dev);
    int propModel(gfx::Device& dev, const std::string& path);

    gfx::Device* dev_ = nullptr;
    render::FileReader read_;
    water::ShoreTypes types_;
    water::Network net_;
    Json netJson_;
    bool dirty_ = false;
    float builtWaterY_ = 0;
    int builtW_ = 0, builtH_ = 0;
    std::vector<std::unique_ptr<render::Model>> chunks_, transparent_;
    std::vector<PropModel> propModels_;
    std::vector<PropInstance> props_;
    std::vector<render::Image> images_;
    std::vector<render::Material> materials_;
    std::vector<std::string> warnings_;
};

} // namespace aiverse::game
