#include "StudioMetalScene.h"
#include "apps/editor/SceneRenderable.h"
#include "apps/studio-native/StudioTerrainGeometry.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
#include "apps/studio-native/StudioAuthoredLights.h"
#include "apps/studio-native/StudioPbrMaterialPreview.h"
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
        if (materialSphere_) {
            render::releaseModel(*device_,*materialSphere_);
            materialSphere_.reset();
        }
        if (terrain_.vb.valid()) device_->destroyBuffer(terrain_.vb);
        if (terrain_.ib.valid()) device_->destroyBuffer(terrain_.ib);
        renderer_.shutdown();
    }
}
std::string StudioMetalScene::pick(float x, float y, float width, float height,
                                   const editor::MapDoc& map, bool fpsProject) const {
    float3 origin{}, direction{};
    if (materialMode_ || !cameraInitialized_ ||
        !camera_.screenRay(x,y,width,height,origin,direction)) return {};
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
    camera_.pitch = std::clamp(camera_.pitch - dy * 0.007f, -1.52f, 1.52f);
}
void StudioMetalScene::zoom(float wheel) {
    camera_.distance = std::clamp(camera_.distance * std::exp(-wheel * 0.035f), 2.0f, 100000.0f);
}
void StudioMetalScene::look(float dx, float dy) {
    // First-person free look preserves the camera's world-space eye instead
    // of orbiting around its target. WASD movement then translates the eye.
    const float3 position=camera_.eye();
    camera_.yaw += dx*0.007f;
    camera_.pitch=std::clamp(camera_.pitch-dy*0.007f,-1.52f,1.52f);
    const float cp=std::cos(camera_.pitch),sp=std::sin(camera_.pitch);
    const float3 fromTarget{std::sin(camera_.yaw)*cp,sp,std::cos(camera_.yaw)*cp};
    camera_.target=position-fromTarget*camera_.distance;
}
void StudioMetalScene::pan(float dx, float dy) {
    // Editor navigation follows camera orientation, not RTS ground-plane input.
    const float speed=std::max(0.005f,camera_.distance*0.0018f);
    const float yaw=camera_.yaw;
    const float3 right{std::cos(yaw),0,-std::sin(yaw)};
    const float3 up{-std::sin(yaw)*std::sin(camera_.pitch),
                    std::cos(camera_.pitch),
                    -std::cos(yaw)*std::sin(camera_.pitch)};
    camera_.target += right*(-dx*speed) + up*(dy*speed);
}
void StudioMetalScene::move(float right, float up, float forward) {
    const float speed=std::max(0.025f,camera_.distance*0.045f);
    const float yaw=camera_.yaw;
    const float3 side{std::cos(yaw),0,-std::sin(yaw)};
    const float3 ahead{-std::sin(yaw)*std::cos(camera_.pitch),
                       -std::sin(camera_.pitch),
                       -std::cos(yaw)*std::cos(camera_.pitch)};
    camera_.target += (side*right + float3{0,1,0}*up + ahead*forward)*speed;
}
void StudioMetalScene::focusSelected(const editor::MapDoc& map,bool fpsProject,
                                      const std::string& selectedId) {
    const auto proxies=studioDocumentProxies(map,fpsProject,terrainLevelHeight_);
    for (const auto& proxy:proxies) {
        if (proxy.id != selectedId) continue;
        camera_.target=(proxy.boundsMin+proxy.boundsMax)*0.5f;
        const float3 extent=proxy.boundsMax-proxy.boundsMin;
        const float size=std::max({extent.x,extent.y,extent.z,0.5f});
        camera_.distance=std::clamp(size*3.0f,2.0f,100000.0f);
        return;
    }
}
void StudioMetalScene::resetView() {
    cameraInitialized_=false;
    camera_.pitch=0.85f;
    camera_.yaw=0.55f;
}
void StudioMetalScene::frame(int framebufferWidth, int framebufferHeight, const editor::MapDoc& doc,
                             bool fpsProject, const std::string& selectedId,
                             const std::string& previewName, const Json& previewFactors,
                             uint64_t previewRevision, float levelHeight) {
    if (!device_ || framebufferWidth < 2 || framebufferHeight < 2) return;
    const bool materialRequested = !previewName.empty() && previewFactors.isObject();
    if (materialRevision_ != previewRevision) {
        materialRevision_ = previewRevision;
        if (materialRequested) {
            if (!materialSphere_) {
                // Same primitive->Model->GPU path used by AIVERSE's HDR material gallery.
                render::MeshBuilder sphere;
                sphere.color(render::rgba(255,255,255));
                sphere.sphere({0,0,0},1.0f,32,48);
                render::Material base;
                base.vertexColors=false;
                materialSphere_ = render::modelFromParts("studio/pbr-preview",
                    {{"PBR sphere",&sphere,base,render::mat4{}}});
                if (!materialSphere_) throw std::runtime_error("Material preview geometry missing");
                materialSphere_->batching.autoLod=false;
                render::prepareModelUpload(*materialSphere_);
                if (!render::uploadModel(*device_,*materialSphere_))
                    throw std::runtime_error("Material preview GPU upload failed");
            }
            materialSphere_->materials.at(0)=studioPbrPreviewMaterial(previewFactors,previewName);
            materialSphere_->markMaterialChanged(); // invalidate renderer-owned material signature
        }
    }
    if (materialMode_ != materialRequested) {
        materialMode_=materialRequested;
        cameraInitialized_=false;
    }
    if (materialMode_) {
        if (!cameraInitialized_) {
            camera_.target={0,0,0};
            camera_.distance=4.3f;
            camera_.pitch=.5f;
            camera_.yaw=.25f;
            cameraInitialized_=true;
        }
        device_->beginFrame(framebufferWidth,framebufferHeight);
        renderer_.beginScene(camera_,framebufferWidth,framebufferHeight);
        renderer_.lighting=render::Lighting{};
        renderer_.lighting.sunIntensity=1.2f;
        renderer_.lighting.skyColor={.42f,.51f,.66f};
        renderer_.lighting.groundColor={.27f,.27f,.31f};
        renderer_.lighting.fogDensity=0;
        renderer_.lights.add({1.5f,2.4f,1.6f},6,{1,.9f,.78f},2.5f);
        if (materialSphere_) {
            render::ModelDraw sample;
            sample.model=materialSphere_.get();
            renderer_.drawModel(sample);
        }
        renderer_.drawMesh(cube_,render::mat4::translate({0,-1.14f,0})*
            render::mat4::scale({4,.18f,4}),{.22f,.25f,.3f,1},render::kDrawNoCastShadow);
        gfx::PassDesc previewPass;
        previewPass.clearColor[0]=.07f;
        previewPass.clearColor[1]=.09f;
        previewPass.clearColor[2]=.135f;
        renderer_.render(previewPass);
        device_->endPass();
        device_->endFrame();
        return;
    }
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
    renderer_.lighting=Lighting{}; // reset when project and lighting settings change
    const auto day=previewDocumentDay(doc,fpsProject);
    const Json& lightingSource=fpsProject ? doc.data.fps["lighting"] : doc.data.extra["lighting"];
    if (lightingSource.isObject()) {
        renderer_.lighting.sunIntensity=day.sun;
        renderer_.lighting.skyColor={.5f*day.ambient,.58f*day.ambient,.7f*day.ambient};
        renderer_.lights.setNightGlow(day.night*1.5f);
        renderer_.lights.setClouds(day.clouds,.6f,16.f,0,0);
    }
    const auto activeLights=studioAuthoredLights(doc,fpsProject,levelHeight);
    for(const auto& light:activeLights) renderer_.lights.add(light);

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
    // Editor gizmos for authored lights; actual light contribution is passed through
    // Renderer3D::lights, not faked with an overlay screenshot.
    for(const auto& light:activeLights) {
        const auto at=light.pos;
        const float extent=std::clamp(light.radius*.08f,.2f,2.f);
        const uint32_t color=rgba(245,200,100);
        renderer_.line({at.x-extent,at.y,at.z},{at.x+extent,at.y,at.z},color);
        renderer_.line({at.x,at.y-extent,at.z},{at.x,at.y+extent,at.z},color);
        renderer_.line({at.x,at.y,at.z-extent},{at.x,at.y,at.z+extent},color);
    }
    gfx::PassDesc pass;
    pass.clearColor[0] = 0.038f; pass.clearColor[1] = 0.064f;
    pass.clearColor[2] = 0.098f; pass.clearColor[3] = 1.0f;
    renderer_.render(pass);
    device_->endPass();
    device_->endFrame();
}
}
