// LENS showcase host: a GeometryScene filmed by two ECS camera entities.
// Main camera: CameraLensComponent with driveExposure + driveDepthOfField, so the
// thin-lens focus interval and exposure reach PRISMA through CameraPostFxPool.
// Optional second camera renders into its own target; CameraCompositor puts it
// over the main frame as picture-in-picture. Capture with the platform hook:
//   AIVERSE_CONTENT=<dir> AIVERSE_SCREENSHOT=out.ppm AIVERSE_EXIT_AFTER_FRAMES=N
//   AIVERSE_PRISMA_DOF=0   disables the PRISMA defocus pass (on by default)
//   LENS_SCENE=scene.json  GeometryScene JSON relative to AIVERSE_CONTENT
//   LENS_POST=post.json    PRISMA block; needs depthOfField (+ autoExposure, hdr on)
//   LENS_EYE="x,y,z" LENS_AT="x,y,z"   main camera position and look-at/focus point
//   LENS_LENS="focalMm,fNumber,shutterSeconds,iso[,compensationEV,focusMetres]"
//              focus defaults to |AT-EYE|
//   LENS_PIP="eyeX,eyeY,eyeZ,atX,atY,atZ,fovDeg,x,y,w,h"  second camera; viewport in
//              0..1 of the frame (x,y from the top-left)
//   LENS_SUN, LENS_SUNDIR, LENS_SUNCOLOR, LENS_AMBIENT, LENS_SKY, LENS_LIGHTS as in the
//   SpectraFX host (FX_* there). LENS_W / LENS_H frame size.
// Layouts (one at a time; each uses only engine LENS APIs):
//   LENS_SPLIT="eye,at,fov"   second camera; split-screen left/right halves, each
//              camera with its own CameraPostFxPool entry. LENS_POST2 gives the second
//              camera its own PRISMA stack (setCameraSettings). LENS_MASK2 = its
//              CameraComponent::visibilityMask (instances carry "layers" in the scene).
//   LENS_DIRECTOR="eye,at,fov,switchFrame,blendSeconds"  CameraDirector: the second
//              camera gets the higher priority at switchFrame; frames step 1/30 s.
//   LENS_CUBE=faceSize        six cube faces from the main eye -> CameraPanorama
//              equirectangular frame.
//   LENS_MIRROR="nx,ny,nz,offset,x,y,w,h"  makeMirrorCamera view as an inset.
//   LENS_PORTAL="ex,ey,ez,yawDeg,xx,xy,xz,yawDeg,x,y,w,h"  makePortalCamera (entrance ->
//              exit) as an inset.
//   LENS_STEREO="ipd,convergence"  makeStereoCameraViews side by side;
//              LENS_XR=1 builds the eyes through makeXrCameraViews from a runtime-style
//              frame (pose + FOV tangents) instead.
// Website frames only: no engine behaviour changes.
#include "engine/platform/Platform.h"
#include "engine/render/GeometryScene.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/CameraPostFxPool.h"
#include "engine/render/CameraCompositor.h"
#include "engine/render/CameraRig.h"
#include "engine/render/CameraDirector.h"
#include "engine/render/CameraCaptureViews.h"
#include "engine/render/CameraXrViews.h"
#include "engine/render/CameraPanorama.h"
#include "engine/sim/SceneHierarchy.h"
#include "engine/data/Json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;

