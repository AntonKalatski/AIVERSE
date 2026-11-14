// Spline roads in the view (docs/roads.md § Look): the map's "roadNetwork" turned into static models — carriageways
// with lane markings, curbs, shoulders, sidewalks, medians, junction surfaces with fillets and zebra crossings,
// barriers, bridge decks with pillars, tunnel portals and the road props (built-in shapes or the game's models).
// Geometry comes from RtsRoadNet (the simulation's numbers); meshes are cut into square chunks so the renderer culls
// them. Textures: the game's (road type "look.textures") or procedural tiles generated at init. No shaders of its own:
// everything is drawn through Renderer3D::drawModel (PBR, shadows, lightmaps as static scenery).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../../../engine/render/Model.h"
#include "../../../engine/render/Renderer3D.h"
#include "../../../engine/render/SpriteBatch.h"
#include "../../../engine/render/TextureCache.h"
#include "../RtsRoadNet.h"

namespace aiverse::game {

struct MapGrid;

class RtsRoadView {
public:
    // rules "roadTypes" (composed rules.json); reader for the game's textures / models
    void init(gfx::Device& dev, const Json& roadTypes, render::FileReader reader);
    void shutdown(gfx::Device& dev);
    // the map (its "roadNetwork"); rebuilt on the next draw. Errors go to warnings().
    void setMap(const Json& map);
    void setNetwork(const RoadNetwork& n) { net_ = n; dirty_ = true; hasNet_ = true; }
    void invalidateGround() { dirty_ = true; }   // terrain changed under the roads
    // heightAt(x, z) in world units (the view's terrain); cellMeters of the road data turns it into metres
    void draw(render::Renderer3D& r, const std::function<float(float, float)>& heightAt);
    bool active() const { return hasNet_ && !net_.empty(); }
    const std::vector<RoadTypeDef>& types() const { return types_; }
    const RoadNetwork& network() const { return net_; }
    const RoadGeometry& geometry() const { return geom_; }
    float cellMeters() const { return cellMeters_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    // editor preview: one edge drawn as a translucent ribbon (not part of the network)
    struct Ghost { bool on = false; RoadNetwork net; bool ok = true; };
    void setGhost(const Ghost& g) { ghost_ = g; ghostDirty_ = true; }

    // materials of the road kit (index into the chunk models' material lists)
    enum Mat : int { kAsphalt = 0, kConcrete, kCobble, kPaving, kGravel, kDirt, kGrass, kCurb, kWhite, kYellow, kMetal,
                     kWood, kLeaves, kBark, kGlow, kSoil, kBridge, kHedge, kDark, kSignalRed, kSignalAmber, kSignalGreen, kSign, kGlass, kAsphaltWorn, kMatCount };

private:
    struct Chunk { std::unique_ptr<render::Model> model; };
    struct PropModel { std::string path; std::unique_ptr<render::Model> model; };
    struct PropInstance { int model = -1; render::mat4 xf; };
    void rebuild(gfx::Device& dev, const std::function<float(float, float)>& heightAt);
    void buildGhost(gfx::Device& dev, const std::function<float(float, float)>& heightAt);
    void release(gfx::Device& dev);
    int propModel(gfx::Device& dev, const std::string& path);
    int paintedMaterial(gfx::Device& dev, int type, int variant);   // carriageway texture with its lines, made on demand
    std::map<std::pair<int, int>, int> painted_;

    gfx::Device* dev_ = nullptr;
    render::FileReader read_;
    std::vector<RoadTypeDef> types_;
    float cellMeters_ = 4;
    RoadNetwork net_;
    Json netJson_;                               // the map's roadNetwork (parsed again when the types change)
    RoadGeometry geom_;
    bool hasNet_ = false, dirty_ = false;
    std::vector<Chunk> chunks_;
    std::vector<PropModel> propModels_;
    std::vector<PropInstance> props_;
    std::vector<render::Image> images_;          // the kit's textures (copied into every chunk model, shared GPU handles)
    std::vector<render::Material> materials_;
    std::vector<std::string> warnings_;
    Ghost ghost_;
    bool ghostDirty_ = false;
    std::unique_ptr<render::Model> ghostModel_;
    render::SpriteBatch decalBatch_;             // road paint as decals
    render::TextureCache paintTex_;
    render::TextureRef paint_;
    render::SpriteMesh decalMesh_;
};

} // namespace aiverse::game
