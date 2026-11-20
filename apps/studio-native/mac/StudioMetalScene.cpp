#include "StudioMetalScene.h"
#include "apps/editor/SceneRenderable.h"
#include "apps/studio-native/StudioTerrainGeometry.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aiverse::studio {
using namespace aiverse::render;
StudioMetalScene::StudioMetalScene(void* metalLayer)
    : device_(gfx::createMetalDevice(metalLayer)) {
    if (!device_) throw std::runtime_error("Metal device unavailable");
    if (!renderer_.init(*device_)) throw std::runtime_error("AIVERSE Renderer3D init failed");
    renderer_.setShadowQuality(ShadowQuality::Off); // cheap editor preview until full scene parity
    MeshBuilder builder;
    builder.color(rgba(255,255,255));
    builder.box({0,0,0}, {1,1,1});
    cube_ = builder.upload(*device_);
    if (!cube_.vb.valid() || !cube_.ib.valid()) throw std::runtime_error("Preview cube mesh upload failed");
    camera_.pitch = 0.85f;
    camera_.yaw = 0.55f;
}
StudioMetalScene::~StudioMetalScene() {
    if (device_) {
        if (cube_.vb.valid()) device_->destroyBuffer(cube_.vb);
        if (cube_.ib.valid()) device_->destroyBuffer(cube_.ib);
        if (terrain_.vb.valid()) device_->destroyBuffer(terrain_.vb);
        if (terrain_.ib.valid()) device_->destroyBuffer(terrain_.ib);
        renderer_.shutdown();
    }
}
std::string StudioMetalScene::pick(float x, float y, float width, float height,
                                   const editor::MapDoc& map, bool fpsProject) const {
    float3 origin{}, direction{};
    if (!cameraInitialized_ || !camera_.screenRay(x,y,width,height,origin,direction)) return {};
    const auto proxies=studioDocumentProxies(map,fpsProject,terrainLevelHeight_);
    std::vector<editor::SceneRenderable> candidates;
    candidates.reserve(proxies.size());
    for(size_t i=0;i<proxies.size();++i) {
        editor::SceneRenderable candidate;
        candidate.entity=uint64_t(i)+1;
        candidate.boundsMin=proxies[i].boundsMin;
        candidate.boundsMax=proxies[i].boundsMax;
        candidate.pickable=true;
        candidates.push_back(candidate);
    }
    const int hit=editor::pickSceneRenderable(candidates,origin,direction);
    return hit<0 ? std::string() : proxies[size_t(hit)].id;
}
void StudioMetalScene::orbit(float dx, float dy) {
    camera_.yaw += dx * 0.007f;
    camera_.pitch = std::clamp(camera_.pitch - dy * 0.007f, 0.18f, 1.45f);
}
void StudioMetalScene::zoom(float wheel) {
    camera_.distance = std::clamp(camera_.distance * std::exp(-wheel * 0.035f), 2.0f, 100000.0f);
}
void StudioMetalScene::frame(int framebufferWidth, int framebufferHeight, const editor::MapDoc& doc,
                             bool fpsProject, const std::string& selectedId, float levelHeight) {
    if (!device_ || framebufferWidth < 2 || framebufferHeight < 2) return;
    const auto& map = doc.data;
    if (terrainRevision_ != doc.revision || terrainLevelHeight_ != levelHeight) {
        terrainLevelHeight_ = levelHeight;
        MeshBuilder source;
        const auto stats = buildTerrainPreview(map, source, levelHeight);
        Mesh replacement;
        if (stats.quads) replacement = source.upload(*device_);
        if (terrain_.vb.valid()) device_->destroyBuffer(terrain_.vb);
        if (terrain_.ib.valid()) device_->destroyBuffer(terrain_.ib);
        terrain_ = replacement;
        terrainRevision_ = doc.revision;
    }
    if (!cameraInitialized_ || lastMapWidth_ != map.w || lastMapHeight_ != map.h) {
        camera_.target = {map.w * .5f, 0.0f, map.h * .5f};
        camera_.distance = std::clamp(float(std::max(map.w, map.h)) * 1.4f, 16.0f, 100000.0f);
        cameraInitialized_ = true;
        lastMapWidth_ = map.w;
        lastMapHeight_ = map.h;
    }
    device_->beginFrame(framebufferWidth, framebufferHeight);
    renderer_.beginScene(camera_, framebufferWidth, framebufferHeight);

    // Real map dimensions and positions from the active native Workspace.
    // Temporary primitive preview (NOT legacy editor terrain, animation or materials).
    const float w = float(std::max(map.w, 1));
    const float h = float(std::max(map.h, 1));
    if (terrain_.vb.valid() && terrain_.ib.valid()) {
        renderer_.drawMesh(terrain_, mat4{}, {1,1,1,1}, kDrawNoCastShadow);
    } else {
        // Unsupported planet maps or empty terrain: basic plane only.
        renderer_.drawMesh(cube_,
                           mat4::translate({w*.5f,-0.08f,h*.5f}) * mat4::scale({w,.12f,h}),
                           {0.18f,0.26f,0.33f,1}, kDrawNoCastShadow);
    }
    const int xStep = std::max(1, map.w / 48);
    const int zStep = std::max(1, map.h / 48);
    for (int x=0; x<=map.w; x+=xStep)
        renderer_.line({float(x),.01f,0},{float(x),.01f,h},rgba(55,84,109,120));
    for (int z=0; z<=map.h; z+=zStep)
        renderer_.line({0,.01f,float(z)},{w,.01f,float(z)},rgba(55,84,109,120));

    const auto proxies=studioDocumentProxies(doc,fpsProject,levelHeight);
    for(const auto& entity:proxies) {
        renderer_.drawMesh(cube_,
            mat4::translate(entity.center)*mat4::rotateY(entity.yaw)*
            mat4::scale(entity.half*2.0f), entity.color, kDrawNoCastShadow);
        if(entity.id==selectedId) {
            const auto lo=entity.boundsMin,hi=entity.boundsMax;
            const float3 p[8]={{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},
                {hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,lo.z},
                {hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
            constexpr int edges[12][2]={{0,1},{1,2},{2,3},{3,0},
                {4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for(const auto& edge:edges)
                renderer_.line(p[edge[0]],p[edge[1]],rgba(255,192,63));
        }
    }
    gfx::PassDesc pass;
    pass.clearColor[0] = 0.038f; pass.clearColor[1] = 0.064f;
    pass.clearColor[2] = 0.098f; pass.clearColor[3] = 1.0f;
    renderer_.render(pass);
    device_->endPass();
    device_->endFrame();
}
}
