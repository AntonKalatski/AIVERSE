#include "Renderer3D.h"
#include "Shaders.h"
#include "SelectionOutlineShaders.h"
#include "Renderer3DMotionShaders.h"
#include "Renderer3DReactiveShaders.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>

namespace aiverse::render {

namespace {
constexpr float kPi = 3.14159265358979f;

struct DrawUniforms { float model[16]; float color[4]; float params[4]; };
struct FrameUniforms {
    float viewProj[16];
    float lightViewProj[16];
    float invViewProj[16];
    float sunDir[4], sunColor[4], skyColor[4], groundColor[4], fog[4], camPos[4], shadow[4], quality[4];
    float trace0[4], trace1[4], trace2[4], trace3[4];   // Shaders.h: traced terrain shadows + SSR
};
void put3(float* d, float3 v, float w) { d[0] = v.x; d[1] = v.y; d[2] = v.z; d[3] = w; }
} // namespace

// ============================== Renderer3D ==============================

ShadowQuality Renderer3D::defaultQualityFor(const gfx::Device& dev, bool mobile) {
    if (const char* q = std::getenv("AIVERSE_SHADOWS")) { // testing override: off|low|medium|high
        std::string v = q;
        return v == "off" ? ShadowQuality::Off : v == "low" ? ShadowQuality::Low : v == "high" ? ShadowQuality::High : ShadowQuality::Medium;
    }
    const std::string& n = dev.info().name;
    if (n.find("llvmpipe") != std::string::npos || n.find("SwiftShader") != std::string::npos) return ShadowQuality::Low;
    if (mobile) {
        // Old/weak mobile GPUs get the cheapest shadows; everything else medium.
        for (const char* weak : {"Mali-4", "Mali-T", "Adreno (TM) 3", "Adreno (TM) 4", "Adreno (TM) 50", "PowerVR SGX", "PowerVR Rogue GE"})
            if (n.find(weak) != std::string::npos) return ShadowQuality::Low;
        return ShadowQuality::Medium;
    }
    return ShadowQuality::High;
}

bool Renderer3D::init(gfx::Device& dev) {
    dev_ = &dev;
    gfx::ShaderDesc ls; ls.glslVertex = shaders::kLitVS; ls.glslFragment = shaders::kLitFS; ls.msl = shaders::kLitMSL;
    ls.uniformSlots = 3; ls.textureSlots = 6; ls.label = "lit"; // U2/tex1/tex2: Lights (tex2 water reflection); tex3 height field, tex4/tex5 scene (trace)
    litShader_ = dev.createShader(ls);
    gfx::ShaderDesc ss; ss.glslVertex = shaders::kShadowVS; ss.glslFragment = shaders::kShadowFS; ss.msl = shaders::kShadowMSL;
    ss.uniformSlots = 2; ss.label = "shadow";
    shadowShader_ = dev.createShader(ss);
    gfx::ShaderDesc cs; cs.glslVertex = shaders::kColorVS; cs.glslFragment = shaders::kColorFS; cs.msl = shaders::kColorMSL;
    cs.uniformSlots = 2; cs.label = "color";
    colorShader_ = dev.createShader(cs);
    if (!litShader_.valid() || !shadowShader_.valid() || !colorShader_.valid()) return false;

    gfx::PipelineDesc mp;
    mp.shader = litShader_;
    mp.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
    mp.attrs[1] = {1, gfx::VertexFormat::Float3, 12};
    mp.attrs[2] = {2, gfx::VertexFormat::UByte4N, 24};
    mp.attrCount = 3; mp.stride = sizeof(MeshVertex);
    mp.indexType = gfx::IndexType::UInt32;
    litPipe_ = dev.createPipeline(mp);
    mp.blend = gfx::BlendMode::Alpha; mp.depthWrite = false;
    litBlendPipe_ = dev.createPipeline(mp);

    gfx::PipelineDesc sp;
    sp.shader = shadowShader_;
    sp.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
    sp.attrCount = 1; sp.stride = sizeof(MeshVertex);
    sp.indexType = gfx::IndexType::UInt32;
    sp.depthOnly = true;
    sp.cull = gfx::CullMode::Back;   // models are closed meshes; back faces would self-shadow less but cost more
    sp.depthBiasConstant = 2.0f; sp.depthBiasSlope = 2.5f;
    shadowPipe_ = dev.createPipeline(sp);

    gfx::PipelineDesc lp;
    lp.shader = colorShader_;
    lp.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
    lp.attrs[1] = {1, gfx::VertexFormat::UByte4N, 12};
    lp.attrCount = 2; lp.stride = sizeof(ColorVertex);
    lp.indexType = gfx::IndexType::None;
    lp.primitive = gfx::Primitive::Lines;
    lp.cull = gfx::CullMode::None;
    lp.blend = gfx::BlendMode::Alpha;
    lp.depthWrite = false;
    linePipe_ = dev.createPipeline(lp);
    lp.primitive = gfx::Primitive::Triangles;
    triPipe_ = dev.createPipeline(lp);

    gfx::BufferDesc vb; vb.usage = gfx::Usage::Dynamic; vb.size = 64 * 1024;
    lineVB_ = dev.createBuffer(vb);
    triVB_ = dev.createBuffer(vb);
    setShadowQuality(quality_);
    initModels();
    if (!lights.init(dev)) return false;
    if (!initTrace()) return false;
    initSelectionOutline();  // optional editor effect: failure must not break the game renderer
    return litPipe_.valid() && shadowPipe_.valid() && linePipe_.valid() && triPipe_.valid();
}

void Renderer3D::setShadowQuality(ShadowQuality q) {
    quality_ = q;
    if (!dev_) return;
    int size = q == ShadowQuality::High ? highShadowSize_ : q == ShadowQuality::Medium ? 1536 : 1024;
    if (q == ShadowQuality::Off) size = 0;
    if (size == shadowSize_ && shadowMap_.valid()) return;
    if (shadowMap_.valid()) dev_->destroyTexture(shadowMap_);
    shadowMap_ = {};
    shadowSize_ = size;
    gfx::TextureDesc td;
    td.width = td.height = std::max(size, 16);
    td.format = gfx::PixelFormat::Depth;
    td.renderTarget = true; td.compare = false; td.filter = gfx::Filter::Nearest; // raw depth for PCSS blocker search
    shadowMap_ = dev_->createTexture(td);
}

void Renderer3D::shutdown() {
    if (!dev_) return;
    gpuModelCommands_.release(*dev_);
    persistentCull_.release(*dev_);
    if (persistentUpload_) {
        persistentUpload_->release();
        persistentUpload_.reset();
    }
    persistentIds_.clear();
    persistentScene_ = {};
    for (auto& [id, history] : objectMotionViews_) {
        if (history.target.valid()) dev_->destroyTexture(history.target);
        if (history.depth.valid()) dev_->destroyTexture(history.depth);
        if (history.reactive.valid()) dev_->destroyTexture(history.reactive);
    }
    objectMotionViews_.clear();
    if (objectMotionPipeline_.valid()) dev_->destroyPipeline(objectMotionPipeline_);
    if (objectMotionShader_.valid()) dev_->destroyShader(objectMotionShader_);
    if (objectMotionInstancedPipeline_.valid()) dev_->destroyPipeline(objectMotionInstancedPipeline_);
    if (objectMotionInstancedShader_.valid()) dev_->destroyShader(objectMotionInstancedShader_);
    if (objectMotionDeformedPipeline_.valid()) dev_->destroyPipeline(objectMotionDeformedPipeline_);
    if (objectMotionDeformedShader_.valid()) dev_->destroyShader(objectMotionDeformedShader_);
    if (objectMotionBakedPipeline_.valid()) dev_->destroyPipeline(objectMotionBakedPipeline_);
    if (objectMotionBakedShader_.valid()) dev_->destroyShader(objectMotionBakedShader_);
    if (objectMotionMaskedPipeline_.valid()) dev_->destroyPipeline(objectMotionMaskedPipeline_);
    if (objectMotionMaskedShader_.valid()) dev_->destroyShader(objectMotionMaskedShader_);
    if (objectMotionReactivePipeline_.valid()) dev_->destroyPipeline(objectMotionReactivePipeline_);
    if (objectMotionReactiveMeshPipeline_.valid()) dev_->destroyPipeline(objectMotionReactiveMeshPipeline_);
    if (objectMotionReactiveShader_.valid()) dev_->destroyShader(objectMotionReactiveShader_);
    objectMotionPipeline_ = {}; objectMotionShader_ = {};
    objectMotionInstancedPipeline_ = {}; objectMotionInstancedShader_ = {};
    objectMotionDeformedPipeline_ = {}; objectMotionDeformedShader_ = {};
    objectMotionBakedPipeline_ = {}; objectMotionBakedShader_ = {};
    objectMotionMaskedPipeline_ = {}; objectMotionMaskedShader_ = {};
    objectMotionReactivePipeline_ = {}; objectMotionReactiveShader_ = {};
    objectMotionReactiveMeshPipeline_ = {};
    if (objectMotionReactiveSkinPipeline_.valid()) dev_->destroyPipeline(objectMotionReactiveSkinPipeline_);
    if (objectMotionReactiveSkinShader_.valid()) dev_->destroyShader(objectMotionReactiveSkinShader_);
    if (objectMotionReactiveBakedPipeline_.valid()) dev_->destroyPipeline(objectMotionReactiveBakedPipeline_);
    if (objectMotionReactiveBakedShader_.valid()) dev_->destroyShader(objectMotionReactiveBakedShader_);
    objectMotionReactiveSkinPipeline_ = {}; objectMotionReactiveSkinShader_ = {};
    objectMotionReactiveBakedPipeline_ = {}; objectMotionReactiveBakedShader_ = {};
    motionReactiveDraws_.clear();
    shutdownModels();
    lights.shutdown();
    shutdownTrace();
    dev_->destroyTexture(shadowMap_);
    dev_->destroyBuffer(lineVB_); dev_->destroyBuffer(triVB_);
    if (sceneSelectionMask_.valid()) dev_->destroyTexture(sceneSelectionMask_);
    if (sceneSelectionTriangle_.valid()) dev_->destroyBuffer(sceneSelectionTriangle_);
    if (sceneSelectionPrimitiveMaskPipe_.valid())
        dev_->destroyPipeline(sceneSelectionPrimitiveMaskPipe_);
    for (auto p : {sceneSelectionEdgePipe8_, sceneSelectionEdgePipeHdr_})
        if (p.valid()) dev_->destroyPipeline(p);
    if (sceneSelectionEdgeShader_.valid())
        dev_->destroyShader(sceneSelectionEdgeShader_);
    for (auto p : {litPipe_, litBlendPipe_, shadowPipe_, linePipe_, triPipe_}) dev_->destroyPipeline(p);
    for (auto s : {litShader_, shadowShader_, colorShader_}) dev_->destroyShader(s);
    dev_ = nullptr;
}

void Renderer3D::beginScene(const RTSCamera& cam, int fbW, int fbH) {
    static const bool enablePersistentScene = [] {
        const char* value = std::getenv("AIVERSE_PERSISTENT_GPU_SCENE");
        return value && std::strcmp(value, "1") == 0;
    }();
    persistentSceneEnabled_ = enablePersistentScene;
    if (persistentSceneEnabled_) ++persistentSceneEpoch_;
    bakedLightmapped_ = bakedCountLm_; bakedProbed_ = bakedCountPr_; bakedCountLm_ = bakedCountPr_ = 0;   // agent-lightbake
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(graphEpoch_ < 0) graphEpoch_ = now;
    graphTime_ = float(now - graphEpoch_);
    sceneW_ = fbW; sceneH_ = fbH; // viewport of the main pass (screen or an offscreen target of this size)
    float aspect = fbH > 0 ? float(fbW) / float(fbH) : 1.0f;
    viewProj_ = cam.proj(aspect, dev_->info().clipDepthZeroToOne) * cam.view();
    invViewProj_ = viewProj_.inverse();
    camPos_ = cam.eye();
    camFwd_ = normalize(cam.target - camPos_);
    cam_ = cam;
    ssrThisFrame_ = false;
    items_.clear(); lines_.clear(); tris_.clear(); custom_.clear(); customOpaque_.clear(); customShadow_.clear();
    motionReactiveDraws_.clear(); // per-scene extensions register after beginScene
    modelItems_.clear(); jointData_.clear(); morphData_.clear(); modelStats_ = {};
    lodDebugSample_ = {}; // selected instance is tracked per viewport beginScene
    // ModelItem pointers are valid only within this scene; never reuse their
    // material identity across frames, asset reloads or another viewport.
    materialStateIds_.clear();
    materialHashCandidates_.clear();
    materialRepresentatives_.clear();
    materialItemIds_.clear();
    geometryStats_ = {}; profile_ = {}; geometryMainView_ = true;
    RTSCamera shadowCamera = cam; shadowCamera.jitter = {};
    computeShadowMatrix(shadowCamera, aspect);
    geometryShadowSize_=shadowSize_;
    lights.begin(cam, aspect);
}

void Renderer3D::computeShadowMatrix(const RTSCamera& cam, float aspect) {
    // Fit an orthographic light frustum around the part of the ground the camera sees.
    float3 pts[8];
    int n = 0;
    const float w = 1000.0f, h = w / aspect;
    const float sx[4] = {0, w, 0, w}, sy[4] = {0, 0, h, h};
    for (int i = 0; i < 4; ++i) {
        float3 g;
        if (!cam.screenToGround(sx[i], sy[i], w, h, g) || length(g - cam.target) > cam.distance * 3.0f) {
            float3 o, d;
            cam.screenRay(sx[i], sy[i], w, h, o, d);
            float3 flatD = normalize(float3{d.x, 0, d.z});
            g = cam.target + flatD * (cam.distance * 1.6f);
        }
        pts[n++] = g;
        pts[n++] = g + float3{0, 5.0f, 0}; // tall objects
    }
    if (hasShadowBounds_) {
        n=0;
        for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)
            pts[n++]={x?shadowBoundsMax_.x:shadowBoundsMin_.x,y?shadowBoundsMax_.y:shadowBoundsMin_.y,z?shadowBoundsMax_.z:shadowBoundsMin_.z};
    }
    const float3 center=hasShadowBounds_?(shadowBoundsMin_+shadowBoundsMax_)*.5f:cam.target;
    float3 L = normalize(lighting.sunDir);
    float3 up = std::fabs(L.y) > 0.99f ? float3{0, 0, 1} : float3{0, 1, 0};
    mat4 view = mat4::lookAt(center + L * 60.0f, center, up);
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (int i = 0; i < n; ++i) {
        float3 p = view.transformPoint(pts[i]);
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
    }
    // texel snapping keeps shadow edges stable while the camera pans
    float size = std::max(maxX - minX, maxY - minY) + 2.0f;
    float texel = size / float(std::max(shadowSize_, 16));
    shadowTexelWorld_ = texel;
    shadowExtent_ = size;
    float cx = std::floor((minX + maxX) * 0.5f / texel) * texel, cy = std::floor((minY + maxY) * 0.5f / texel) * texel;
    mat4 proj = mat4::ortho(cx - size * 0.5f, cx + size * 0.5f, cy - size * 0.5f, cy + size * 0.5f,
                            -maxZ - 40.0f, -minZ + 10.0f, dev_->info().clipDepthZeroToOne);
    lightViewProj_ = proj * view;
}