namespace {
const char* env(const char* k, const char* d) { const char* v = std::getenv(k); return v && *v ? v : d; }
std::vector<float> floats(const std::string& s) {
    std::vector<float> out; std::stringstream ss(s); std::string p;
    while (std::getline(ss, p, ',')) out.push_back(float(std::atof(p.c_str())));
    return out;
}
Fixed fx(double v) { return Fixed::fromRaw(int64_t(std::llround(v * 65536.0))); }
Vec3 vec(float3 v) { return {fx(v.x), fx(v.y), fx(v.z)}; }

struct Target {
    gfx::TextureHandle color, depth;
    int w = 0, h = 0;
    bool make(gfx::Device& d, int width, int height, const char* label) {
        gfx::TextureDesc c; c.width = width; c.height = height; c.renderTarget = true; c.label = label;
        gfx::TextureDesc z = c; z.format = gfx::PixelFormat::Depth;
        color = d.createTexture(c); depth = d.createTexture(z); w = width; h = height;
        return color.valid() && depth.valid();
    }
};

struct Host {
    Renderer3D r3d;
    GeometryScene scene;
    CameraPostFxPool pool;
    CameraCompositor compositor;
    CameraRigEvaluator rigs;
    World world;
    SceneHierarchy tree;
    Entity mainCam{}, pipCam{}, altCam{};
    Target mainTarget, pipTarget, altTarget, faces[6], panoTarget, eyes[2];
    CameraDirector director;
    CameraPanorama panorama;
    std::string layout;          // split, director, cube, mirror, portal, stereo
    std::vector<float> layoutArgs;
    int switchFrame = 0;
    Camera insetCamera{};
    gfx::TextureHandle frameTex;   // 1x1 white: PiP border
    gfx::Rect pipRect{}, pipBorder{};
    std::vector<float> extraLights;
    float3 clear{};
    int frame = 0;

    Entity makeCamera(float3 eye, float3 at, float fovDeg) {
        const Entity target = world.create();
        LocalTransform tl; tl.position = vec(at);
        tree.attach(world, target, tl);
        const Entity cam = world.create();
        LocalTransform cl; cl.position = vec(eye);
        tree.attach(world, cam, cl);
        CameraComponent c; c.verticalFov = fx(fovDeg * 0.0174532925); c.nearPlane = fx(0.05); c.farPlane = fx(400);
        world.add<CameraComponent>(cam, c);
        CameraRigComponent rig; rig.mode = CameraRigMode::LookAt; rig.target = target; rig.smoothing = 0_fx;
        world.add<CameraRigComponent>(cam, rig);
        return cam;
    }

    void init(gfx::Device& d) {
        r3d.init(d);
        r3d.setShadowQuality(ShadowQuality::High);
        r3d.setMaterialQuality(MaterialQuality::High);
        // The Mac window may not report its drawable size during init: size the
        // camera targets from the requested frame instead.
        int w = d.framebufferWidth(), h = d.framebufferHeight();
        if (w <= 0 || h <= 0) { w = std::atoi(env("LENS_W", "1920")); h = std::atoi(env("LENS_H", "1080")); }
        pool.init(d);
        if (const char* file = std::getenv("LENS_POST")) {
            std::string text;
            if (platform::readContentFile(file, text))
                pool.configure(parsePrisma(Json::parse(text), Json()), PrismaQuality::High, false);
            else std::fprintf(stderr, "[lens-showcase] post not found: %s\n", file);
        }
        if (const char* file = std::getenv("LENS_SCENE")) {
            std::string text;
            if (platform::readContentFile(file, text))
                scene.load(d, Json::parse(text), [](const std::string& p, std::string& out) { return platform::readContentFile(p, out); });
            else std::fprintf(stderr, "[lens-showcase] scene not found: %s\n", file);
        }
        auto eye = floats(env("LENS_EYE", "0,1.6,6")); eye.resize(3, 0.f);
        auto at = floats(env("LENS_AT", "0,1.2,0")); at.resize(3, 0.f);
        mainCam = makeCamera({eye[0], eye[1], eye[2]}, {at[0], at[1], at[2]}, 40);
        addLens(mainCam, env("LENS_LENS", "50,1.8,0.0166667,100"), eye, at);
        initLayout(d, w, h, eye);
        if (!mainTarget.color.valid()) mainTarget.make(d, w, h, "lens main camera");
        if (const char* p = std::getenv("LENS_PIP"); p && *p && layout.empty()) {
            auto v = floats(p); v.resize(11, 0.f);
            pipCam = makeCamera({v[0], v[1], v[2]}, {v[3], v[4], v[5]}, v[6] > 0 ? v[6] : 50);
            pipRect = {int(v[7] * w), int(v[8] * h), int(v[9] * w), int(v[10] * h)};
            pipTarget.make(d, pipRect.w, pipRect.h, "lens pip camera");
            makeBorder(d, h);
        }
        finishInit(d);
    }

