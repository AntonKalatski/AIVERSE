#include "apps/editor/AuthoringWater.h"
#include "StudioMetalScene.h"
#include "apps/studio-native/StudioLightingPreview.h"
#include "engine/render/GiModelGeometry.h"
#include "apps/editor/SceneRenderable.h"
#include "apps/studio-native/StudioVisualPicking.h"
#include "apps/studio-native/StudioTerrainGeometry.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
#include "apps/studio-native/StudioAuthoredLights.h"
#include "apps/studio-native/StudioPbrMaterialPreview.h"
#include "engine/view/AppearanceChoiceAuthoring.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <cmath>
#include <stdexcept>

namespace aiverse::studio {
using namespace aiverse::render;
namespace { struct StudioFrameScope {
    gfx::Device& device;
    ~StudioFrameScope(){device.endPass();device.endFrame();}
}; }
namespace {
// Shared layout of the native orientation gizmo (top-right corner).
struct GizmoLayout { float cx, cy, r; };
GizmoLayout gizmoLayout(float width,float height) {
    const float r=std::clamp(std::min(width,height)*0.065f,24.0f,160.0f);
    return {width-r*1.75f,r*1.75f,r};
}
struct GizmoBasis { float3 right, up, forward; };
GizmoBasis gizmoBasis(float yaw,float pitch) {
    const float cp=std::cos(pitch),sp=std::sin(pitch);
    const float3 forward{-std::sin(yaw)*cp,-sp,-std::cos(yaw)*cp};
    const float3 right{std::cos(yaw),0,-std::sin(yaw)};
    return {right,cross(right,forward),forward};
}
float3 gizmoAxis(int arm) {
    float3 a{0,0,0};
    const float sign=(arm&1)?-1.0f:1.0f;
    if(arm/2==0)a.x=sign; else if(arm/2==1)a.y=sign; else a.z=sign;
    return a;
}
}
StudioMetalScene::StudioMetalScene(void* metalLayer)
    : device_(gfx::createMetalDevice(metalLayer)) {
    if (!device_) throw std::runtime_error("Metal device unavailable");
    deviceLifetime_=std::make_unique<render::RenderDeviceLifetime>(*device_);
    facialGpu_.setDeviceLease(deviceLifetime_->lease());
    renderer_.setGpuDeviceLease(deviceLifetime_->lease());
    if (!renderer_.init(*device_)) throw std::runtime_error("AIVERSE Renderer3D init failed");
    if (!post_.init(*device_)) throw std::runtime_error("Studio HDR init failed");
    PrismaSettings postSettings;postSettings.hdr=HdrMode::On;
    post_.setSettings(postSettings);post_.setQuality(PrismaQuality::High);
    lodDiagnostics_=std::getenv("AIVERSE_LOD_DIAGNOSTICS")!=nullptr;
    renderer_.setLodDiagnosticsEnabled(lodDiagnostics_);
    antialiasing_.setGpuDeviceLease(deviceLifetime_->lease());
    if(!antialiasing_.init(*device_))throw std::runtime_error("Studio anti-aliasing init failed");
    PostFxSettings aa;aa.hdr=HdrMode::Off;
    aa.effects.push_back(defaultPostFxEffect(PostFxType::Fxaa));
    antialiasing_.setSettings(std::move(aa));
    antialiasing_.setQuality(PostFxQuality::Medium);
    renderer_.setShadowQuality(ShadowQuality::Off); // cheap editor preview until full scene parity
    MeshBuilder builder;
    builder.color(rgba(255,255,255));
    builder.box({0,0,0}, {1,1,1});
    cube_ = builder.upload(*device_);
    if (!cube_.vb.valid() || !cube_.ib.valid()) throw std::runtime_error("Preview cube mesh upload failed");
    studioCyclorama_=vivid::humanStudioCyclorama().upload(*device_);
    if(!studioCyclorama_.vb.valid()||!studioCyclorama_.ib.valid())throw std::runtime_error("Studio cyclorama upload failed");
    camera_.pitch = 0.85f;
    camera_.yaw = 0.55f;
}
void StudioMetalScene::releasePreviewModel(render::Model& model) {
    // Retire CPU packets/layouts before the Model and its geometry disappear.
    renderer_.invalidateModelCachesFor(&model);
    render::releaseModel(*device_,model);
}
void StudioMetalScene::clearDocumentModels() {
    if (device_) for (auto& [path,model]:documentModels_)
        if (model) releasePreviewModel(*model);
    modelInstances_.clear();
    documentModels_.clear();
    rejectedDocumentModels_.clear();
    documentModelTimes_.clear();
    instanceMaterials_.clear();
}

void StudioMetalScene::syncDocumentModels(
    const editor::MapDoc& doc,const std::string& projectPath,
    const Json& rules,const std::vector<StudioDocumentProxy>& proxies) {
    modelInstances_.clear();
    if (projectPath.empty()) return;
    namespace fs=std::filesystem;
    const fs::path project=projectPath;
    const fs::path root=project/"content";
    if (!fs::is_directory(root) || fs::is_symlink(fs::symlink_status(root))) return;
    // The proxy list is the authoritative list of actual document objects.
    // Only existing, explicitly authored content/ model paths are accepted.
    const auto& entities=doc.data.entities;
    std::set<std::string> active;
    {
        // materials.json drives Mesh Renderer material overrides in the preview.
        const fs::path libraryPath=project/"materials.json";
        std::error_code timeError;
        const auto modified=fs::last_write_time(libraryPath,timeError);
        if(libraryPath.string()!=materialLibraryPath_ || (!timeError && modified!=materialLibraryTime_)) {
            materialLibraryPath_=libraryPath.string();
            materialLibraryTime_=timeError?fs::file_time_type{}:modified;
            materialLibrary_=Json::object();
            std::ifstream input(libraryPath,std::ios::binary);
            if(input) {
                const std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
                try { const Json parsed=Json::parse(text)["materials"]; if(parsed.isObject())materialLibrary_=parsed; }
                catch(const std::exception&) {}
            }
            instanceMaterials_.clear();
        }
    }
    std::set<std::string> overridden;
    for (const auto& proxy:proxies) {
        if (proxy.id.rfind("entity:",0)!=0) continue;
        const int index=studioMapEntityIndex(proxy.id,doc);
       if(index<0)continue;
       const auto& entity=entities[size_t(index)];
       if(entity.extra["components"].has("NereidWater"))continue;
       const auto& view=rules["types"][entity.type]["view"];
       const Json& raw=view["model"];
       std::string path=editor::authoredModelAsset(entity);
       if(path.empty()) {
           if(raw.isString())path=raw.str();
           else if(raw.isArray()&&raw.size()){const auto& first=raw[size_t(0)];path=first.isString()?first.str():first.getStr("file");}
       }
        // Never load project-root arbitrary files or external paths.
        if (path.rfind("content/",0)!=0 || path.size()>512) continue;
        const std::string relative=path.substr(8);
        const fs::path relativePath(relative);
        if (relativePath.empty() || relativePath.is_absolute()) continue;
        bool allowed=true;
        fs::path current=root;
        for (const auto& segment:relativePath) {
            if (segment.empty() || segment=="." || segment=="..") {allowed=false;break;}
            current/=segment;
            std::error_code ec;
            const auto status=fs::symlink_status(current,ec);
            if (ec || fs::is_symlink(status)) {allowed=false;break;}
        }
        if (!allowed || !fs::is_regular_file(current)) continue;
        if (modelInstances_.size()>=2000) break;
        active.insert(path);
        std::error_code modifiedError;
        const auto modified=fs::last_write_time(current,modifiedError);
        if (modifiedError) continue;
        const auto previousTime=documentModelTimes_.find(path);
        if (previousTime!=documentModelTimes_.end() &&
            previousTime->second!=modified) {
            auto old=documentModels_.find(path);
            if (old!=documentModels_.end()) {
                releasePreviewModel(*old->second);
                documentModels_.erase(old);
            }
            rejectedDocumentModels_.erase(path);
        }
        documentModelTimes_[path]=modified;
        if (!documentModels_.count(path) && !rejectedDocumentModels_.count(path)) {
            if (documentModels_.size()>=512) continue;
            // Model loaders may request .bin, textures, MTL beside the model.
            // Resolve each request with the same root/symlink checks.
            auto read=[root](const std::string& name,std::string& out)->bool {
                if (name.rfind("content/",0)!=0 || name.size()>512) return false;
                const fs::path rel(name.substr(8));
                if (rel.empty() || rel.is_absolute()) return false;
                fs::path file=root;
                for(const auto& part:rel) {
                    if(part.empty()||part=="."||part=="..") return false;
                    file/=part;
                    std::error_code ec;
                    const auto st=fs::symlink_status(file,ec);
                    if(ec || fs::is_symlink(st)) return false;
                }
                if (!fs::is_regular_file(file)) return false;
                std::error_code sizeError;
                const auto bytes=fs::file_size(file,sizeError);
                if (sizeError || bytes>256ull*1024ull*1024ull) return false;
                std::ifstream input(file,std::ios::binary);
                if (!input) return false;
                out.assign(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
                return bool(input) || input.eof();
            };
            try {
                auto model=render::loadModel(path,read);
                if (!model || model->skinned() || model->vertices.empty() ||
                    model->indices.empty()) {
                    rejectedDocumentModels_.insert(path);
                    continue;
                }
                bool deformable=false;
                for (const auto& mesh:model->meshes) deformable |= mesh.deformable;
                // Rigid opaque/masked models render with their authored LODs.
                // CPU picking uses base geometry; alpha holes/LOD silhouette
                // are approximate until GPU ID/depth picking is available.
                bool nonOpaque=false;
                for(const auto& material:model->materials)
                    nonOpaque |= material.alpha==render::AlphaMode::Blend ||
                                 material.transparent || material.baseColor.w<.999f;
                if (deformable || nonOpaque ||
                    model->vertices.size()>1000000 || model->indices.size()>3000000) {
                    rejectedDocumentModels_.insert(path);
                    continue;
                }
                model->batching.autoLod=false;
                model->batching.mergeRigid=false;
                model->batching.allowGeometryPool=false;
                render::prepareModelUpload(*model);
                if (!render::uploadModel(*device_,*model,true)) {
                    releasePreviewModel(*model);
                    rejectedDocumentModels_.insert(path);
                    continue;
                }
                documentModels_.emplace(path,std::move(model));
            } catch (const std::exception&) {
                // Invalid resource stays a visible proxy rather than hiding
                // the map object or pretending a model was rendered.
                rejectedDocumentModels_.insert(path);
                continue;
            }
        }
        auto found=documentModels_.find(path);
        if (found==documentModels_.end()) continue;
        // Anchor model to the same authored entity location as its proxy.
        // The source file's own node transforms are applied by drawModel and picker.
        const Json& overrides=entity.extra["components"]["StaticModel"]["materials"];
        if(overrides.isObject() && overrides.size()) {
            // Model pointer: a hot-reloaded file gets a new Model and must re-resolve.
            const std::string signature=path+"|"+std::to_string(reinterpret_cast<uintptr_t>(found->second.get()))+"|"+overrides.dump(-1);
            auto& cached=instanceMaterials_[proxy.id];
            if(cached.signature!=signature || cached.materials.size()<found->second->materials.size()) {
                cached.signature=signature;
                cached.materials=found->second->materials;
                Json authored=Json::object();authored.set("materials",overrides);
                const auto plan=view::AppearanceChoiceAuthoring::resolve(*found->second,authored);
                for(size_t i=0;i<plan.materialLayers.size() && i<cached.materials.size();++i)
                    for(const auto& layer:plan.materialLayers[i])
                        view::AppearanceAuthoringService::applyMaterial(cached.materials[i],materialLibrary_,layer);
            }
            overridden.insert(proxy.id);
        }
        modelInstances_.push_back({proxy.id,found->second.get(),
           editor::authoredEntity(entity)?editor::authoredModelMatrix(entity):
           render::mat4::translate({proxy.center.x,proxy.center.y-proxy.half.y,proxy.center.z})*
           render::mat4::rotateY(proxy.yaw)});
    }
    for (auto it=instanceMaterials_.begin();it!=instanceMaterials_.end();) {
        if (!overridden.count(it->first)) it=instanceMaterials_.erase(it);
        else ++it;
    }
    for (auto it=documentModelTimes_.begin();it!=documentModelTimes_.end();) {
        if (!active.count(it->first)) it=documentModelTimes_.erase(it);
        else ++it;
    }
    for (auto it=rejectedDocumentModels_.begin();it!=rejectedDocumentModels_.end();) {
        if (!active.count(*it)) it=rejectedDocumentModels_.erase(it);
        else ++it;
    }
    for (auto it=documentModels_.begin();it!=documentModels_.end();) {
        if (!active.count(it->first)) {
            releasePreviewModel(*it->second);
            it=documentModels_.erase(it);
        } else ++it;
    }
}

StudioMetalScene::~StudioMetalScene() {
    if (device_) {
        device_->waitForResourceIdle();
        clearDocumentModels();
        lightingRuntime_.shutdown(*device_,renderer_);
        if(lightingModel_) {
            renderer_.invalidateModelCachesFor(lightingModel_.get());
            releaseModel(*device_,*lightingModel_);lightingModel_.reset();
        }
        post_.shutdown();
        (void)facialGpu_.clear(*device_,renderer_);
        if(studioCyclorama_.vb.valid())device_->destroyBuffer(studioCyclorama_.vb);
        if(studioCyclorama_.ib.valid())device_->destroyBuffer(studioCyclorama_.ib);
        if (cube_.vb.valid()) device_->destroyBuffer(cube_.vb);
        if (cube_.ib.valid()) device_->destroyBuffer(cube_.ib);
        if (materialSphere_) {
            releasePreviewModel(*materialSphere_);
            materialSphere_.reset();
        }
        if (terrain_.vb.valid()) device_->destroyBuffer(terrain_.vb);
        if (terrain_.ib.valid()) device_->destroyBuffer(terrain_.ib);
        nereid_.shutdown(*device_);
        antialiasing_.shutdown();
        renderer_.shutdown();
        if(deviceLifetime_)deviceLifetime_->retire();
    }
}
bool StudioMetalScene::sculptPick(float x,float y,float width,float height,
        const vivid::HumanFaceShape& selection,vivid::HumanFaceHit& hit,std::string* error) const noexcept {
    try {
    float3 origin{},direction{};
    // The facial preview renders with camera_ (no tween/ortho view override).
    if(!facialMode_ || !cameraInitialized_ ||
       !camera_.screenRay(x,y,width,height,origin,direction))return false;
    return facialGpu_.sculptRaycast(origin,direction,selection,hit);
    } catch(const std::exception& ex) {
        if(error)*error=ex.what();
        return false;
    } catch(...) {
        if(error)*error="Sculpt pick failed";
        return false;
    }
}
std::string StudioMetalScene::pick(float x, float y, float width, float height,
                                   const editor::MapDoc& map, bool fpsProject) const {
    float3 origin{}, direction{};
    // Facial preview is an imported Model, not an object in the active map.
    // Never route its clicks to unrelated MapDoc entities underneath.
    if (materialMode_ || facialMode_ || !cameraInitialized_ ||
        !viewCamera().screenRay(x,y,width,height,origin,direction)) return {};
    // Pick the oriented cube mesh that Renderer3D actually draws, not its
    // expanded axis-aligned bounding box. Empty rotated corners are unpickable.
    auto proxies=studioDocumentProxies(map,fpsProject,terrainLevelHeight_);
    proxies.erase(std::remove_if(proxies.begin(),proxies.end(),[&](const auto& proxy){
        const int index=studioMapEntityIndex(proxy.id,map);
        return index>=0 && map.data.entities[size_t(index)].extra["components"].has("NereidWater");
    }),proxies.end());
    // These are the exact model instances submitted by the previous frame.
    return studioPickSceneSurfaces(modelInstances_,proxies,origin,direction);
}
render::RTSCamera StudioMetalScene::viewCamera() const {
    render::RTSCamera view=camera_;
    view.zNear=std::clamp(camera_.distance*0.002f,0.05f,0.5f);
    view.zFar=std::max(1000.0f,camera_.distance*6.0f);
    if(view.ortho) {
        view.orthoHalfHeightOverride=std::max(0.01f,camera_.distance*std::tan(camera_.fovY*0.5f));
        view.distance=std::min(camera_.distance,400.0f);
    }
    return view;
}
void StudioMetalScene::startTween(render::float3 target,float distance,float yaw,float pitch) {
    tween_.active=true;
    tween_.fromTarget=camera_.target;tween_.toTarget=target;
    tween_.fromDistance=camera_.distance;tween_.toDistance=distance;
    tween_.fromYaw=camera_.yaw;
    // Shortest rotation; the stored yaw may have accumulated many turns.
    tween_.toYaw=camera_.yaw+std::remainder(yaw-camera_.yaw,6.28318530718f);
    tween_.fromPitch=camera_.pitch;tween_.toPitch=pitch;
    tween_.start=std::chrono::steady_clock::now();
}
void StudioMetalScene::stepTween() {
    if(!tween_.active)return;
    const float seconds=std::chrono::duration<float>(std::chrono::steady_clock::now()-tween_.start).count();
    const float t=std::clamp(seconds/0.24f,0.0f,1.0f);
    const float k=t*t*(3.0f-2.0f*t);
    camera_.target=tween_.fromTarget+(tween_.toTarget-tween_.fromTarget)*k;
    camera_.distance=tween_.fromDistance+(tween_.toDistance-tween_.fromDistance)*k;
    camera_.yaw=tween_.fromYaw+(tween_.toYaw-tween_.fromYaw)*k;
    camera_.pitch=tween_.fromPitch+(tween_.toPitch-tween_.fromPitch)*k;
    if(t>=1.0f)tween_.active=false;
}
void StudioMetalScene::setOrientation(float yaw,float pitch) {
    if(!std::isfinite(yaw)||!std::isfinite(pitch))return;
    startTween(camera_.target,camera_.distance,yaw,std::clamp(pitch,-1.5707963f,1.5707963f));
}
void StudioMetalScene::setOrthographic(bool ortho) { camera_.ortho=ortho; autoIso_=false; }
std::vector<std::string> StudioMetalScene::modelMaterialSlots(const std::string& asset) const {
    std::vector<std::string> names;
    const auto found=documentModels_.find(asset);
    if(found==documentModels_.end() || !found->second)return names;
    for(const auto& material:found->second->materials)
        if(!material.name.empty() && std::find(names.begin(),names.end(),material.name)==names.end())
            names.push_back(material.name);
    return names;
}
int StudioMetalScene::orientationGizmoHit(float x,float y,float width,float height) const {
    if(!orientationGizmo_ || materialMode_ || width<4 || height<4)return -1;
    const auto g=gizmoLayout(width,height);
    if(std::hypot(x-g.cx,y-g.cy)>g.r*1.45f)return -1;
    if(std::hypot(x-g.cx,y-g.cy)<g.r*0.26f)return 6;
    const auto b=gizmoBasis(camera_.yaw,camera_.pitch);
    int best=-1;float bestScore=1e9f;
    for(int arm=0;arm<6;++arm) {
        const float3 a=gizmoAxis(arm);
        const float tx=g.cx+dot(a,b.right)*g.r,ty=g.cy-dot(a,b.up)*g.r;
        const float d=std::hypot(x-tx,y-ty);
        // Prefer the arm nearer to the viewer when tips overlap.
        const float score=d+dot(a,b.forward)*g.r*0.15f;
        if(d<g.r*0.42f && score<bestScore){bestScore=score;best=arm;}
    }
    return best;
}
void StudioMetalScene::activateOrientationGizmo(int hit) {
    if(hit==6){setOrthographic(!camera_.ortho);return;}
    if(hit<0 || hit>5)return;
    const float sign=(hit&1)?-1.0f:1.0f;
    const int axis=hit/2;
    const float half=1.5707963f;
    if(axis==0)setOrientation(sign*half,0);
    else if(axis==1)setOrientation(0,sign*half);
    else setOrientation(sign>0?0.0f:3.14159265f,0);
    if(!camera_.ortho){camera_.ortho=true;autoIso_=true;}
}
void StudioMetalScene::drawOrientationGizmo(const render::RTSCamera& view,int width,int height) {
    // Drawn flat in screen space (like the HTML gizmo): every vertex is a pixel
    // position unprojected onto a plane just beyond the near plane, so the corner
    // placement never adds perspective skew. Overlays are depth-tested without
    // depth writes; this plane is in front of every scene surface.
    const float W=float(width),H=float(height);
    const auto g=gizmoLayout(W,H);
    const float depth=view.ortho?0.5f:view.zNear*1.5f;
    bool ok=true;
    auto at=[&](float x,float y)->float3 {
        float3 o,d;
        if(!view.screenRay(x,y,W,H,o,d)){ok=false;return {};}
        return o+d*depth;
    };
    auto tri=[&](float ax,float ay,float bx,float by,float cx,float cy,uint32_t col){
        const float3 p=at(ax,ay),q=at(bx,by),r=at(cx,cy);
        if(ok)renderer_.triangle(p,q,r,col);
    };
    auto quad=[&](float ax,float ay,float bx,float by,float cx,float cy,float dx,float dy,uint32_t col){
        tri(ax,ay,bx,by,cx,cy,col);tri(ax,ay,cx,cy,dx,dy,col);
    };
    auto segment=[&](float x0,float y0,float x1,float y1,float w,uint32_t col){
        const float lx=x1-x0,ly=y1-y0,l=std::hypot(lx,ly);
        if(l<1e-3f)return;
        const float nx=-ly/l*w*.5f,ny=lx/l*w*.5f;
        quad(x0+nx,y0+ny,x1+nx,y1+ny,x1-nx,y1-ny,x0-nx,y0-ny,col);
    };
    auto disc=[&](float cx,float cy,float radius,uint32_t col,int segments){
        for(int i=0;i<segments;++i){
            const float a0=6.2831853f*float(i)/segments,a1=6.2831853f*float(i+1)/segments;
            tri(cx,cy,cx+std::cos(a0)*radius,cy+std::sin(a0)*radius,cx+std::cos(a1)*radius,cy+std::sin(a1)*radius,col);
        }
    };
    const float r=g.r,u=r/31.0f; // u: one HTML-gizmo pixel
    disc(g.cx,g.cy,r+11*u,rgba(27,43,65,150),32);
    const auto b=gizmoBasis(view.yaw,view.pitch);
    int order[6]={0,1,2,3,4,5};
    std::sort(order,order+6,[&](int l,int rr){return dot(gizmoAxis(l),b.forward)>dot(gizmoAxis(rr),b.forward);});
    const uint32_t colors[3]={rgba(232,102,95),rgba(134,207,108),rgba(95,151,238)};
    for(const int arm:order) {
        const float3 a=gizmoAxis(arm);
        const bool negative=(arm&1)!=0;
        const uint32_t col=negative?rgba(111,127,150):colors[arm/2];
        const float ax=dot(a,b.right)*r,ay=-dot(a,b.up)*r;
        const float len=std::hypot(ax,ay);
        const float tx=g.cx+ax,ty=g.cy+ay;
        if(len<=4*u){disc(tx,ty,7*u,col,16);continue;}
        const float ux=ax/len,uy=ay/len;
        const float cone=std::min(len,std::max(7*u,15*u*len/r));
        const float bx=tx-ux*cone,by=ty-uy*cone,half=6*u;
        segment(g.cx,g.cy,bx,by,2.4f*u,col);
        tri(tx,ty,bx-uy*half,by+ux*half,bx+uy*half,by-ux*half,col);
        if(negative)continue;
        // Axis letter beyond the tip, drawn as strokes.
        const float lx=tx+ux*9*u,ly=ty+uy*9*u,h=3.6f*u,w=2.4f*u,st=1.6f*u;
        if(arm/2==0){segment(lx-w,ly-h,lx+w,ly+h,st,col);segment(lx+w,ly-h,lx-w,ly+h,st,col);}
        else if(arm/2==1){segment(lx-w,ly-h,lx,ly,st,col);segment(lx+w,ly-h,lx,ly,st,col);segment(lx,ly,lx,ly+h,st,col);}
        else{segment(lx-w,ly-h,lx+w,ly-h,st,col);segment(lx+w,ly-h,lx-w,ly+h,st,col);segment(lx-w,ly+h,lx+w,ly+h,st,col);}
    }
    // Centre square: click toggles Persp/Iso (highlighted while Iso).
    const float c=6.5f*u;
    quad(g.cx-c,g.cy-c,g.cx+c,g.cy-c,g.cx+c,g.cy+c,g.cx-c,g.cy+c,
         view.ortho?rgba(130,181,255):rgba(214,224,240));
}

void StudioMetalScene::orbit(float dx, float dy) {
    tween_.active=false;
    if(autoIso_){camera_.ortho=false;autoIso_=false;}
    camera_.yaw += dx * 0.007f;
    camera_.pitch = std::clamp(camera_.pitch - dy * 0.007f, -1.52f, 1.52f);
}
void StudioMetalScene::zoom(float wheel) {
    tween_.active=false;
    // Unity-like dolly: zoom toward the pivot; at the minimum distance keep
    // moving forward by pushing the pivot instead of stopping.
    const float next=camera_.distance*std::exp(-wheel*0.035f);
    constexpr float kMinDistance=2.0f;
    if(next<kMinDistance && wheel>0) {
        const float cp=std::cos(camera_.pitch),sp=std::sin(camera_.pitch);
        const float3 forward{-std::sin(camera_.yaw)*cp,-sp,-std::cos(camera_.yaw)*cp};
        camera_.target+=forward*std::max(0.02f,kMinDistance*(1.0f-std::exp(-wheel*0.035f)));
        camera_.distance=kMinDistance;
        return;
    }
    camera_.distance = std::clamp(next, kMinDistance, 100000.0f);
}
void StudioMetalScene::look(float dx, float dy) {
    // First-person free look preserves the camera's world-space eye instead
    // of orbiting around its target. WASD movement then translates the eye.
    tween_.active=false;
    if(autoIso_){camera_.ortho=false;autoIso_=false;}
    const float3 position=camera_.eye();
    camera_.yaw += dx*0.007f;
    camera_.pitch=std::clamp(camera_.pitch-dy*0.007f,-1.52f,1.52f);
    const float cp=std::cos(camera_.pitch),sp=std::sin(camera_.pitch);
    const float3 fromTarget{std::sin(camera_.yaw)*cp,sp,std::cos(camera_.yaw)*cp};
    camera_.target=position-fromTarget*camera_.distance;
}
void StudioMetalScene::pan(float dx, float dy) {
    // Editor navigation follows camera orientation, not RTS ground-plane input.
    tween_.active=false;
    const float speed=std::max(0.005f,camera_.distance*0.0018f);
    const float yaw=camera_.yaw;
    const float3 right{std::cos(yaw),0,-std::sin(yaw)};
    const float3 up{-std::sin(yaw)*std::sin(camera_.pitch),
                    std::cos(camera_.pitch),
                    -std::cos(yaw)*std::sin(camera_.pitch)};
    camera_.target += right*(-dx*speed) + up*(dy*speed);
}
void StudioMetalScene::move(float right, float up, float forward) {
    tween_.active=false;
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
    for (const auto& instance:modelInstances_) {
        if (instance.id!=selectedId || !instance.model) continue;
        const auto lo=instance.model->boundsMin,hi=instance.model->boundsMax;
        float3 worldMin{1e30f,1e30f,1e30f},worldMax{-1e30f,-1e30f,-1e30f};
        for (int corner=0;corner<8;++corner) {
            const float3 local{corner&1?hi.x:lo.x,corner&2?hi.y:lo.y,corner&4?hi.z:lo.z};
            const auto p=instance.world.transformPoint(local);
            worldMin={std::min(worldMin.x,p.x),std::min(worldMin.y,p.y),std::min(worldMin.z,p.z)};
            worldMax={std::max(worldMax.x,p.x),std::max(worldMax.y,p.y),std::max(worldMax.z,p.z)};
        }
        const auto size=worldMax-worldMin;
        startTween((worldMin+worldMax)*.5f,
            std::clamp(std::max({size.x,size.y,size.z,1.0f})*2.5f,2.0f,100000.0f),camera_.yaw,camera_.pitch);
        return;
    }
    auto proxies=studioDocumentProxies(map,fpsProject,terrainLevelHeight_);
    proxies.erase(std::remove_if(proxies.begin(),proxies.end(),[&](const auto& proxy){
        const int index=studioMapEntityIndex(proxy.id,map);
        return index>=0 && map.data.entities[size_t(index)].extra["components"].has("NereidWater");
    }),proxies.end());
    for (const auto& proxy:proxies) {
        if (proxy.id != selectedId) continue;
        const float3 extent=proxy.boundsMax-proxy.boundsMin;
        const float size=std::max({extent.x,extent.y,extent.z,0.5f});
        startTween((proxy.boundsMin+proxy.boundsMax)*0.5f,std::clamp(size*3.0f,2.0f,100000.0f),
            camera_.yaw,camera_.pitch);
        return;
    }
}
void StudioMetalScene::resetView() {
    tween_.active=false;
    camera_.ortho=false;
    cameraInitialized_=false;
    camera_.pitch=0.85f;
    camera_.yaw=0.55f;
}
void StudioMetalScene::frame(int framebufferWidth, int framebufferHeight, const editor::MapDoc& doc,
                             bool fpsProject, const std::string& selectedId,
                             const std::string& previewName, const Json& previewFactors,
                             uint64_t previewRevision, float levelHeight,
                             const std::string& projectPath,const Json& rules,
                             const StudioFacialPreviewBinding* facialBindingIn) {
    static const StudioFacialPreviewBinding kNoFacialBinding{};
    const StudioFacialPreviewBinding& facialBinding=facialBindingIn?*facialBindingIn:kNoFacialBinding;
    const std::string& projectRoot=projectPath;
    facialReady_=false;
    facialError_.clear();
    if (!device_ || framebufferWidth < 2 || framebufferHeight < 2) return;
    if(antialiasing_.pendingTargetRetirements())antialiasing_.collectRetiredTargets();
    if (renderer_.pendingProgramRetirements()) renderer_.collectRetiredPrograms();
    if (renderer_.pendingTargetRetirements()) renderer_.collectRetiredTargets();
    if (renderer_.hasPendingModelPipelineJobs()) renderer_.pollPendingModelPipelineJobs();
    const bool materialRequested = !previewName.empty() && previewFactors.isObject();
    const bool facialRequested = facialBinding.bound() && !materialRequested;
    if(facialMode_!=facialRequested) {
        facialMode_=facialRequested;
        shadowResolution_={};
        if(facialRequested)renderer_.setShadowQuality(ShadowQuality::High);
        cameraInitialized_=false;
        facialFramedGuid_.clear();
        // Facial preview overwrites renderer_.lighting; re-apply map lighting after it.
        lightingRevision_=std::numeric_limits<uint64_t>::max();
    }
    // A real imported Model is drawn through the SAME Metal Renderer3D used
    // for native materials and the map; no HTML fake character, no second
    // Animator/Graph clock. CPU import is asynchronous; GPU publication and
    // retirement remain on this owner thread before frame submissions.
    if(!facialRequested){renderer_.setModelLodEnabled(true);renderer_.clearShadowBounds();renderer_.setShadowQuality(ShadowQuality::Off);renderer_.lights.setQuality(LightQuality::Medium);}
    if(facialRequested) {
        renderer_.setModelLodEnabled(facialBinding.previewAnimation.automaticLod);
        std::string reason;
        const bool synced=facialGpu_.sync(*device_,renderer_,projectRoot,facialBinding,reason);
        const std::string loading=synced?std::string{}:reason;
        if(synced || facialGpu_.showsCharacter(facialBinding)) {
            // Spans both pose evaluation and the deferred renderer draw list.
            const auto modelRead=facialGpu_.acquireRead();
            if(!modelRead) {
                facialError_="Facial Model read lease unavailable";
                return;
            }
            // Frame every newly shown character once; later edits of the
            // same character keep the artist's camera.
            if(!cameraInitialized_ || facialFramedGuid_!=facialBinding.guid()) {
                camera_.target={0,0,0};
                camera_.distance=4.2f;
                camera_.pitch=.15f;
                camera_.yaw=.0f;
                camera_.ortho=false;
                tween_.active=false;
                cameraInitialized_=true;
                facialFramedGuid_=facialBinding.guid();
                facialCameraSerial_=facialBinding.cameraSerial;
            }
            if(facialCameraSerial_!=facialBinding.cameraSerial) {
                // The preview normalizes the character to 2 units tall around
                // the origin: head near +0.85, chest near +0.4.
                facialCameraSerial_=facialBinding.cameraSerial;
                const auto& v=facialBinding.cameraView;
                camera_.target=v=="face"?float3{0,.82f,0}:v=="torso"?float3{0,.4f,0}:float3{0,0,0};
                camera_.distance=v=="face"?.85f:v=="torso"?2.f:4.2f;
                camera_.pitch=v=="face"?.05f:.15f;camera_.yaw=0;camera_.ortho=false;tween_.active=false;
            }
            device_->beginFrame(framebufferWidth,framebufferHeight);
            renderer_.beginScene(camera_,framebufferWidth,framebufferHeight);
            renderer_.clearDynamicGiOverlay(); // map GI must not leak into the isolated facial preview
            const auto* model=facialGpu_.model();
            // Match the exact normalization used by StudioFacialGpuPreview::submit.
            const auto extent=model->boundsMax-model->boundsMin;
            const float scale=2.f/std::max({extent.x,extent.y,extent.z,.0001f});
            const float h=std::max(.01f,extent.y*scale);
            const float3 foot{0,-extent.y*.5f*scale,0};
            vivid::applyHumanStudioLighting(renderer_,facialBinding.studioLighting,foot,h);
            vivid::updateHumanStudioShadows(renderer_,shadowResolution_,h,framebufferHeight);
            const auto bg=facialBinding.studioLighting.rig().background;
            if(facialBinding.studioLighting.backdrop)renderer_.drawMesh(studioCyclorama_,mat4::trs(foot,{}, {h,h,h}),{bg.x,bg.y,bg.z,1},kDrawNoCastShadow,0);

            const bool submitted=facialGpu_.submit(
                renderer_,facialBinding,1.f/60.f,reason);
            gfx::PassDesc pass;
            pass.clearColor[0]=bg.x;
            pass.clearColor[1]=bg.y;
            pass.clearColor[2]=bg.z;
            pass.clearColor[3]=1.f;
            renderer_.render(pass,&antialiasing_);
            renderer_.resolvePostFx();
            device_->endPass();
            device_->endFrame();
            facialReady_=submitted && synced;
            facialError_=!submitted?reason:loading;
            return;
        }
        // Failed bind never displays stale model from a different GUID.
        // The ordinary map remains visible while a diagnostic is returned.
        facialError_=reason;
        cameraInitialized_=false;
    }else if(!facialBinding.bound()) {
        if(!facialGpu_.clear(*device_,renderer_))
            facialError_="Previous facial Model retirement lacks GPU completion";
    }
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
        lightingRevision_=std::numeric_limits<uint64_t>::max();
        cameraInitialized_=false;
    }
    if (materialMode_) {
        lightingStatus_="Material preview · GI: off · Direct: standard";
        if (!cameraInitialized_) {
            camera_.target={0,0,0};
            camera_.distance=4.3f;
            camera_.pitch=.5f;
            camera_.yaw=.25f;
            camera_.ortho=false;
            tween_.active=false;
            cameraInitialized_=true;
        }
        device_->beginFrame(framebufferWidth,framebufferHeight);
        StudioFrameScope scope{*device_};
        renderer_.beginScene(viewCamera(),framebufferWidth,framebufferHeight);
        renderer_.clearDynamicGiOverlay();
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
        renderer_.render(previewPass,&antialiasing_);
        renderer_.resolvePostFx();

        return;
    }
    const auto& map = doc.data;
    const bool showTerrain=rules["terrain"].getBool("render",true)&&map.extra.getStr("terrainPresentation","legacy")!="entities";
    const auto proxies=studioDocumentProxies(doc,fpsProject,levelHeight);
    syncDocumentModels(doc,projectPath,rules,proxies);

    if (terrainRevision_ != doc.revision || terrainLevelHeight_ != levelHeight || (showTerrain && !terrain_.vb.valid())) {
        lightingRevision_=std::numeric_limits<uint64_t>::max();
        terrainLevelHeight_ = levelHeight;
        MeshBuilder source;
        Mesh replacement;
        if(showTerrain){const auto stats=buildTerrainPreview(map,source,levelHeight);if(stats.quads)replacement=source.upload(*device_);}
        if (terrain_.vb.valid()) device_->destroyBuffer(terrain_.vb);
        if (terrain_.ib.valid()) device_->destroyBuffer(terrain_.ib);
        terrain_ = replacement;
        terrainRevision_ = doc.revision;
    }
    const Json settings=renderLightingSettings((fpsProject?doc.data.fps:doc.data.extra)["renderLighting"]);
    const bool useLighting=settings.getStr("indirect")!="off" || settings.getStr("direct")!="legacy";
    const auto activeLights=studioAuthoredLights(doc,fpsProject,levelHeight,std::numeric_limits<size_t>::max());
    Lighting environment;
    const auto day=previewDocumentDay(doc,fpsProject);
    const Json& lightingSource=fpsProject ? doc.data.fps["lighting"] : doc.data.extra["lighting"];
    if(lightingSource.isObject()) {
        environment.sunIntensity=day.sun;
        environment.skyColor={.5f*day.ambient,.58f*day.ambient,.7f*day.ambient};
    }
    if(lightingRevision_!=doc.revision) {
        if(!lightingRuntime_.shutdown(*device_,renderer_) || !device_->waitForResourceIdle())
            throw std::runtime_error("Studio lighting retirement failed");
        if(lightingModel_) {
            renderer_.invalidateModelCachesFor(lightingModel_.get());
            releaseModel(*device_,*lightingModel_);lightingModel_.reset();
        }
        lightingRevision_=doc.revision;
        lightingStatus_="Preview geometry · GI: off · Direct: standard";
        if(useLighting || fpsProject) {
            std::set<std::string> modelIds;
            for(const auto& instance:modelInstances_)modelIds.insert(instance.id);
            lightingModel_=studioLightingPreview(doc,fpsProject,levelHeight,modelIds);
            StaticShadowGeometry geometry;
            if(lightingModel_) {
                prepareModelUpload(*lightingModel_);
                geometry=giModelGeometry(*lightingModel_);
                if(!uploadModel(*device_,*lightingModel_,true))throw std::runtime_error("Studio lighting upload failed");
            }
            const float3 center=lightingModel_?float3{(lightingModel_->boundsMin.x+lightingModel_->boundsMax.x)*.5f,
                lightingModel_->boundsMin.y+.25f,(lightingModel_->boundsMin.z+lightingModel_->boundsMax.z)*.5f}:float3{map.w*.5f,.25f,map.h*.5f};
            try {
                // Reuse the shared immutable opaque-geometry snapshot for real
                // assets. Reject an oversized snapshot explicitly; never crop it
                // to a misleading subset or substitute proxy geometry silently.
                if(useLighting)for(const auto& instance:modelInstances_) {
                    size_t faces=0;
                    for(const auto& node:instance.model->nodes)if(node.mesh>=0)
                        for(const auto& p:instance.model->meshes[size_t(node.mesh)].primitives) {
                            const auto& material=instance.model->materials[size_t(p.material)];
                            if(material.castShadow&&material.alpha==AlphaMode::Opaque&&!material.transparent)faces+=p.indexCount/3;
                        }
                    if(faces>1000000 || geometry.triangles.size()>1000000-faces)throw std::runtime_error("Studio GI snapshot exceeds one million opaque triangles");
                    auto source=giModelGeometry(*instance.model,instance.world);
                    const auto offset=uint32_t(geometry.albedos.size());
                    geometry.albedos.insert(geometry.albedos.end(),source.albedos.begin(),source.albedos.end());
                    for(auto triangle:source.triangles){triangle.surface+=offset;geometry.triangles.push_back(triangle);}
                }
                if(useLighting)lightingRuntime_.initialize(*device_,renderer_,post_,settings,geometry,activeLights,center,environment);
            } catch(const std::exception& e) {
                const std::string reason=e.what();
                if(!lightingRuntime_.shutdown(*device_,renderer_))throw;
                lightingStatus_="Lighting rejected: "+reason+" · standard preview retained";
            }
        }
    }
    if (!cameraInitialized_ || lastMapWidth_ != map.w || lastMapHeight_ != map.h) {
        camera_.target = {map.w * .5f, 0.0f, map.h * .5f};
        camera_.distance = std::clamp(float(std::max(map.w, map.h)) * 1.4f, 16.0f, 100000.0f);
        if(fpsProject && lightingModel_) {
            const auto lo=lightingModel_->boundsMin,hi=lightingModel_->boundsMax;
            camera_.target=(lo+hi)*.5f;
            camera_.distance=std::clamp(std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z})*1.4f,16.f,100000.f);
        }
        tween_.active = false;
        cameraInitialized_ = true;
        lastMapWidth_ = map.w;
        lastMapHeight_ = map.h;
    }
    stepTween();
    const auto view=viewCamera();
    device_->beginFrame(framebufferWidth, framebufferHeight);
    StudioFrameScope scope{*device_};
    nereid_.configure(fpsProject?Json::object():editor::authoredWaterDocument(doc));
    nereid_.prepare(*device_,view.eye());
    const double opticalNow=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    nereid_.updateUnderwater(post_,underwaterFrameTime_>0?float(opticalNow-underwaterFrameTime_):1.f/60.f);
    underwaterFrameTime_=opticalNow;
    renderer_.beginScene(view, framebufferWidth, framebufferHeight);
    renderer_.setSceneSelectionOutline(true);
    renderer_.lighting=environment;
    if (lightingSource.isObject()) {
        renderer_.lights.setNightGlow(day.night*1.5f);
        renderer_.lights.setClouds(day.clouds,.6f,16.f,0,0);
    }
    if(!lightingRuntime_.direct || !lightingRuntime_.direct->enabled)
        for(const auto& light:activeLights)renderer_.lights.add(light);
    lightingRuntime_.update(*device_,renderer_,camera_.target);
    if(lightingRuntime_.indirect || lightingRuntime_.direct)lightingStatus_=lightingRuntime_.status();

    // Real map dimensions and positions from the active native Workspace.
    // Temporary primitive preview (NOT legacy editor terrain, animation or materials).
    const float w = float(std::max(map.w, 1));
    const float h = float(std::max(map.h, 1));
    if(lightingModel_) {
        ModelDraw draw;draw.model=lightingModel_.get();renderer_.drawModel(draw);
    }
    if(showTerrain && !lightingModel_){
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
    for (int x=0; !fpsProject && x<=map.w; x+=xStep)
        renderer_.line({float(x),.01f,0},{float(x),.01f,h},rgba(55,84,109,120));
    for (int z=0; !fpsProject && z<=map.h; z+=zStep)
        renderer_.line({0,.01f,float(z)},{w,.01f,float(z)},rgba(55,84,109,120));

    } // external/Terra surface suppresses the default plane and grid

    for(const auto& entity:proxies) {
        const int sourceIndex=studioMapEntityIndex(entity.id,doc);
        if(sourceIndex>=0 && doc.data.entities[size_t(sourceIndex)].extra["components"].has("NereidWater"))continue;
        bool hasModel=false;
        for(const auto& instance:modelInstances_) if(instance.id==entity.id) {hasModel=true;break;}
        if (hasModel) continue;
        if(!lightingModel_&&!entity.light){
            const auto transform=mat4::translate(entity.center)*mat4::rotateY(entity.yaw)*mat4::scale(entity.half*2.0f);
            if(entity.id==selectedId)renderer_.drawSelectionMesh(cube_,transform,entity.color,{.25f,.62f,1.f,1.f});
            else renderer_.drawMesh(cube_,transform,entity.color,kDrawNoCastShadow);
        }

    }
    for(const auto& instance:modelInstances_) {
        render::ModelDraw draw;
        draw.model=instance.model;
        draw.world=instance.world;
        if(auto custom=instanceMaterials_.find(instance.id);custom!=instanceMaterials_.end())
            draw.materials=&custom->second.materials;
        if(instance.id==selectedId) draw.outline={.25f,.62f,1.0f,.035f};
        renderer_.drawModel(draw);
    }
    nereid_.draw(renderer_);
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
    if(orientationGizmo_)drawOrientationGizmo(view,framebufferWidth,framebufferHeight);
    gfx::PassDesc pass;
    pass.clearColor[0] = 0.038f; pass.clearColor[1] = 0.064f;
    pass.clearColor[2] = 0.098f; pass.clearColor[3] = 1.0f;
    renderer_.render(pass,useLighting?lightingRuntime_.pipeline(post_):static_cast<PostProcess*>(&post_));
    renderer_.resolvePrisma();
    lightingRuntime_.presented();

    if(lodDiagnostics_&&++diagnosticFrames_%120==0){
        const auto& stats=renderer_.modelStats();
        std::fprintf(stderr,"[studio-lod] instances=%zu assets=%zu cameraTriangles=%llu shadowTriangles=%u lod=%llu/%llu/%llu/%llu chains=%llu blocked=%llu shadowLimited=%llu\n",
            modelInstances_.size(),documentModels_.size(),(unsigned long long)renderer_.profile().cameraModelTriangles,renderer_.profile().triangles[0],
            (unsigned long long)stats.cameraLodNodes[0],(unsigned long long)stats.cameraLodNodes[1],(unsigned long long)stats.cameraLodNodes[2],(unsigned long long)stats.cameraLodNodes[3],
            (unsigned long long)stats.cameraLodChains,(unsigned long long)stats.cameraLodBlocked,(unsigned long long)stats.cameraShadowLimitedLod);
    }
}
}