void Renderer3D::drawMesh(const Mesh& m, const mat4& model, float4 color, uint32_t flags, float specular, float emissive) {
    items_.push_back({&m, model, color, flags, specular, emissive});
}

void Renderer3D::drawSelectionMesh(const Mesh& mesh, const mat4& model,
                                   float4 tint, float4 selection) {
    drawMesh(mesh, model, tint);
    if (!items_.empty()) items_.back().selection = selection;
}


void Renderer3D::line(float3 a, float3 b, uint32_t c) { lines_.push_back({a, c}); lines_.push_back({b, c}); }
void Renderer3D::triangle(float3 a, float3 b, float3 c, uint32_t col) { tris_.push_back({a, col}); tris_.push_back({b, col}); tris_.push_back({c, col}); }

void Renderer3D::quadXZ(float3 a, float3 b, float w, uint32_t c, float y) {
    float3 d = b - a; d.y = 0;
    float len = length(d);
    if (len < 1e-5f) return;
    float3 side = float3{-d.z, 0, d.x} * (w * 0.5f / len);
    a.y = b.y = y;
    triangle(a - side, b - side, b + side, c);
    triangle(a - side, b + side, a + side, c);
}

void Renderer3D::circleXZ(float3 c, float r, uint32_t col, int seg, float y) {
    for (int i = 0; i < seg; ++i) {
        float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        line({c.x + std::cos(a0) * r, y, c.z + std::sin(a0) * r}, {c.x + std::cos(a1) * r, y, c.z + std::sin(a1) * r}, col);
    }
}

void Renderer3D::uploadFrameUniforms(bool forShadow) {
    FrameUniforms f{};
    std::memcpy(f.viewProj, viewProj_.m, sizeof f.viewProj);
    std::memcpy(f.lightViewProj, lightViewProj_.m, sizeof f.lightViewProj);
    std::memcpy(f.invViewProj, invViewProj_.m, sizeof f.invViewProj);
    put3(f.sunDir, normalize(lighting.sunDir), lighting.sunIntensity);
    // trace tier: terrain shadows march the height field; SSR when the scene targets are bound this frame
    if (traceActive() && !forShadow) {
        const HeightField& h = heightField_;
        f.trace0[0] = h.originX; f.trace0[1] = h.originZ;
        f.trace0[2] = 1.0f / std::max(h.extentX, 1e-3f); f.trace0[3] = 1.0f / std::max(h.extentZ, 1e-3f);
        f.trace1[0] = h.heightMin; f.trace1[1] = h.heightRange;
        f.trace1[2] = std::max(lighting.terrainShadowDistance, 2.0f);
        f.trace1[3] = std::max(lighting.terrainShadowSoftness, 0.005f);
    }
    put3(f.trace2, camFwd_, ssrThisFrame_ && !forShadow ? (modelReflections_ ? 2.0f : 1.0f) : 0.0f);
    f.trace3[0] = 48.0f;    // SSR march distance (world)
    f.trace3[1] = 0.6f;     // thickness: a ray passing further behind a surface is a miss (thin objects)
    f.trace3[2] = 16.0f;    // steps (+4 refinement)
    f.trace3[3] = 0.08f;    // terrain shadow ray bias above the receiver (avoids self-shadowing on slopes)
    put3(f.sunColor, lighting.sunColor, 1);
    put3(f.skyColor, lighting.skyColor, lighting.tonemap ? 1.0f : 0.0f);   // w: tonemap switch of the lit shaders
    put3(f.groundColor, lighting.groundColor, lighting.srgbModels ? 2.0f : 1.0f);   // w: model output encoding (2 = sRGB)
    put3(f.fog, lighting.fogColor, lighting.fogDensity);
    put3(f.camPos, camPos_, graphTime_);
    const bool zeroToOne = dev_->info().clipDepthZeroToOne;
    f.shadow[0] = dev_->info().renderTargetFlipY ? 1.0f : 0.0f;
    f.shadow[1] = zeroToOne ? 1.0f : 0.5f;
    f.shadow[2] = zeroToOne ? 0.0f : 0.5f;
    f.shadow[3] = 1.0f / float(std::max(shadowSize_, 16));
    // More PCF samples preserve fine shadow silhouettes in wide penumbrae.
    // Low stays at four taps; medium/high retain the nine-sample blocker search.
    f.quality[0] = quality_ == ShadowQuality::High ? 24.0f : quality_ == ShadowQuality::Medium ? 12.0f : 4.0f;
    {
        // Maximum search/filter radius; PCSS can narrow contacts to half a texel.
        float radiusWorld = std::max(lighting.shadowSoftness, shadowTexelWorld_ * 1.5f);
        // Low (4 fixed taps): a wide radius would show 4 offset copies, keep it around two texels
        if (quality_ == ShadowQuality::Low) radiusWorld = std::min(radiusWorld, shadowTexelWorld_ * 2.0f);
        f.quality[2] = radiusWorld / std::max(shadowExtent_, 1.0f);
        // Keep receiver displacement tied to resolution, not penumbra width: thin folds must not detach.
        f.quality[3] = shadowTexelWorld_ * 1.5f;
    }
    // Fractional tap count encodes temporal PCSS sampling without changing U1 layout.
    // This flag is active only for the HDR ScreenLighting TAA path; static PCF stays stable elsewhere.
    if (!forShadow && quality_ >= ShadowQuality::Medium && post_ && post_->temporalShadowResolve())
        f.quality[0] += 0.5f;
    f.quality[1] = quality_ == ShadowQuality::Off || forShadow ? 0.0f : lighting.shadowContactHardening ? 2.0f : 1.0f;
    dev_->setUniforms(1, &f, sizeof f);
}