    void makeBorder(gfx::Device& d, int h) {
        const int b = std::max(2, h / 270);
        pipBorder = {pipRect.x - b, pipRect.y - b, pipRect.w + 2 * b, pipRect.h + 2 * b};
        gfx::TextureDesc t; t.width = t.height = 1; const uint8_t white[4] = {236, 230, 220, 255}; t.data = white;
        frameTex = d.createTexture(t);
    }

    void addLens(Entity cam, const std::string& spec, const std::vector<float>& eye, const std::vector<float>& at) {
        auto l = floats(spec);
        l.resize(6, 0.f);
        CameraLensComponent lens;
        lens.physicalFov = true;
        lens.focalLengthMm = fx(l[0]); lens.fNumber = fx(l[1]);
        lens.shutterSeconds = fx(l[2]); lens.iso = fx(l[3]);
        lens.exposureCompensation = fx(l[4]);
        const float dx = at[0] - eye[0], dy = at[1] - eye[1], dz = at[2] - eye[2];
        lens.focusDistance = fx(l[5] > 0 ? l[5] : std::sqrt(dx * dx + dy * dy + dz * dz));
        lens.driveExposure = true; lens.driveDepthOfField = true;
        world.add<CameraLensComponent>(cam, lens);
    }

    std::string readContent(const char* file) {
        std::string text;
        if (!platform::readContentFile(file, text)) std::fprintf(stderr, "[lens-showcase] not found: %s\n", file);
        return text;
    }

    void initLayout(gfx::Device& d, int w, int h, const std::vector<float>& eye) {
        static const char* kinds[] = {"SPLIT", "DIRECTOR", "CUBE", "MIRROR", "PORTAL", "STEREO"};
        for (const char* k : kinds) {
            const std::string var = std::string("LENS_") + k;
            if (const char* v = std::getenv(var.c_str())) {
                layout = k; layoutArgs = floats(v);
                for (auto& c : layout) c = char(std::tolower(c));
                break;
            }
        }
        auto& a = layoutArgs;
        if (layout == "split" || layout == "director") {
            a.resize(layout == "split" ? 7 : 9, 0.f);
            altCam = makeCamera({a[0], a[1], a[2]}, {a[3], a[4], a[5]}, a[6] > 0 ? a[6] : 40);
            if (const char* l2 = std::getenv("LENS_LENS2"))
                addLens(altCam, l2, {a[0], a[1], a[2]}, {a[3], a[4], a[5]});
            if (const char* m = std::getenv("LENS_MASK2"))
                world.get<CameraComponent>(altCam).visibilityMask = uint32_t(std::strtoul(m, nullptr, 0));
            if (const char* file = std::getenv("LENS_POST2")) {
                const std::string text = readContent(file);
                if (!text.empty()) pool.setCameraSettings(altCam, parsePrisma(Json::parse(text), Json()));
            }
            if (layout == "split") {
                mainTarget.make(d, w / 2, h, "lens split left");
                altTarget.make(d, w - w / 2, h, "lens split right");
            } else {
                switchFrame = int(a[7]);
                director.setBlendDuration(a[8]);
                world.get<CameraComponent>(mainCam).priority = 2;
                world.get<CameraComponent>(altCam).priority = 1;
            }
        } else if (layout == "cube") {
            const int face = a.empty() || a[0] < 16 ? 512 : int(a[0]);
            for (int i = 0; i < 6; ++i) faces[i].make(d, face, face, "lens cube face");
            panoTarget.make(d, std::min(w, 2 * h), std::min(w, 2 * h) / 2, "lens panorama");
            panorama.init(d);
        } else if (layout == "mirror" || layout == "portal") {
            a.resize(layout == "mirror" ? 8 : 12, 0.f);
            const float* r = &a[a.size() - 4];
            pipRect = {int(r[0] * w), int(r[1] * h), int(r[2] * w), int(r[3] * h)};
            pipTarget.make(d, pipRect.w, pipRect.h, "lens inset camera");
            makeBorder(d, h);
        } else if (layout == "stereo") {
            a.resize(2, 0.f);
            eyes[0].make(d, w / 2, h, "lens left eye");
            eyes[1].make(d, w - w / 2, h, "lens right eye");
        }
        (void)eye;
    }