void Renderer3D::bindLitFrame() {
    uploadFrameUniforms(false);
    dev_->setTexture(0, shadowMap_);
    lights.bind();
}

void Renderer3D::bindTraceTextures() {
    bindTrace(3);
    bindSceneTextures(4, 5);
}

void Renderer3D::resetObjectMotionHistory(uint64_t viewId) {
    auto it = objectMotionViews_.find(viewId);
    if (it == objectMotionViews_.end()) return;
    it->second.previous.clear();
    it->second.previousDeformed.clear();
    it->second.valid = false;
}
void Renderer3D::resetAllObjectMotionHistory() {
    for (auto& [id, view] : objectMotionViews_) {
        view.previous.clear();
        view.previousDeformed.clear();
        view.valid = false;
    }
}

void Renderer3D::releaseObjectMotionViewAfterGpuIdle(uint64_t viewId) {
    // The caller must have waited for all submissions referencing the view.
    // We cannot infer this from CPU frameSerial() or viewport close events.
    auto it = objectMotionViews_.find(viewId);
    if (it == objectMotionViews_.end()) return;
    if (dev_) {
        if (it->second.target.valid()) dev_->destroyTexture(it->second.target);
        if (it->second.depth.valid()) dev_->destroyTexture(it->second.depth);
        if (it->second.reactive.valid()) dev_->destroyTexture(it->second.reactive);
    }
    objectMotionViews_.erase(it);
    if (objectMotionStats_.viewId == viewId) objectMotionStats_ = {};
}