    void finishInit(gfx::Device& d) {
        tree.update(world);
        compositor.init(d);
        extraLights = floats(env("LENS_LIGHTS", ""));
        auto sd = floats(env("LENS_SUNDIR", "0.6,0.35,-0.5")); sd.resize(3, 0.5f);
        r3d.lighting.sunDir = {sd[0], sd[1], sd[2]};
        if (const char* c = std::getenv("LENS_SUNCOLOR")) { auto sc = floats(c); sc.resize(3, 1.f); r3d.lighting.sunColor = {sc[0], sc[1], sc[2]}; }
        if (const char* a = std::getenv("LENS_AMBIENT")) {
            auto am = floats(a); am.resize(6, 0.1f);
            r3d.lighting.skyColor = {am[0], am[1], am[2]}; r3d.lighting.groundColor = {am[3], am[4], am[5]};
        }
        r3d.lighting.sunIntensity = float(std::atof(env("LENS_SUN", "1.0")));
        r3d.lighting.fogDensity = 0.f;
        auto sky = floats(env("LENS_SKY", "0.5,0.6,0.75")); sky.resize(3, 0.5f);
        clear = {sky[0], sky[1], sky[2]};
    }

    // One camera pass. `post`: the camera's own CameraPostFxPool entry;
    // `override` replaces the resolved pose (director, cube faces, mirror...).
    void drawView(gfx::Device& d, Entity cam, const Target& t, bool post,
                  const Camera* override = nullptr) {
        EcsCameraView view;
        if (!resolveEcsCamera(world, cam, view)) return;
        rigs.apply(world, view, 0.0, 0.f);
        if (override) view.camera = *override;
        Camera camera = post ? pool.prepare(view, t.w, t.h) : view.camera;
        r3d.beginScene(camera, t.w, t.h);
        r3d.setVisibilityMask(view.visibilityMask);
        for (size_t i = 0; i + 8 <= extraLights.size(); i += 8) {
            const float* e = &extraLights[i];
            r3d.lights.add({e[0], e[1], e[2]}, e[3], {e[4], e[5], e[6]}, e[7]);
        }
        scene.draw(r3d);
        gfx::PassDesc pass;
        pass.colorTarget = t.color; pass.depthTarget = t.depth;
        pass.clearColor[0] = clear.x; pass.clearColor[1] = clear.y; pass.clearColor[2] = clear.z;
        PostProcess* pp = post ? pool.get(cam) : nullptr;
        r3d.render(pass, pp);
        if (pp) r3d.resolvePrisma();
        d.endPass();
    }

    Camera resolved(Entity cam) {
        EcsCameraView view;
        if (!resolveEcsCamera(world, cam, view)) return {};
        rigs.apply(world, view, 0.0, 0.f);
        return view.camera;
    }