void Renderer3D::renderObjectMotionVectors(PostProcess& post) {
    objectMotionStats_ = {};
    if (!post.wantsObjectMotionVectors() || !dev_) return;
    uint64_t viewId = 0;
    mat4 previousCamera;
    const bool previousCameraValid =
        post.objectMotionContext(viewId, previousCamera);
    auto& history = objectMotionViews_[viewId];
    objectMotionStats_.viewId = viewId;
    const uint64_t epoch = post.objectMotionEpoch(viewId);
    if (history.epoch != epoch) {
        history.previous.clear();
        history.previousDeformed.clear();
        history.valid = false;
        history.epoch = epoch;
    }

    // Collect current rigid-node transforms once, before any optional GPU pass.
    // Stable (entity,node) pairs are the identity: repeated primitives and
    // instancing batches cannot accidentally advance history multiple times.
    using Key = std::pair<uint64_t, int>;
    std::map<Key, MotionRigidPose> current;
    auto eligible = [](const ModelItem& it) {
        return it.motionPersistentId != 0 && it.motionNodeIndex >= 0 &&
            it.rigid && it.inView && it.tint.w >= 0.99f &&
            it.dissolve <= 0.0f && it.joints < 0 && !it.baked &&
            it.morph < 0 && (!it.prim || it.prim->morphTargets == 0 ||
            !it.model || !it.model->morphTex.valid()) &&
            it.materialRole != 1 && it.shadowRole != 2 &&
            it.lodThreshold >= 1.0f && !it.lodInvert &&
            it.mat && (it.mat->alpha == AlphaMode::Opaque ||
             it.mat->alpha == AlphaMode::Mask) && !it.mat->shaderGraph &&
            it.mat->team == TeamMode::None && !it.mat->transparent && !(it.flags & kDrawTransparent) &&
            it.model && !it.model->geometry && it.model->vb.valid() &&
            it.model->ib.valid() && it.indexCount > 0;
    };
    for (const auto& it : modelItems_)
        if (eligible(it))
            current.try_emplace(Key{it.motionPersistentId,it.motionNodeIndex},
                                MotionRigidPose{it.model,it.world});

    using DeformKey = std::tuple<uint64_t,int,size_t>;
    std::map<DeformKey, MotionDeformPose> currentDeformed;
    auto deformEligible = [&](const ModelItem& it) {
        if (!it.motionPersistentId || it.motionNodeIndex < 0 ||
            !it.inView || !it.model || !it.prim || !it.mat ||
            !it.model->vb.valid() || !it.model->ib.valid() ||
            it.model->geometry || it.indexCount == 0 ||
            it.mat->shaderGraph)
            return false;
        const bool hasMorph = it.prim->morphTargets > 0 &&
                              it.model->morphTex.valid();
        const bool hasSkin = it.joints >= 0;
        // Mixed baked-clip + morph uses the dedicated Stage 29 shader:
        // morph deltas are applied before texture-atlas skinning.
        const bool bakedEligible = it.baked && it.model->baked.tex.valid() &&
            it.prim->jointPalette.size() <= 80 &&
            (!hasMorph || it.prim->morphTargets <= kMaxMorphTargets);
        return (hasSkin || hasMorph || bakedEligible) &&
            !(it.baked && !bakedEligible) && it.tint.w >= 0.99f &&
            it.dissolve <= 0.0f && it.materialRole != 1 &&
            it.shadowRole != 2 && it.lodThreshold >= 1.0f &&
            !it.lodInvert && it.mat->alpha == AlphaMode::Opaque &&
            !it.mat->transparent && !(it.flags & kDrawTransparent);
    };
    for (const auto& it : modelItems_) {
        if (!deformEligible(it)) continue;
        MotionDeformPose pose;
        pose.world = it.world;
        pose.sourceModel = it.model;
        pose.hasSkin = it.joints >= 0;
        pose.baked = it.baked;
        pose.animation = it.anim;
        if (pose.hasSkin &&
            size_t(it.joints) + pose.joints.size() <= jointData_.size())
            std::memcpy(pose.joints.data(), jointData_.data() + it.joints,
                        pose.joints.size()*sizeof(float));
        else if (pose.hasSkin) continue;
        if (it.morph >= 0 &&
            size_t(it.morph) + pose.weights.size() <= morphData_.size())
            std::memcpy(pose.weights.data(),morphData_.data()+it.morph,
                        pose.weights.size()*sizeof(float));
        currentDeformed[{it.motionPersistentId,it.motionNodeIndex,
                         it.geometryPrimitive}] = std::move(pose);
    }

    // First frame, camera cut or viewport resize cannot use the previous
    // screen velocity. Seed the next frame without drawing any overlay.
    const int w = std::max(sceneW_,1), h = std::max(sceneH_,1);
    const bool resize = (history.width != 0 && history.height != 0) &&
                        (history.width != w || history.height != h);
    if (!previousCameraValid || resize || !history.valid) {
        history.valid = true;
        history.previous = std::move(current);
        history.previousDeformed = std::move(currentDeformed);
        if (resize) {
            if (history.target.valid()) dev_->destroyTexture(history.target);
            if (history.depth.valid()) dev_->destroyTexture(history.depth);
            if (history.reactive.valid()) dev_->destroyTexture(history.reactive);
            history.target = {}; history.depth = {}; history.reactive = {};
        }
        history.width = w; history.height = h;
        return;
    }
    const auto sceneDepth = post.objectMotionSceneDepth();
    if (!sceneDepth.valid() || !dev_->supports(gfx::PixelFormat::RGBA16F)) {
        history.previous = std::move(current);
        history.previousDeformed = std::move(currentDeformed);
        return;
    }

    if (history.width != w || history.height != h) {
        if (history.target.valid()) dev_->destroyTexture(history.target);
        if (history.depth.valid()) dev_->destroyTexture(history.depth);
        if (history.reactive.valid()) dev_->destroyTexture(history.reactive);
        history.target = {}; history.depth = {}; history.reactive = {};
        history.width = w; history.height = h;
    }
    if (!history.target.valid()) {
        gfx::TextureDesc desc;
        desc.width = w; desc.height = h;
        desc.renderTarget = true;
        desc.format = gfx::PixelFormat::RGBA16F;
        desc.filter = gfx::Filter::Nearest;
        desc.label = "prisma rigid motion velocity";
        history.target = dev_->createTexture(desc);
    }
    if (!history.depth.valid()) {
        gfx::TextureDesc desc;
        desc.width = w; desc.height = h;
        desc.renderTarget = true;
        desc.format = gfx::PixelFormat::Depth;
        desc.filter = gfx::Filter::Nearest;
        desc.label = "prisma rigid motion scratch depth";
        history.depth = dev_->createTexture(desc);
    }
    if (!objectMotionPipeline_.valid() && !objectMotionShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = motion_shaders::kVS;
        shader.glslFragment = motion_shaders::kFS;
        shader.msl = motion_shaders::kMSL;
        shader.textureSlots = 1; shader.uniformSlots = 1;
        shader.label = "prisma rigid motion overlay";
        objectMotionShader_ = dev_->createShader(shader);
        if (objectMotionShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionShader_;
            pipeline.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            pipeline.attrCount = 1;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA16F;
            pipeline.label = "prisma rigid motion overlay";
            objectMotionPipeline_ = dev_->createPipeline(pipeline);
        }
    }
    if (!objectMotionInstancedShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = motion_shaders::kInstancedVS;
        shader.glslFragment = motion_shaders::kInstancedFS;
        shader.msl = motion_shaders::kInstancedMSL;
        shader.textureSlots = 1;
        shader.uniformSlots = 1;
        shader.label = "prisma batched rigid velocity";
        objectMotionInstancedShader_ = dev_->createShader(shader);
        if (objectMotionInstancedShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionInstancedShader_;
            pipeline.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            pipeline.attrCount = 1;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA16F;
            pipeline.label = "prisma batched rigid velocity";
            objectMotionInstancedPipeline_ = dev_->createPipeline(pipeline);
        }
    }
    if (!objectMotionDeformedShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = motion_shaders::kDeformedVS;
        shader.glslFragment = motion_shaders::kFS;
        shader.msl = motion_shaders::kDeformedMSL;
        shader.textureSlots = 9; // original morph delta texture at slot 8
        shader.uniformSlots = 6; // U0 plus current/previous palette U3/U4 and morph U5
        shader.label = "prisma skinned morph velocity";
        objectMotionDeformedShader_ = dev_->createShader(shader);
        if (objectMotionDeformedShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionDeformedShader_;
            pipeline.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            pipeline.attrs[1] = {5, gfx::VertexFormat::UByte4,
                                 uint32_t(offsetof(ModelVertex,joints))};
            pipeline.attrs[2] = {6, gfx::VertexFormat::UByte4N,
                                 uint32_t(offsetof(ModelVertex,weights))};
            pipeline.attrCount = 3;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA16F;
            pipeline.label = "prisma skinned morph velocity";
            objectMotionDeformedPipeline_ = dev_->createPipeline(pipeline);
        }
    }
    if (!objectMotionBakedShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = motion_shaders::kBakedVS;
        shader.glslFragment = motion_shaders::kFS;
        shader.msl = motion_shaders::kBakedMSL;
        shader.textureSlots = 9; // texture 0 scene depth, 7 baked atlas, 8 morph deltas
        shader.uniformSlots = 6; // U0, U3 palette, U4 animation, U5 prior/current morph
        shader.label = "prisma baked animation velocity";
        objectMotionBakedShader_ = dev_->createShader(shader);
        if (objectMotionBakedShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionBakedShader_;
            pipeline.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            pipeline.attrs[1] = {5, gfx::VertexFormat::UByte4,
                                 uint32_t(offsetof(ModelVertex,joints))};
            pipeline.attrs[2] = {6, gfx::VertexFormat::UByte4N,
                                 uint32_t(offsetof(ModelVertex,weights))};
            pipeline.attrCount = 3;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA16F;
            pipeline.label = "prisma baked animation velocity";
            objectMotionBakedPipeline_ = dev_->createPipeline(pipeline);
        }
    }
    if (!objectMotionMaskedShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = motion_shaders::kMaskedVS;
        shader.glslFragment = motion_shaders::kMaskedFS;
        shader.msl = motion_shaders::kMaskedMSL;
        shader.textureSlots = 2; // sampled scene depth + alpha base texture
        shader.uniformSlots = 1;
        shader.label = "prisma alpha-cutout velocity";
        objectMotionMaskedShader_ = dev_->createShader(shader);
        if (objectMotionMaskedShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionMaskedShader_;
            pipeline.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            pipeline.attrs[1] = {2, gfx::VertexFormat::Float2,
                                 uint32_t(offsetof(ModelVertex,uv))};
            pipeline.attrs[2] = {4, gfx::VertexFormat::UByte4N,
                                 uint32_t(offsetof(ModelVertex,color))};
            pipeline.attrCount = 3;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA16F;
            pipeline.label = "prisma alpha-cutout velocity";
            objectMotionMaskedPipeline_ = dev_->createPipeline(pipeline);
        }
    }
    if (history.target.valid() && history.depth.valid()) {
        struct MotionU {
            float current[16], previous[16], params[4];
        };
        // The reactive pass is independent from object-velocity shaders:
        // transparent water/particles providers must still run if a rigid
        // motion pipeline is unavailable on a particular backend.
        if (objectMotionPipeline_.valid()) {
        gfx::PassDesc pass;
        pass.colorTarget = history.target;
        pass.depthTarget = history.depth; // NEVER sample and attach scene depth
        pass.clearColor[0] = pass.clearColor[1] = 0.0f;
        pass.clearColor[2] = pass.clearColor[3] = 0.0f; // B=0: no object
        pass.clearColorEnabled = true;
        pass.clearDepthEnabled = true;
        dev_->beginPass(pass);
        dev_->setViewport({0,0,w,h});
        dev_->setPipeline(objectMotionPipeline_);
        dev_->setTexture(0, sceneDepth);
        // Geometry-equivalent rigid draws can share one instanced velocity
        // submission even when their ordinary material states differ.
        // The source Model pointer is part of the key: model buffer IDs,
        // index offsets and base vertex must be identical in one batch.
        using BatchKey = std::tuple<const Model*, uint32_t, uint32_t>;
        std::map<BatchKey, std::vector<const ModelItem*>> batches;
        for (const auto& it : modelItems_) {
            if (!eligible(it) || it.mat->alpha == AlphaMode::Mask) continue;
            batches[{it.model,it.firstIndex,it.indexCount}].push_back(&it);
        }
        struct BatchU {
            float current[16][16];
            float previous[16][16];
            float params[4];
        };
        static_assert(sizeof(BatchU) <= 4096);
        const auto lastModel = [&](const ModelItem& it) {
            const Key key{it.motionPersistentId,it.motionNodeIndex};
            const auto previous = history.previous.find(key);
            return previous == history.previous.end() ||
                   previous->second.sourceModel != it.model
                ? it.world : previous->second.world;
        };
        for (const auto& [key, items] : batches) {
            for (size_t first = 0; first < items.size();) {
                const size_t n = objectMotionInstancedPipeline_.valid()
                    ? std::min<size_t>(16,items.size()-first) : 1;
                const ModelItem& sample = *items[first];
                if (n > 1) {
                    BatchU uniforms{};
                    uniforms.params[0] = float(w);
                    uniforms.params[1] = float(h);
                    uniforms.params[2] = dev_->info().renderTargetFlipY ? 1.0f : 0.0f;
                    for (size_t i=0; i<n; ++i) {
                        const ModelItem& it = *items[first+i];
                        const mat4 currentMvp = viewProj_ * it.world;
                        const mat4 previousMvp = previousCamera * lastModel(it);
                        std::memcpy(uniforms.current[i], currentMvp.m, sizeof currentMvp.m);
                        std::memcpy(uniforms.previous[i], previousMvp.m, sizeof previousMvp.m);
                    }
                    dev_->setPipeline(objectMotionInstancedPipeline_);
                    dev_->setUniforms(0,&uniforms,sizeof uniforms);
                } else {
                    const mat4 currentMvp = viewProj_ * sample.world;
                    const mat4 previousMvp = previousCamera * lastModel(sample);
                    MotionU uniforms{};
                    std::memcpy(uniforms.current,currentMvp.m,sizeof uniforms.current);
                    std::memcpy(uniforms.previous,previousMvp.m,sizeof uniforms.previous);
                    uniforms.params[0] = float(w);
                    uniforms.params[1] = float(h);
                    uniforms.params[2] = dev_->info().renderTargetFlipY ? 1.0f : 0.0f;
                    dev_->setPipeline(objectMotionPipeline_);
                    dev_->setUniforms(0,&uniforms,sizeof uniforms);
                }
                dev_->setVertexBuffer(sample.model->vb);
                dev_->setIndexBuffer(sample.model->ib);
                dev_->drawIndexed(sample.indexCount,
                    sample.firstIndex + sample.model->gpuFirstIndex,
                    int32_t(sample.model->gpuBaseVertex),
                    static_cast<uint32_t>(n));
                objectMotionStats_.rigidDraws++;
                objectMotionStats_.rigidInstances += static_cast<uint32_t>(n);
                first += n;
            }
        }
        // Alpha-mask replay must happen after rigid batches, before the
        // deformable pass, so cut-out fragments never mark empty pixels
        // with a bogus object velocity.
        if (objectMotionMaskedPipeline_.valid()) {
            struct MaskU {
                float current[16], previous[16], params[4];
                float uvTransform0[4], uvTransform1[4];
            };
            dev_->setPipeline(objectMotionMaskedPipeline_);
            for (const auto& it : modelItems_) {
                if (!eligible(it) || it.mat->alpha != AlphaMode::Mask)
                    continue;
                const Key key{it.motionPersistentId,it.motionNodeIndex};
                const auto previous = history.previous.find(key);
                if (previous == history.previous.end() ||
                    previous->second.sourceModel != it.model) continue;
                const mat4 currentMvp = viewProj_ * it.world;
                const mat4 previousMvp = previousCamera * previous->second.world;
                MaskU u{};
                std::memcpy(u.current,currentMvp.m,sizeof u.current);
                std::memcpy(u.previous,previousMvp.m,sizeof u.previous);
                u.params[0]=float(w); u.params[1]=float(h);
                u.uvTransform0[0]=it.mat->uvOffset.x;
                u.uvTransform0[1]=it.mat->uvOffset.y;
                u.uvTransform0[2]=it.mat->uvScale.x;
                u.uvTransform0[3]=it.mat->uvScale.y;
                u.uvTransform1[0]=it.mat->uvRotation;
                u.uvTransform1[1]=it.mat->alphaCutoff;
                u.uvTransform1[2]=it.mat->baseColor.w;
                u.uvTransform1[3]=it.mat->vertexColors ? 1.0f : 0.0f;
                dev_->setUniforms(0,&u,sizeof u);
                gfx::TextureHandle base=white_;
                if (it.mat->baseTex >= 0 &&
                    size_t(it.mat->baseTex) < it.model->images.size()) {
                    auto tex=it.model->images[size_t(it.mat->baseTex)].tex;
                    if (tex.valid()) base=tex;
                }
                dev_->setTexture(1,base);
                dev_->setVertexBuffer(it.model->vb);
                dev_->setIndexBuffer(it.model->ib);
                dev_->drawIndexed(it.indexCount,
                    it.firstIndex + it.model->gpuFirstIndex,
                    int32_t(it.model->gpuBaseVertex));
                objectMotionStats_.cutoutDraws++;
            }
        }
        // Re-evaluate vertex deformation for BOTH poses. Current and prior
        // palette buffers are independent U3/U4 blocks (each <= 4KB).
        // Morph deltas are read from the same immutable model texture, with
        // current and previous weights in U5. No CPU mesh readback.
        if (objectMotionDeformedPipeline_.valid()) {
            struct MorphU {
                float currentW[8] = {};
                float previousW[8] = {};
                float info[4] = {};
                float info2[4] = {};
            };
            static_assert(sizeof(MorphU) <= 4096);
            dev_->setPipeline(objectMotionDeformedPipeline_);
            for (const auto& it : modelItems_) {
                if (!deformEligible(it)) continue;
                const DeformKey key{it.motionPersistentId,it.motionNodeIndex,
                                    it.geometryPrimitive};
                const auto prior = history.previousDeformed.find(key);
                const auto now = currentDeformed.find(key);
                if (prior == history.previousDeformed.end() ||
                    now == currentDeformed.end() ||
                    prior->second.hasSkin != now->second.hasSkin ||
                    prior->second.baked != now->second.baked ||
                    prior->second.sourceModel != now->second.sourceModel ||
                    it.baked) { ++objectMotionStats_.rejected; continue; }
                const auto& cur = now->second;
                const auto& prev = prior->second;
                const mat4 currentMvp = viewProj_ * cur.world;
                const mat4 previousMvp = previousCamera * prev.world;
                MotionU u{};
                std::memcpy(u.current,currentMvp.m,sizeof u.current);
                std::memcpy(u.previous,previousMvp.m,sizeof u.previous);
                u.params[0] = float(w);
                u.params[1] = float(h);
                u.params[2] = cur.hasSkin ? 1.0f : 0.0f;
                const bool morph = it.prim->morphTargets > 0 &&
                                   it.model->morphTex.valid();
                u.params[3] = morph ? 1.0f : 0.0f;
                MorphU mo{};
                std::memcpy(mo.currentW,cur.weights.data(),sizeof mo.currentW);
                std::memcpy(mo.previousW,prev.weights.data(),sizeof mo.previousW);
                if (morph) {
                    mo.info[0] = float(it.prim->morphTexel);
                    mo.info[1] = float(it.prim->firstVertex);
                    mo.info[2] = float(it.prim->vertexCount);
                    mo.info[3] = float(it.prim->morphTargets);
                    mo.info2[0] = float(it.model->morphTexWidth);
                }
                dev_->setUniforms(0,&u,sizeof u);
                dev_->setUniforms(3,cur.joints.data(),
                                  cur.joints.size()*sizeof(float));
                dev_->setUniforms(4,prev.joints.data(),
                                  prev.joints.size()*sizeof(float));
                dev_->setUniforms(5,&mo,sizeof mo);
                if (morph) dev_->setTexture(8,it.model->morphTex);
                dev_->setVertexBuffer(it.model->vb);
                dev_->setIndexBuffer(it.model->ib);
                dev_->drawIndexed(it.indexCount,
                    it.firstIndex + it.model->gpuFirstIndex,
                    int32_t(it.model->gpuBaseVertex));
                objectMotionStats_.skinnedDraws++;
            }
        }
        // Texture-baked animation: rows A/B and blend are independently
        // sampled for current and previous pose. Keep this separate from
        // CPU-palette skinning; no animation state is written back to ECS.
        if (objectMotionBakedPipeline_.valid()) {
            struct BakedMotionU {
                float current[16], previous[16], params[4];
            };
            struct BakedAnimU {
                float current[4], previous[4];
            };
            struct BakedMorphU {
                float current[8] = {};
                float previous[8] = {};
                float info[4] = {};
                float info2[4] = {};
            };
            static_assert(sizeof(BakedMorphU) <= 4096);
            dev_->setPipeline(objectMotionBakedPipeline_);
            for (const auto& it : modelItems_) {
                if (!it.baked || !deformEligible(it)) continue;
                const DeformKey key{it.motionPersistentId,it.motionNodeIndex,
                                     it.geometryPrimitive};
                const auto before = history.previousDeformed.find(key);
                const auto now = currentDeformed.find(key);
                if (before == history.previousDeformed.end() ||
                    now == currentDeformed.end() ||
                    before->second.sourceModel != now->second.sourceModel ||
                    !before->second.baked) continue;
                const mat4 currentMvp = viewProj_ * now->second.world;
                const mat4 previousMvp = previousCamera * before->second.world;
                BakedMotionU u{};
                std::memcpy(u.current,currentMvp.m,sizeof u.current);
                std::memcpy(u.previous,previousMvp.m,sizeof u.previous);
                u.params[0] = float(w); u.params[1] = float(h);
                const bool hasMorph = it.prim->morphTargets > 0 &&
                                      it.model->morphTex.valid();
                u.params[2] = hasMorph ? 1.0f : 0.0f;
                float palette[80] = {};
                for (size_t i=0;i<it.prim->jointPalette.size() && i<80;++i)
                    palette[i] = float(it.prim->jointPalette[i]);
                BakedAnimU anim{};
                const auto& a = now->second.animation, b = before->second.animation;
                anim.current[0]=a.x; anim.current[1]=a.y;
                anim.current[2]=a.z; anim.current[3]=a.w;
                anim.previous[0]=b.x; anim.previous[1]=b.y;
                anim.previous[2]=b.z; anim.previous[3]=b.w;
                BakedMorphU morph{};
                if (hasMorph) {
                    std::memcpy(morph.current,now->second.weights.data(),
                                sizeof morph.current);
                    std::memcpy(morph.previous,before->second.weights.data(),
                                sizeof morph.previous);
                    morph.info[0] = float(it.prim->morphTexel);
                    morph.info[1] = float(it.prim->firstVertex);
                    morph.info[2] = float(it.prim->vertexCount);
                    morph.info[3] = float(it.prim->morphTargets);
                    morph.info2[0] = float(it.model->morphTexWidth);
                }
                dev_->setUniforms(0,&u,sizeof u);
                dev_->setUniforms(3,palette,sizeof palette);
                dev_->setUniforms(4,&anim,sizeof anim);
                dev_->setUniforms(5,&morph,sizeof morph);
                dev_->setTexture(7,it.model->baked.tex);
                dev_->setTexture(8,hasMorph ? it.model->morphTex : white_);
                dev_->setVertexBuffer(it.model->vb);
                dev_->setIndexBuffer(it.model->ib);
                dev_->drawIndexed(it.indexCount,
                    it.firstIndex + it.model->gpuFirstIndex,
                    int32_t(it.model->gpuBaseVertex));
                objectMotionStats_.bakedDraws++;
            }
        }
        dev_->endPass();
        objectMotionStats_.submitted = true;
        post.acceptObjectMotionVectors(history.target);
        } // optional object-velocity rendering

        // Conservative translucent coverage: surfaces whose true motion
        // cannot be reconstructed suppress blur locally via a reactive mask.
        // No previous-pose approximation, no readback, no ECS state changes.
        auto reactiveEligible = [](const ModelItem& it) {
            if (!it.inView || !it.model || !it.prim || !it.mat ||
                !it.model->vb.valid() || !it.model->ib.valid() ||
                it.indexCount == 0 || !it.rigid || it.joints >= 0 ||
                it.baked || it.morph >= 0 || it.model->geometry ||
                it.mat->shaderGraph || it.materialRole == 1 ||
                it.shadowRole == 2) return false;
            const bool translucent = it.tint.w < 0.99f ||
                it.mat->transparent || it.mat->alpha == AlphaMode::Blend ||
                (it.flags & kDrawTransparent) != 0;
            // Fade parts are skipped by the velocity overlay until their
            // coverage is reproduced. Mark only actual dithered fragments
            // reactive so these LOD boundaries cannot smear the background.
            const bool fade = (it.lodThreshold < 1.0f || it.lodInvert) &&
                it.mat->alpha == AlphaMode::Opaque;
            return translucent || fade;
        };
        auto reactiveAnimatedEligible = [](const ModelItem& it) {
            if (!it.inView || !it.model || !it.prim || !it.mat ||
                !it.model->vb.valid() || !it.model->ib.valid() ||
                !it.indexCount || it.model->geometry ||
                it.materialRole == 1 || it.shadowRole == 2 ||
                it.mat->shaderGraph) return false;
            const bool deform = it.joints >= 0 || it.baked ||
                (it.prim->morphTargets > 0 && it.model->morphTex.valid());
            const bool translucent = it.tint.w < 0.99f ||
                it.mat->transparent || it.mat->alpha == AlphaMode::Blend ||
                (it.flags & kDrawTransparent) != 0;
            return deform && translucent &&
                (!it.baked || it.model->baked.tex.valid());
        };
        // Water sheets and other immediate MeshVertex draws use DrawItem
        // rather than ModelItem. Their opacity is part of DrawItem::color;
        // the reactive pass reuses those same GPU buffers and transforms.
        const auto meshReactiveEligible = [](const DrawItem& item) {
            return item.mesh && item.mesh->vb.valid() &&
                item.mesh->ib.valid() && item.mesh->indexCount &&
                item.color.w > 0.001f && item.color.w < 0.99f;
        };
        bool meshReactiveNeeded = false;
        for (const auto& item : items_)
            meshReactiveNeeded |= meshReactiveEligible(item);
        bool rigidReactiveNeeded = false, animatedReactiveNeeded = false;
        for (const auto& it : modelItems_) {
            rigidReactiveNeeded |= reactiveEligible(it);
            animatedReactiveNeeded |= reactiveAnimatedEligible(it);
        }
        const bool reactiveNeeded = rigidReactiveNeeded ||
            animatedReactiveNeeded || meshReactiveNeeded ||
            !motionReactiveDraws_.empty();
        if (reactiveNeeded && !history.reactive.valid()) {
            gfx::TextureDesc target;
            target.width = w; target.height = h;
            target.renderTarget = true;
            target.format = gfx::PixelFormat::RGBA8;
            target.filter = gfx::Filter::Nearest;
            target.label = "prisma motion reactive mask";
            history.reactive = dev_->createTexture(target);
        }
        if ((rigidReactiveNeeded || meshReactiveNeeded) && !objectMotionReactiveShader_.valid()) {
            gfx::ShaderDesc shader;
            shader.glslVertex = reactive_shaders::kVS;
            shader.glslFragment = reactive_shaders::kFS;
            shader.msl = reactive_shaders::kMSL;
            shader.textureSlots = 1;
            shader.uniformSlots = 1;
            shader.label = "prisma motion reactive coverage";
            objectMotionReactiveShader_ = dev_->createShader(shader);
            if (objectMotionReactiveShader_.valid()) {
                gfx::PipelineDesc pipeline;
                pipeline.shader = objectMotionReactiveShader_;
                pipeline.attrs[0] = {0,gfx::VertexFormat::Float3,0};
                pipeline.attrCount = 1;
                pipeline.stride = sizeof(ModelVertex);
                pipeline.indexType = gfx::IndexType::UInt32;
                pipeline.cull = gfx::CullMode::Back;
                pipeline.depthTest = false;
                pipeline.depthWrite = false;
                pipeline.colorFormat = gfx::PixelFormat::RGBA8;
                pipeline.label = "prisma motion reactive coverage";
                objectMotionReactivePipeline_ = dev_->createPipeline(pipeline);
            }
        }
        if (meshReactiveNeeded && !objectMotionReactiveMeshPipeline_.valid() &&
            objectMotionReactiveShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionReactiveShader_;
            pipeline.attrs[0] = {0,gfx::VertexFormat::Float3,0};
            pipeline.attrCount = 1;
            pipeline.stride = sizeof(MeshVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::None; // water sheets are two-sided
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.blend = gfx::BlendMode::Screen;
            pipeline.colorFormat = gfx::PixelFormat::RGBA8;
            pipeline.label = "prisma mesh water reactive coverage";
            objectMotionReactiveMeshPipeline_ = dev_->createPipeline(pipeline);
        }
        // Deformable translucent models use the same GPU current-pose
        // vertex deformation as their motion shaders, but a coverage-only
        // fragment. The Metal fragment is swapped at shader creation time.
        auto prepareAnimatedReactive = [&](bool baked) {
            auto& shaderHandle = baked ? objectMotionReactiveBakedShader_
                                       : objectMotionReactiveSkinShader_;
            auto& pipelineHandle = baked ? objectMotionReactiveBakedPipeline_
                                         : objectMotionReactiveSkinPipeline_;
            if (shaderHandle.valid()) return;
            const char* vertex = baked ? motion_shaders::kBakedVS :
                                           motion_shaders::kDeformedVS;
            const char* vertexMsl = baked ? motion_shaders::kBakedMSL :
                                              motion_shaders::kDeformedMSL;
            const std::string msl = reactive_shaders::animatedMsl(vertexMsl);
            if (msl.empty()) return;
            gfx::ShaderDesc shader;
            shader.glslVertex = vertex;
            shader.glslFragment = reactive_shaders::kAnimatedFS;
            shader.msl = msl.c_str();
            shader.textureSlots = 9; // scene depth 0 + morph atlas 8 / baked 7
            shader.uniformSlots = 6;
            shader.label = baked ? "prisma animated baked reactive" :
                                   "prisma animated skin reactive";
            shaderHandle = dev_->createShader(shader);
            if (!shaderHandle.valid()) return;
            gfx::PipelineDesc pipeline;
            pipeline.shader = shaderHandle;
            pipeline.attrs[0] = {0,gfx::VertexFormat::Float3,0};
            pipeline.attrs[1] = {5,gfx::VertexFormat::UByte4,
                                 uint32_t(offsetof(ModelVertex,joints))};
            pipeline.attrs[2] = {6,gfx::VertexFormat::UByte4N,
                                 uint32_t(offsetof(ModelVertex,weights))};
            pipeline.attrCount = 3;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.blend = gfx::BlendMode::Screen;
            pipeline.colorFormat = gfx::PixelFormat::RGBA8;
            pipeline.label = shader.label;
            pipelineHandle = dev_->createPipeline(pipeline);
        };
        if (animatedReactiveNeeded) {
            bool needBaked = false, needSkin = false;
            for (const auto& item : modelItems_)
                if (reactiveAnimatedEligible(item)) {
                    needBaked |= item.baked;
                    needSkin |= !item.baked;
                }
            if (needBaked) prepareAnimatedReactive(true);
            if (needSkin) prepareAnimatedReactive(false);
        }
        if (reactiveNeeded && history.reactive.valid() &&
            (objectMotionReactivePipeline_.valid() ||
             objectMotionReactiveSkinPipeline_.valid() ||
             objectMotionReactiveBakedPipeline_.valid() ||
             objectMotionReactiveMeshPipeline_.valid() ||
             !motionReactiveDraws_.empty())) {
            struct ReactiveUniforms {
                float mvp[16], params[4], lod[4];
            };
            static_assert(sizeof(ReactiveUniforms) == 96);
            struct ReactiveMorphU {
                float currentW[8] = {}, previousW[8] = {};
                float info[4] = {}, info2[4] = {};
            };
            gfx::PassDesc maskPass;
            maskPass.colorTarget = history.reactive;
            maskPass.depthTarget = history.depth;
            maskPass.clearColor[0] = maskPass.clearColor[1] =
                maskPass.clearColor[2] = maskPass.clearColor[3] = 0.0f;
            maskPass.clearColorEnabled = true;
            maskPass.clearDepthEnabled = true;
            dev_->beginPass(maskPass);
            dev_->setViewport({0,0,w,h});
            dev_->setTexture(0,sceneDepth);
            for (const auto& it : modelItems_) {
                if (!objectMotionReactivePipeline_.valid() || !reactiveEligible(it))
                    continue;
                dev_->setPipeline(objectMotionReactivePipeline_);
                const mat4 mvp = viewProj_ * it.world;
                ReactiveUniforms u{};
                std::memcpy(u.mvp,mvp.m,sizeof u.mvp);
                u.params[0] = float(w); u.params[1] = float(h);
                u.params[2] = 1.0f; // conservative coverage, not alpha parity
                const bool fade = it.lodThreshold < 1.0f || it.lodInvert;
                u.lod[0] = it.lodThreshold;
                u.lod[1] = it.lodInvert ? 1.0f : 0.0f;
                u.lod[2] = fade ? 1.0f : 0.0f;
                if (fade) ++objectMotionStats_.lodReactiveDraws;
                dev_->setUniforms(0,&u,sizeof u);
                dev_->setVertexBuffer(it.model->vb);
                dev_->setIndexBuffer(it.model->ib);
                dev_->drawIndexed(it.indexCount,
                    it.firstIndex + it.model->gpuFirstIndex,
                    int32_t(it.model->gpuBaseVertex));
                ++objectMotionStats_.reactiveDraws;
            }

            // Actual water/transparent immediate mesh submissions. This
            // includes RTS TerrainRenderer water surfaces, which queue
            // Renderer3D::drawMesh(water_, ..., waterColor) before render.
            // We reuse exactly the submitted mesh GPU geometry and matrix.
            if (objectMotionReactiveMeshPipeline_.valid()) {
                dev_->setPipeline(objectMotionReactiveMeshPipeline_);
                dev_->setTexture(0,sceneDepth);
                for (const DrawItem& item : items_) {
                    if (!meshReactiveEligible(item)) continue;
                    ReactiveUniforms uniforms{};
                    const mat4 mvp = viewProj_ * item.model;
                    std::memcpy(uniforms.mvp,mvp.m,sizeof uniforms.mvp);
                    uniforms.params[0] = float(w);
                    uniforms.params[1] = float(h);
                    uniforms.params[2] = std::clamp(item.color.w,0.0f,1.0f);
                    dev_->setUniforms(0,&uniforms,sizeof uniforms);
                    dev_->setVertexBuffer(item.mesh->vb);
                    dev_->setIndexBuffer(item.mesh->ib);
                    dev_->drawIndexed(item.mesh->indexCount);
                    ++objectMotionStats_.reactiveMeshDraws;
                }
            }
            // Animated translucent current-pose silhouettes. We intentionally
            // use the current pose for both vertex parameters: the fragment
            // writes coverage, NOT velocity. Thus this works without an ID.
            for (const auto& it : modelItems_) {
                if (!reactiveAnimatedEligible(it)) continue;
                const bool baked = it.baked;
                const auto pipeline = baked ? objectMotionReactiveBakedPipeline_
                                             : objectMotionReactiveSkinPipeline_;
                if (!pipeline.valid()) continue;
                const bool morph = it.prim->morphTargets > 0 &&
                                   it.model->morphTex.valid();
                const bool skin = it.joints >= 0;
                if (skin &&
                    (it.joints < 0 ||
                     size_t(it.joints) + size_t(kMaxPaletteJoints) * 12 >
                         jointData_.size())) {
                    ++objectMotionStats_.rejected;
                    continue;
                }
                if (morph && it.morph >= 0 &&
                    size_t(it.morph) + size_t(kMaxMorphTargets) >
                        morphData_.size()) {
                    ++objectMotionStats_.rejected;
                    continue;
                }
                MotionU position{};
                const mat4 mvp = viewProj_ * it.world;
                std::memcpy(position.current,mvp.m,sizeof position.current);
                std::memcpy(position.previous,mvp.m,sizeof position.previous);
                position.params[0] = float(w);
                position.params[1] = float(h);
                position.params[2] = baked ? (morph ? 1.0f : 0.0f) :
                                             (skin ? 1.0f : 0.0f);
                position.params[3] = !baked && morph ? 1.0f : 0.0f;

                ReactiveMorphU mo{};
                if (morph) {
                    if (it.morph >= 0) {
                        std::memcpy(mo.currentW,morphData_.data()+it.morph,
                                    sizeof mo.currentW);
                        std::memcpy(mo.previousW,mo.currentW,sizeof mo.previousW);
                    }
                    mo.info[0] = float(it.prim->morphTexel);
                    mo.info[1] = float(it.prim->firstVertex);
                    mo.info[2] = float(it.prim->vertexCount);
                    mo.info[3] = float(it.prim->morphTargets);
                    mo.info2[0] = float(it.model->morphTexWidth);
                }

                dev_->setPipeline(pipeline);
                dev_->setUniforms(0,&position,sizeof position);
                dev_->setTexture(0,sceneDepth);
                if (baked) {
                    float palette[80] = {};
                    for (size_t j=0;j<it.prim->jointPalette.size() && j<80;++j)
                        palette[j] = float(it.prim->jointPalette[j]);
                    float animation[8] = {
                        it.anim.x,it.anim.y,it.anim.z,it.anim.w,
                        it.anim.x,it.anim.y,it.anim.z,it.anim.w
                    };
                    dev_->setUniforms(3,palette,sizeof palette);
                    dev_->setUniforms(4,animation,sizeof animation);
                    dev_->setUniforms(5,&mo,sizeof mo);
                    dev_->setTexture(7,it.model->baked.tex);
                } else {
                    float joints[size_t(kMaxPaletteJoints)*12] = {};
                    if (skin)
                        std::memcpy(joints,jointData_.data()+it.joints,
                                    sizeof joints);
                    dev_->setUniforms(3,joints,sizeof joints);
                    dev_->setUniforms(4,joints,sizeof joints);
                    dev_->setUniforms(5,&mo,sizeof mo);
                }
                dev_->setTexture(8,morph ? it.model->morphTex : white_);
                dev_->setVertexBuffer(it.model->vb);
                dev_->setIndexBuffer(it.model->ib);
                dev_->drawIndexed(it.indexCount,
                    it.firstIndex + it.model->gpuFirstIndex,
                    int32_t(it.model->gpuBaseVertex));
                ++objectMotionStats_.reactiveAnimatedDraws;
            }

            // Optional water/particles/custom extensions. The GPU pass and
            // viewport are already active. Clients must blend coverage over
            // previous layers (Screen union) and must not end the pass.
            for (auto& renderReactive : motionReactiveDraws_) {
                renderReactive(*dev_,sceneDepth,w,h);
                ++objectMotionStats_.reactiveCallbacks;
            }
            dev_->endPass();
            post.acceptMotionReactiveMask(history.reactive);
        }
    }
    history.previous = std::move(current);
    history.previousDeformed = std::move(currentDeformed);
}

void Renderer3D::resolvePrisma() {
    if (!post_) {
        motionReactiveDraws_.clear();
        return;
    }
    PostProcess* post = post_;
    post_ = nullptr;
    dev_->endPass();
    renderObjectMotionVectors(*post);
    PostFrame f{viewProj_, camPos_, lighting, sceneW_ > 0 ? sceneW_ : dev_->framebufferWidth(), sceneH_ > 0 ? sceneH_ : dev_->framebufferHeight(),
                quality_ != ShadowQuality::Off ? shadowMap_ : gfx::TextureHandle{}, lightViewProj_};
    post->resolve(*dev_, postOut_, f);
    motionReactiveDraws_.clear();
    hdrFrame_ = false;
}

// Distinct, stable high-contrast colour for each draw call. Instanced draws
// intentionally consume one ID, not one ID per instance.
float4 Renderer3D::drawCallDebugColor(uint32_t drawId) {
    const float h = std::fmod(float(drawId) * 0.61803398875f, 1.0f) * 6.0f;
    const float x = 0.76f * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if (h < 1) { r = .76f; g = x; }
    else if (h < 2) { r = x; g = .76f; }
    else if (h < 3) { g = .76f; b = x; }
    else if (h < 4) { g = x; b = .76f; }
    else if (h < 5) { r = x; b = .76f; }
    else { r = .76f; b = x; }
    return {r + .17f, g + .17f, b + .17f, 1.0f};
}

void Renderer3D::render(const gfx::PassDesc& screenPass, PostProcess* post) {
    drawCalls_ = 0;
    // Synchronize persistent scene staging before any rendering passes.
    // Model shader bindings and GPU culling remain on the legacy path.
    syncPersistentModels();
    // GPU command generation must complete encoding before render passes.
    // Metal guarantees command order between compute and render encoders.
    prepareGpuModelDraws();
    prepareOcclusion();
    // ---- shadow pass
    if (quality_ != ShadowQuality::Off && shadowMap_.valid()) {
        gfx::PassDesc sp;
        sp.depthTarget = shadowMap_;
        sp.clearColorEnabled = false;
        sp.clearDepthEnabled = true;
        dev_->beginPass(sp);
        dev_->setViewport({0, 0, shadowSize_, shadowSize_});
        dev_->setPipeline(shadowPipe_);
        uploadFrameUniforms(true);
        for (auto& it : items_) {
            if (it.flags & (kDrawNoCastShadow | kDrawUnlit) || it.color.w < 0.99f) continue;
            DrawUniforms u{};
            std::memcpy(u.model, it.model.m, sizeof u.model);
            dev_->setUniforms(0, &u, sizeof u);
            dev_->setVertexBuffer(it.mesh->vb);
            dev_->setIndexBuffer(it.mesh->ib);
            dev_->drawIndexed(it.mesh->indexCount);
            ++drawCalls_;
        }
        renderModels(0);
        for (auto& fn : customShadow_) fn(*dev_);
        dev_->endPass();
    }

    renderSpotShadow();   // High: the main spot light's shadow map (Renderer3DReflect.cpp)
    lights.renderMap();


    // ---- main pass (left open for the caller's UI; with a post hook: the hook's scene target)
    post_ = post && post->active() ? post : nullptr;
    postOut_ = screenPass;
    const int sw = sceneW_ > 0 ? sceneW_ : dev_->framebufferWidth(), sh = sceneH_ > 0 ? sceneH_ : dev_->framebufferHeight();
    const gfx::PassDesc mainPass = post_ ? post_->scenePass(screenPass, sw, sh) : screenPass;
    hdrFrame_ = post_ && post_->linearScene();
    lights.setLinearScene(hdrFrame_);
    renderReflection();
    // Exclude shadow, probe and reflection passes from the visible-colour ID range.
    debugMainDrawId_ = 0;
    // trace tier + water: the opaque scene goes into colour + depth textures first (SSR reads them),
    // then it is blitted (with depth) into the main pass and the rest is drawn on top
    bool capture = modelReflections_ && hdrFrame_ && traceActive();
    if (traceActive()) {
        for (const auto& it : items_)
            if (it.color.w < 0.99f && it.specular >= 0.85f && !(it.flags & kDrawUnlit)) { capture = true; break; }
        if (capture) capture = beginSceneCapture(sw, sh);
    }
    if (!capture) dev_->beginPass(mainPass);
    dev_->setViewport({0, 0, sw, sh});
    for (auto& fn : customOpaque_) fn(*dev_);
    dev_->setPipeline(litPipe_);
    uploadFrameUniforms(false);
    dev_->setTexture(0, shadowMap_);
    lights.bind(2, 1, 2);
    bindTraceTextures();
    auto drawItem = [&](const DrawItem& it) {
        DrawUniforms u{};
        std::memcpy(u.model, it.model.m, sizeof u.model);
        u.color[0] = it.color.x; u.color[1] = it.color.y; u.color[2] = it.color.z; u.color[3] = it.color.w;
        u.params[0] = (it.flags & kDrawUnlit) ? 1.f : 0.f;
        u.params[1] = it.specular;
        u.params[2] = (it.flags & kDrawNoReceiveShadow) ? 0.f : 1.f;
        u.params[3] = it.emissive;
        if (studioDebugMode_ == 2 || studioDebugMode_ == 4 || studioDebugMode_ == 9)
            u.params[0] = 10.0f + float(studioDebugMode_);
        if (drawCallDebug_) {
            const float4 c = drawCallDebugColor(++debugMainDrawId_);
            u.color[0] = c.x; u.color[1] = c.y; u.color[2] = c.z; u.color[3] = 1.0f;
            u.params[0] = 2.0f; // solid diagnostic output in GLSL and Metal
        }
        dev_->setUniforms(0, &u, sizeof u);
        dev_->setVertexBuffer(it.mesh->vb);
        dev_->setIndexBuffer(it.mesh->ib);
        dev_->drawIndexed(it.mesh->indexCount);
        ++drawCalls_;
        // drawItem is only used by the current camera's colour pass.
        profile_.cameraMeshTriangles += uint64_t(it.mesh->indexCount) / 3;
    };
    for (auto& it : items_) if (it.color.w >= 0.99f) drawItem(it);
    // The per-phase profile also includes reflection/SSR replay. Snapshot
    // only the *first* main-camera opaque pass so a triangle is not counted
    // twice when the same visible surface is drawn for another target.
    const uint32_t opaqueStart = profile_.triangles[1];
    renderModels(1);
    profile_.cameraModelTriangles += uint32_t(profile_.triangles[1] - opaqueStart);
    if (!sceneSelectionOutline_) renderModels(3); // legacy game outline; editor uses a depth-tested mask
    if (capture) {
        endSceneCapture(mainPass, sw, sh); dev_->setViewport({0, 0, sw, sh});
        // The blit binds scene color at slot 0; model shaders expect the sun depth map there.
        dev_->setTexture(0, shadowMap_);
        // Read the first opaque pass while shading into a different target (no feedback).
        if (modelReflections_ && hdrFrame_) { uploadFrameUniforms(false); renderModels(1); }
    }
    // translucent meshes (water, ghosts) before overlays so that overlays (fog of war, markers) stay on top
    bool any = false;
    for (auto& it : items_) {
        if (it.color.w >= 0.99f) continue;
        if (!any) { dev_->setPipeline(litBlendPipe_); uploadFrameUniforms(false); dev_->setTexture(0, shadowMap_); lights.bind(2, 1, 2); bindTraceTextures(); any = true; }
        drawItem(it);
    }
    const uint32_t blendedStart = profile_.triangles[2];
    renderModels(2);
    profile_.cameraModelTriangles += uint32_t(profile_.triangles[2] - blendedStart);
    renderModels(4);   // transparent objects (kDrawTransparent) write their depth now: later ground layers stay under them
    lights.drawHalos(viewProj_);   // additive glows, depth-tested against the scene
    // custom renderers (sprites) after translucent meshes: they don't write depth, so water drawn
    // later would tint them; overlays (fog of war, markers) still come on top
    for (auto& fn : custom_) fn(*dev_);
    if (!tris_.empty()) {
        dev_->updateBuffer(triVB_, tris_.data(), tris_.size() * sizeof(ColorVertex));
        dev_->setPipeline(triPipe_);
        uploadFrameUniforms(false);
        dev_->setVertexBuffer(triVB_);
        dev_->draw(uint32_t(tris_.size()));
        ++drawCalls_;
        profile_.cameraOverlayTriangles += uint64_t(tris_.size()) / 3;
    }
    if (!lines_.empty()) {
        dev_->updateBuffer(lineVB_, lines_.data(), lines_.size() * sizeof(ColorVertex));
        dev_->setPipeline(linePipe_);
        uploadFrameUniforms(false);
        dev_->setVertexBuffer(lineVB_);
        dev_->draw(uint32_t(lines_.size()));
        ++drawCalls_;
    }
    if (sceneSelectionOutline_) renderSelectionOutline(mainPass, sw, sh);
}


bool Renderer3D::initSelectionOutline() {
    if (!dev_) return false;
    gfx::ShaderDesc shader;
    shader.glslVertex = selection_outline::kVertex;
    shader.glslFragment = selection_outline::kFragment;
    shader.msl = selection_outline::kMetal;
    shader.textureSlots = 1;
    shader.uniformSlots = 1;
    shader.label = "scene selection edge";
    sceneSelectionEdgeShader_ = dev_->createShader(shader);
    if (!sceneSelectionEdgeShader_.valid()) return false;
    gfx::PipelineDesc pd;
    pd.shader = sceneSelectionEdgeShader_;
    pd.attrs[0] = {0, gfx::VertexFormat::Float2, 0};
    pd.attrCount = 1;
    pd.stride = 2 * sizeof(float);
    pd.indexType = gfx::IndexType::None;
    pd.cull = gfx::CullMode::None;
    pd.blend = gfx::BlendMode::Alpha;
    pd.depthTest = false;
    pd.depthWrite = false;
    pd.colorFormat = gfx::PixelFormat::RGBA8;
    pd.label = "scene selection edge RGBA8";
    sceneSelectionEdgePipe8_ = dev_->createPipeline(pd);
    pd.colorFormat = gfx::PixelFormat::RGBA16F;
    pd.label = "scene selection edge HDR";
    sceneSelectionEdgePipeHdr_ = dev_->createPipeline(pd);
    // Mask primitives with their REAL geometry, but never modify the scene
    // depth. The same depth buffer is reused to hide occluded silhouettes.
    gfx::PipelineDesc primitiveMask;
    primitiveMask.shader = litShader_;
    primitiveMask.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
    primitiveMask.attrs[1] = {1, gfx::VertexFormat::Float3, 12};
    primitiveMask.attrs[2] = {2, gfx::VertexFormat::UByte4N, 24};
    primitiveMask.attrCount = 3;
    primitiveMask.stride = sizeof(MeshVertex);
    primitiveMask.indexType = gfx::IndexType::UInt32;
    primitiveMask.depthTest = true;
    primitiveMask.depthWrite = false;
    primitiveMask.colorFormat = gfx::PixelFormat::RGBA8;
    primitiveMask.label = "selection primitive mask depth read-only";
    sceneSelectionPrimitiveMaskPipe_ = dev_->createPipeline(primitiveMask);
    const float triangle[6] = {-1, -1, 3, -1, -1, 3};
    gfx::BufferDesc bd;
    bd.data = triangle;
    bd.size = sizeof triangle;
    bd.label = "scene selection edge fullscreen triangle";
    sceneSelectionTriangle_ = dev_->createBuffer(bd);
    return sceneSelectionEdgePipe8_.valid() && sceneSelectionTriangle_.valid();
}

void Renderer3D::renderSelectionOutline(const gfx::PassDesc& mainPass,
                                        int width, int height) {
    if (!dev_ || !mainPass.colorTarget.valid() || !mainPass.depthTarget.valid() ||
        !sceneSelectionTriangle_.valid() || width <= 0 || height <= 0)
        return;
    const gfx::PipelineHandle edgePipeline = hdrFrame_
        ? sceneSelectionEdgePipeHdr_ : sceneSelectionEdgePipe8_;
    if (!edgePipeline.valid()) return;
    bool any = false;
    for (const auto& item : modelItems_) if (item.outline.w > 0 && item.inView) {
        any = true;
        break;
    }
    if (!any) for (const auto& item : items_) if (item.selection.w > 0) {
        any = true;
        break;
    }
    if (!any) return;

    if (!sceneSelectionMask_.valid() ||
        sceneSelectionWidth_ != width || sceneSelectionHeight_ != height) {
        if (sceneSelectionMask_.valid()) dev_->destroyTexture(sceneSelectionMask_);
        sceneSelectionMask_ = {};
        sceneSelectionWidth_ = width;
        sceneSelectionHeight_ = height;
        gfx::TextureDesc td;
        td.width = width; td.height = height;
        td.format = gfx::PixelFormat::RGBA8;
        td.renderTarget = true;
        td.filter = gfx::Filter::Nearest;
        td.label = "Scene selected-object depth-tested mask";
        sceneSelectionMask_ = dev_->createTexture(td);
    }
    if (!sceneSelectionMask_.valid()) return;

    // Renderer3D::render normally leaves the main pass open. Temporarily
    // suspend it, draw visible selected geometry using the same scene depth,
    // then resume the colour pass WITHOUT clearing its contents or depth.
    dev_->endPass();
    gfx::PassDesc mask;
    mask.colorTarget = sceneSelectionMask_;
    mask.depthTarget = mainPass.depthTarget;
    mask.clearColor[0] = mask.clearColor[1] = mask.clearColor[2] =
        mask.clearColor[3] = 0.0f;
    mask.clearDepthEnabled = false;
    dev_->beginPass(mask);
    dev_->setViewport({0, 0, width, height});

    // Existing model skin/morph/LOD pipelines draw a second pass with zero
    // normal displacement. Cull backfaces and do not write the scene depth.
    selectionMaskPass_ = true;
    uploadFrameUniforms(false);
    renderModels(3);
    selectionMaskPass_ = false;

    // Primitive previews use their original geometry, not bounding-box lines.
    bool bound = false;
    for (const auto& item : items_) {
        if (item.selection.w <= 0 || !item.mesh || !item.mesh->vb.valid())
            continue;
        if (!bound) {
            if (!sceneSelectionPrimitiveMaskPipe_.valid()) break;
            dev_->setPipeline(sceneSelectionPrimitiveMaskPipe_);
            uploadFrameUniforms(false);
            bound = true;
        }
        DrawUniforms u{};
        std::memcpy(u.model, item.model.m, sizeof u.model);
        u.color[0] = item.selection.x;
        u.color[1] = item.selection.y;
        u.color[2] = item.selection.z;
        u.color[3] = 1.0f;
        u.params[0] = 2.0f;  // existing solid unlit/diagnostic branch
        dev_->setUniforms(0, &u, sizeof u);
        dev_->setVertexBuffer(item.mesh->vb);
        dev_->setIndexBuffer(item.mesh->ib);
        dev_->drawIndexed(item.mesh->indexCount);
        ++drawCalls_;
    }
    dev_->endPass();

    gfx::PassDesc resume = mainPass;
    resume.clearColorEnabled = false;
    resume.clearDepthEnabled = false;
    dev_->beginPass(resume);
    dev_->setViewport({0, 0, width, height});

    dev_->setPipeline(edgePipeline);
    dev_->setVertexBuffer(sceneSelectionTriangle_);
    const float params[4] = {
        1.0f / float(width), 1.0f / float(height),
        dev_->info().renderTargetFlipY ? 1.0f : 0.0f, 0.0f
    };
    dev_->setUniforms(0, params, sizeof params);
    dev_->setTexture(0, sceneSelectionMask_);
    dev_->draw(3);
    ++drawCalls_;
}

} // namespace aiverse::render