    void tick(gfx::Device& d) {
        d.beginFrame(platform::framebufferWidth(), platform::framebufferHeight());
        pool.bindWorld(world);
        const int W = mainTarget.w, H = mainTarget.h;
        std::vector<CameraCompositeLayer> layers;
        int screenW = W, screenH = H;
        if (layout == "split") {
            drawView(d, mainCam, mainTarget, true);
            drawView(d, altCam, altTarget, true);
            screenW = W + altTarget.w;
            layers.push_back({mainTarget.color, {0, 0, W, H}, 0, 1.f, 1});
            layers.push_back({altTarget.color, {W, 0, altTarget.w, H}, 1, 1.f, 2});
        } else if (layout == "director") {
            if (frame == switchFrame) {
                world.get<CameraComponent>(mainCam).priority = 1;
                world.get<CameraComponent>(altCam).priority = 2;
            }
            std::vector<EcsCameraView> views;
            for (Entity e : {mainCam, altCam}) {
                EcsCameraView v;
                if (resolveEcsCamera(world, e, v)) { rigs.apply(world, v, 0.0, 0.f); views.push_back(v); }
            }
            const auto shot = director.evaluate(views, 1.f / 30.f);
            if (shot.valid) drawView(d, mainCam, mainTarget, true, &shot.camera);
            if (frame == switchFrame + 1 || frame % 10 == 0)
                std::fprintf(stderr, "[lens-showcase] frame %d director source=%u blend=%.2f cut=%d\n", frame,
                             shot.source.index, shot.blendWeight, shot.cut ? 1 : 0);
            layers.push_back({mainTarget.color, {0, 0, W, H}, 0, 1.f, 1});
        } else if (layout == "cube") {
            std::array<uint64_t, 6> keys{1, 2, 3, 4, 5, 6};
            const auto views = makeCubemapCameraViews(resolved(mainCam), keys, faces[0].w);
            for (int i = 0; i < 6; ++i) drawView(d, mainCam, faces[i], false, &views[size_t(i)].camera);
            std::array<gfx::TextureHandle, 6> tex{};
            for (int i = 0; i < 6; ++i) tex[size_t(i)] = faces[i].color;
            if (!panorama.render(tex, panoTarget.color, panoTarget.w, panoTarget.h) && frame == 2)
                std::fprintf(stderr, "[lens-showcase] panorama render failed\n");
            layers.push_back({panoTarget.color, {(W - panoTarget.w) / 2, (H - panoTarget.h) / 2, panoTarget.w, panoTarget.h}, 0, 1.f, 1});
        } else if (layout == "mirror" || layout == "portal") {
            drawView(d, mainCam, mainTarget, true);
            const Camera src = resolved(mainCam);
            const auto& a = layoutArgs;
            if (layout == "mirror") {
                const float3 n = normalize(float3{a[0], a[1], a[2]});
                insetCamera = makeMirrorCamera(src, n, a[3]);
                // Clip what lies behind the mirror plane (the wall/floor itself).
                insetCamera.obliqueClip = true;
                insetCamera.clipWorldPlane = {n.x, n.y, n.z, a[3]};
            } else {
                const float deg = 0.0174532925f;
                const mat4 entrance = mat4::translate({a[0], a[1], a[2]}) * mat4::rotateY(a[3] * deg);
                const mat4 exit = mat4::translate({a[4], a[5], a[6]}) * mat4::rotateY(a[7] * deg);
                insetCamera = makePortalCamera(src, entrance, exit);
                // Only what is in front of the exit is visible through the portal.
                float3 f = normalize(exit.transformDir({0, 0, -1}));
                if (dot(f, cameraPoseRotation(insetCamera).rotate({0, 0, -1})) < 0) f = f * -1.f;
                const float3 o{a[4], a[5], a[6]};
                insetCamera.obliqueClip = true;
                insetCamera.clipWorldPlane = {f.x, f.y, f.z, -dot(f, o)};
            }
            drawView(d, mainCam, pipTarget, false, &insetCamera);
            layers.push_back({mainTarget.color, {0, 0, W, H}, 0, 1.f, 1});
            layers.push_back({frameTex, pipBorder, 1, 1.f, 2});
            layers.push_back({pipTarget.color, pipRect, 2, 1.f, 3});
        } else if (layout == "stereo") {
            const Camera src = resolved(mainCam);
            std::array<CameraCaptureView, 2> v{};
            bool ok = true;
            if (std::getenv("LENS_XR")) {
                // Runtime-style frame: predicted eye poses and FOV tangents.
                CameraXrFrame xr; xr.predictedDisplayTime = 1.0;
                const quat rot = cameraPoseRotation(src);
                const float3 right = rot.rotate({1, 0, 0});
                const float half = std::clamp(layoutArgs[0], 0.f, 0.2f) * 0.5f;
                const float vt = std::tan(src.fovY * 0.5f), ht = vt * float(eyes[0].w) / float(eyes[0].h);
                for (int i = 0; i < 2; ++i) {
                    auto& e = xr.eyes[size_t(i)];
                    e.position = src.eye() + right * (i == 0 ? -half : half);
                    e.orientation = rot;
                    e.leftTan = -ht * (i == 0 ? 1.1f : 0.9f); e.rightTan = ht * (i == 0 ? 0.9f : 1.1f);
                    e.downTan = -vt; e.upTan = vt;
                    e.targetAssetKey = uint64_t(i + 1); e.width = eyes[i].w; e.height = eyes[i].h;
                }
                ok = makeXrCameraViews(xr, src, v);
            } else {
                v = makeStereoCameraViews(src, layoutArgs[0], layoutArgs[1], {1, 2}, eyes[0].w, eyes[0].h);
            }
            if (ok) for (int i = 0; i < 2; ++i) drawView(d, mainCam, eyes[i], true, &v[size_t(i)].camera);
            else if (frame == 2) std::fprintf(stderr, "[lens-showcase] XR eye views rejected\n");
            screenW = eyes[0].w + eyes[1].w;
            layers.push_back({eyes[0].color, {0, 0, eyes[0].w, H}, 0, 1.f, 1});
            layers.push_back({eyes[1].color, {eyes[0].w, 0, eyes[1].w, H}, 1, 1.f, 2});
        } else {
            drawView(d, mainCam, mainTarget, true);
            if (pipTarget.color.valid()) drawView(d, pipCam, pipTarget, false);
            layers.push_back({mainTarget.color, {0, 0, W, H}, 0, 1.f, 1});
            if (pipTarget.color.valid()) {
                layers.push_back({frameTex, pipBorder, 1, 1.f, 2});
                layers.push_back({pipTarget.color, pipRect, 2, 1.f, 3});
            }
        }
        compositor.composite(layers, screenW, screenH);
        if (++frame == 3) {
            const auto s = pool.lensState(mainCam);
            std::fprintf(stderr, "[lens-showcase] layout=%s post=%d pip=%d cameras=%zu\n", layout.empty() ? "main" : layout.c_str(),
                         pool.get(mainCam) ? 1 : 0, pipTarget.color.valid() ? 1 : 0, pool.cameraCount());
            std::fprintf(stderr, "[lens-showcase] scene instances=%u submitted=%u\n", scene.stats().instances, scene.stats().submitted);
            std::fprintf(stderr, "[lens-showcase] lens EV %+.2f, dof %d focus %.2f m range %.2f m blur %.1f px\n",
                         s.ev, s.depthOfField ? 1 : 0, s.dof.focusDistance, s.dof.focusRangeMeters, s.dof.maxBlur);
        }
    }
};
std::unique_ptr<Host> g;
} // namespace

int aiverseMain(int, char**) {
    platform::AppConfig cfg;
    cfg.title = "AIVERSE LENS showcase";
    cfg.width = std::atoi(env("LENS_W", "1920"));
    cfg.height = std::atoi(env("LENS_H", "1080"));
    cfg.highDpi = false;
    platform::AppCallbacks cb;
    cb.init = [](gfx::Device& d) { g = std::make_unique<Host>(); g->init(d); };
    cb.frame = [](gfx::Device& d, double) { g->tick(d); };
    cb.event = [](const platform::Event&) {};
    cb.shutdown = [] { g.reset(); };
    return platform::run(cfg, cb);
}
