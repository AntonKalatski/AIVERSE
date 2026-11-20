// Native macOS VIVID visual acceptance preview (interactive graphics host).
// Three image captures from the REAL Renderer3D colour+shadow passes:
// baseline -> VIVID GPU morph -> same morph after MotionWeave body animation.
// Requires a GUI Metal graphics session. This is intentionally not a CTest;
// capture/draw verification must not skip when run on a headless CI agent.
#include "engine/platform/Platform.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/GlobalGeometryPool.h"
#include "engine/render/ModelVertexStreamOverride.h"
#include "engine/character/vivid/GpuActorStream.h"
#include "engine/character/vivid/GpuActorRetirement.h"
#include "engine/character/vivid/GpuMorphAssetCache.h"
#include "engine/character/vivid/FaceShapeBinding.h"
#include <aiverselib/motionweave/MotionWeave.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;
namespace {
constexpr int kWidth = 960, kHeight = 640;

void require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}
std::string capturePath(const char* suffix) {
    const char* dir = std::getenv("AIVERSE_VIVID_PREVIEW_PREFIX");
    return std::string(dir && *dir ? dir : "/tmp/aiverse-vivid") + suffix + ".ppm";
}
void savePpm(const std::string& path, int width, int height,
             const std::vector<uint8_t>& pixels) {
    std::ofstream file(path, std::ios::binary);
    require(bool(file), "cannot open screenshot path");
    file << "P6\n" << width << " " << height << "\n255\n";
    for (size_t i = 0; i < pixels.size(); i += 4)
        file.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
    require(bool(file), "cannot write screenshot");
}
struct Capture {
    int index = 0;
    std::vector<uint8_t> data;
};
vivid::SparseMorphLibrary morphsFromModel(const render::Model& model) {
    std::vector<vivid::MorphTargetInput> targets;
    vivid::MorphTargetInput jaw;
    jaw.name = "jawOpen";
    vivid::MorphTargetInput blink;
    blink.name = "blinkRight";
    require(model.vertices.size() <= std::numeric_limits<std::uint32_t>::max(),
            "preview model vertex count exceeds VIVID index ABI");
    for (std::size_t i = 0; i < model.vertices.size(); ++i) {
        const auto& vertex = model.vertices[i];
        if (vertex.pos.y < -0.12f) {
            vivid::MorphDelta d;
            d.vertex = static_cast<std::uint32_t>(i);
            d.position = {0.0f,-0.7f,0.12f};
            jaw.deltas.push_back(d);
        }
        if (vertex.pos.x > 0.18f) {
            vivid::MorphDelta d;
            d.vertex = static_cast<std::uint32_t>(i);
            d.position = {0.65f,0.0f,0.08f};
            blink.deltas.push_back(d);
        }
    }
    require(jaw.deltas.size() > 10 && blink.deltas.size() > 10,
            "procedural face lacks visible morph vertices");
    targets.push_back(std::move(jaw));
    targets.push_back(std::move(blink));
    return vivid::SparseMorphLibrary::compile(model.vertices.size(),std::move(targets));
}
struct Preview {
    render::Renderer3D renderer;
    render::Mesh floor;
    std::unique_ptr<render::Model> model;
    std::unique_ptr<vivid::SparseMorphLibrary> shapes;
    std::unique_ptr<vivid::FaceShapeBinding> shapeBinding;
    vivid::FaceController face{0.0f};
    std::unique_ptr<vivid::GpuMorphAssetCache> sharedCache;
    std::shared_ptr<vivid::GpuMorphSharedAsset> sharedAsset;
    std::unique_ptr<vivid::GpuActorRetirementQueue> actorRetirement;
    std::vector<std::unique_ptr<vivid::GpuActorStream>> actors;
    int actorCount = 1;
    World presentation;
    Entity actor{};
    std::shared_ptr<const render::AnimationGraph> graph;
    gfx::Device* device = nullptr;
    std::vector<Capture> captures;
    bool failed = false, initialized = false, complete = false, animated = false;
    int frame = 0;
    // Capture stages advance only after the callback confirms a rendered
    // image. Pipeline compilation may take many frames on first launch.
    int stage = 0; // baseline, morph, MotionWeave
    int readyFrames = 0;
    int motionFrames = 0;
    bool capturePending = false;

    void initialize(gfx::Device& dev) {
        device = &dev;
        require(dev.info().backend == gfx::Backend::Metal,
                "VIVID preview requires Metal gfx backend");
        // Keep the fixture dedicated and avoid global geometry-pool offsets.
        render::setGlobalGeometryPoolingEnabled(false);
        require(renderer.init(dev), "Renderer3D initialization failed");
        initialized = true;
        renderer.setModelMultiDrawEnabled(false);
        renderer.setModelInstancingEnabled(false);
        renderer.setModelLodEnabled(false);
        renderer.setShadowQuality(render::ShadowQuality::Medium);
        renderer.setShadowBounds({-5.0f,-2.0f,-5.0f},{5.0f,5.0f,5.0f});
        renderer.setTraceEnabled(false);
        renderer.lighting.fogDensity = 0;
        renderer.lighting.sunIntensity = 1.4f;

        render::MeshBuilder head;
        head.sphere({0,0,0},0.95f,24,36);
        render::Material material;
        material.baseColor = {0.86f,0.48f,0.32f,1};
        material.metallic = 0.0f;
        material.roughness = 0.75f;
        material.doubleSided = true;
        model = render::modelFromParts("VIVID preview head",
            {{"head",&head,material,{}}});
        require(model && !model->vertices.empty(), "procedural model missing");
        // MotionWeave owns the clip/graph and final skeletal (here node) pose.
        render::AnimClip clip;
        clip.name = "Nod";
        clip.duration = 1.0f;
        render::AnimChannel channel;
        channel.node = 0;
        channel.times = {0.0f,1.0f};
        channel.values = {0,0,0, 0,0.5f,0};
        clip.channels.push_back(channel);
        model->clips.push_back(std::move(clip));
        render::prepareModelUpload(*model);
        shapes = std::make_unique<vivid::SparseMorphLibrary>(morphsFromModel(*model));
        const std::vector<vivid::ShapeRoute> routes{
            {vivid::Channel::JawOpen,"jawOpen",1.0f},
            {vivid::Channel::EyeBlinkRight,"blinkRight",1.0f}};
        shapeBinding = std::make_unique<vivid::FaceShapeBinding>(
            vivid::FaceShapeBinding::compile(*shapes,routes));
        if(const char* value=std::getenv("AIVERSE_VIVID_PREVIEW_ACTORS")) {
            const int requested=std::atoi(value);
            actorCount=std::clamp(requested,1,32);
        }
        require(render::uploadModel(dev,*model,true), "model GPU upload failed");
        require(!model->geometryPoolPage && !model->gpuBaseVertex &&
                !model->gpuFirstIndex, "VIVID needs dedicated ModelVertex storage");
        const auto preflight = render::modelVertexOverrideStatus(
            *model, model->vb, static_cast<std::uint32_t>(model->vertices.size()));
        if(preflight != render::ModelVertexOverrideStatus::Ready)
            throw std::runtime_error("VIVID preview model vertex metadata invalid: status=" +
                                     std::to_string(int(preflight)));
        std::printf("VIVID preview primitive range: first=%u count=%u total=%zu\n",
                    model->meshes[0].primitives[0].firstVertex,
                    model->meshes[0].primitives[0].vertexCount,
                    model->vertices.size());
        sharedCache=std::make_unique<vivid::GpuMorphAssetCache>(dev);
        sharedAsset=sharedCache->acquire(*shapes,model->vertices);
        require(sharedAsset && sharedAsset->ready(),
                "VIVID shared immutable model CSR GPU asset init failed");
        actorRetirement=std::make_unique<vivid::GpuActorRetirementQueue>(dev);
        actors.reserve(static_cast<std::size_t>(actorCount));
        for(int i=0;i<actorCount;++i) {
            auto actor=std::make_unique<vivid::GpuActorStream>();
            require(actor->initializeShared(dev,*model,sharedAsset),
                    "VIVID per-character GPU morph stream init failed");
            actors.push_back(std::move(actor));
        }
        for(std::size_t i=0;i<actors.size();++i)
            for(std::size_t j=i+1;j<actors.size();++j)
                require(actors[i]->vertexBuffer()!=actors[j]->vertexBuffer(),
                        "VIVID actors accidentally share writable GPU output");
        render::MeshBuilder ground;
        ground.box({0,-0.12f,0},{10,0.2f,9});
        floor = ground.upload(dev);
        require(floor.vb.valid() && floor.ib.valid(), "ground upload failed");

        graph = render::AnimationGraph::compile(*model,Json::parse(
            R"({"nodes":[{"id":"nod","kind":"clip","clip":"Nod"}],"output":"nod"})"));
        actor = presentation.create();
        auto& asset=presentation.add<motionweave::AnimationAssetComponent>(actor);
        asset.model=model.get();
        asset.graph=graph;
        presentation.add<motionweave::AnimationPlayerComponent>(actor);
        presentation.add<motionweave::AnimationPoseComponent>(actor);
        std::printf("VIVID render preview ready: %zu vertices, %zu GPU shapes, %d actors, %zu immutable GPU bytes\n",
                    model->vertices.size(),shapes->targets(),actorCount,
                    sharedAsset->gpuBytes());
    }

    void capture(gfx::Device& dev, const char* suffix, int number) {
        dev.captureNextFrame([this,suffix=std::string(suffix),number](
                             int w,int h,std::vector<uint8_t> pixels) {
            if (w!=kWidth || h!=kHeight || pixels.size()!=size_t(w*h*4)) {
                failed = true;
                std::fprintf(stderr,"VIVID preview: invalid frame readback\n");
                return;
            }
            try {
                savePpm(capturePath(suffix.c_str()),w,h,pixels);
            } catch(const std::exception& e) {
                failed = true;
                std::fprintf(stderr,"VIVID preview capture: %s\n",e.what());
                return;
            }
            captures.push_back({number,std::move(pixels)});
            std::printf("VIVID preview captured %s\n",capturePath(suffix.c_str()).c_str());
            // Keep a capture's visual state until the asynchronous callback:
            // a Metal screenshot can be delivered 1-2 frames after request.
            capturePending = false;
            if (stage != number) {
                failed = true;
                std::fprintf(stderr, "VIVID preview capture stage mismatch\n");
                platform::requestQuit();
                return;
            }
            ++stage;
            readyFrames = 0;
            if(captures.size()==3) {
                std::sort(captures.begin(),captures.end(),
                          [](const Capture& a,const Capture& b){return a.index<b.index;});
                auto changed=[](const Capture& a,const Capture& b) {
                    size_t pixels=0;
                    for(size_t i=0;i<a.data.size();i+=4) {
                        const int diff=std::abs(int(a.data[i])-int(b.data[i]))+
                                       std::abs(int(a.data[i+1])-int(b.data[i+1]))+
                                       std::abs(int(a.data[i+2])-int(b.data[i+2]));
                        if(diff>24)++pixels;
                    }
                    return pixels;
                };
                const auto morphed=changed(captures[0],captures[1]);
                const auto moved=changed(captures[1],captures[2]);
                // Strictly require actual pixels, not just successful GPU draws.
                complete = morphed > 200 && moved > 100 && animated;
                failed = failed || !complete;
                std::printf("VIVID Renderer3D visible morph pixels=%zu, MotionWeave moved pixels=%zu, pass=%d\n",
                            morphed,moved,int(complete));
                platform::requestQuit();
            }
        });
    }

    void frameStep(gfx::Device& dev) {
        if(failed || complete || frame>400) {
            if(frame>400 && !complete) {
                std::fprintf(stderr,
                    "VIVID preview: pipeline/capture timeout at stage=%d pending=%d readyFrames=%d motionFrames=%d\n",
                    stage,int(capturePending),readyFrames,motionFrames);
                failed=true;
            }
            platform::requestQuit();
            return;
        }
        // Keep the exact same rig pose in baseline & morph captures,
        // then advance the single MotionWeave player for the third capture.
        const bool motion = stage >= 2;
        const float dt = motion ? 0.02f : 0.0f;
        if (motion) ++motionFrames;
        motionweave::MotionWeave::update(presentation,dt);
        const auto& pose=presentation.get<motionweave::AnimationPoseComponent>(actor);
        require(pose.valid && pose.matrices.size()==model->nodes.size(),
                "MotionWeave failed to produce a usable pose");
        if(motion && pose.local[0].t.y>0.05f)animated=true;
        const bool faceActive=stage>=1;
        require(face.set(vivid::Layer::Tracking,vivid::Channel::JawOpen,faceActive?1.f:0.f),
                "jaw source invalid");
        require(face.set(vivid::Layer::Tracking,vivid::Channel::EyeBlinkRight,faceActive?1.f:0.f),
                "blink source invalid");
        face.snap();
        // Each actor gets a separate semantic expression and writable GPU
        // stream, while sharing one immutable CSR and both compute programs.
        for(int i=0;i<actorCount;++i) {
            auto expression=face.frame();
            if(faceActive && actorCount>1) {
                const float level=0.30f+0.65f*float(i%5)/4.0f;
                expression.values[static_cast<std::size_t>(vivid::Channel::JawOpen)]=level;
                expression.values[static_cast<std::size_t>(vivid::Channel::EyeBlinkRight)]=
                    1.0f-level*0.5f;
            }
            if(!actors[std::size_t(i)]->dispatch(expression,*shapeBinding))
                throw std::runtime_error(
                    "VIVID character GPU dispatch failed: " +
                    actors[std::size_t(i)]->lastError());
        }

        render::RTSCamera camera;
        camera.target={0,0.45f,0};
        camera.distance=5.8f;
        camera.pitch=0.25f;
        camera.yaw=0.18f;
        renderer.beginScene(camera,kWidth,kHeight);
        renderer.drawMesh(floor,{}, {0.32f,0.38f,0.45f,1});
        const int columns=std::min(actorCount,4);
        const int rows=(actorCount+columns-1)/columns;
        for(int i=0;i<actorCount;++i) {
            render::ModelDraw draw;
            draw.model=model.get();
            draw.nodeWorld=&pose.matrices;
            const float spacing=actorCount>1 ? 1.62f : 0.0f;
            const float x=(float(i%columns)-float(columns-1)*0.5f)*spacing;
            const float z=(float(i/columns)-float(rows-1)*0.5f)*spacing;
            const float scale=actorCount>1 ? 0.62f : 1.0f;
            draw.world=render::mat4::translate({x,1.15f,z}) *
                       render::mat4::scale({scale,scale,scale});
            if(faceActive)
                require(actors[std::size_t(i)]->attach(draw),
                        "VIVID character refused same-frame 60B render override");
            renderer.drawModel(draw);
        }
        // Stage 7AS: inspect the actual current-frame Renderer3D staging
        // rather than re-deriving visibility/LOD from the source Model.
        // This checks GPU vertex-override provenance for the single
        // character and does not claim the VIVID CPU-fed ID shader already
        // consumes the same GPU buffer.
        if(actorCount==1) {
            const auto view=renderer.modelViewProvenance(*model);
            require(view.hasModel && !view.primitives.empty() &&
                    view.frameSerial==device->frameSerial(),
                    "VIVID Renderer3D model view provenance unavailable");
            if(faceActive) {
                const bool actualOverride=std::any_of(
                    view.primitives.begin(),view.primitives.end(),
                    [&](const auto& p) {
                        return p.colourCandidate && p.customGpuStream &&
                            p.vertexOverride.valid() &&
                            p.vertexOverrideCount==
                                actors[0]->vertexCount();
                    });
                require(actualOverride,
                    "Renderer3D lost current-frame GPU actor override provenance");
            }
        }
        gfx::PassDesc pass;
        pass.clearColor[0]=.08f;pass.clearColor[1]=.1f;pass.clearColor[2]=.13f;
        renderer.render(pass);
        const auto& profile=renderer.profile();
        // A newly compiled Metal model pipeline can be Pending for several
        // frames. No blank or partially warmed frame may enter the image
        // comparison, but warmup itself is not a rendering failure.
        const bool bothPasses = profile.draws[0] > 0 && profile.draws[1] > 0;
        readyFrames = bothPasses ? readyFrames + 1 : 0;
        if (frame % 45 == 0 && !bothPasses)
            std::printf("VIVID preview waiting for colour/shadow pipelines: frame=%d stage=%d shadow=%u colour=%u pending=%d\n",
                        frame,stage,profile.draws[0],profile.draws[1],
                        int(renderer.hasPendingModelPipelineJobs()));
        if(!renderer.modelError().empty()) {
            std::fprintf(stderr,"VIVID preview Renderer3D: %s\n",renderer.modelError().c_str());
            failed=true;
        }
        const bool motionReady = stage != 2 || (motionFrames >= 18 && animated);
        if (!capturePending && stage < 3 && readyFrames >= 4 && motionReady) {
            capturePending = true;
            const char* suffix = stage == 0 ? "-baseline" :
                                 stage == 1 ? "-morph" : "-motionweave";
            std::printf("VIVID preview render ready for capture: stage=%d frame=%d shadow=%u colour=%u\n",
                        stage,frame,profile.draws[0],profile.draws[1]);
            capture(dev,suffix,stage);
        }
        dev.endPass();
        ++frame;
    }
    void shutdown() {
        if(!device)return;
        // Frame-retirement correctness is more important than teardown speed:
        // all compute outputs may still be referenced by shadow/colour draws.
        if(actorRetirement) {
            for(auto& actor:actors)
                if(!actorRetirement->retire(actor)) {
                    failed=true;
                    std::fprintf(stderr,"VIVID preview: failed to retire GPU actor\n");
                    return;
                }
            // One completion barrier releases all actors, then their last
            // immutable cache reference and finally the Renderer3D model.
            if(!actorRetirement->drain()) {
                failed=true;
                std::fprintf(stderr,"VIVID preview: GPU actor queue drain failed\n");
                return;
            }
            actors.clear();
            sharedAsset.reset();
            sharedCache.reset();
            actorRetirement.reset();
        } else if(!device->waitForResourceIdle()) {
            std::fprintf(stderr,"VIVID preview: GPU idle barrier failed; cannot retire resources\n");
            failed=true;
            return;
        }
        if(floor.vb.valid())device->destroyBuffer(floor.vb);
        if(floor.ib.valid())device->destroyBuffer(floor.ib);
        if(model)render::releaseModel(*device,*model);
        if(initialized)renderer.shutdown();
        device=nullptr;
    }
};
} // namespace

int aiverseMain(int,char**) {
    platform::AppConfig config;
    config.title="AIVERSE VIVID — Renderer3D / MotionWeave GPU verification";
    config.width=kWidth;
    config.height=kHeight;
    config.highDpi=false;
    auto state=std::make_shared<Preview>();
    platform::AppCallbacks cb;
    cb.init=[state](gfx::Device& dev) {
        try {state->initialize(dev);}
        catch(const std::exception& e) {
            std::fprintf(stderr,"VIVID preview initialization: %s\n",e.what());
            state->failed=true;
        }
    };
    cb.frame=[state](gfx::Device& dev,double) {
        if(state->failed){platform::requestQuit();return;}
        try {state->frameStep(dev);}
        catch(const std::exception& e) {
            std::fprintf(stderr,"VIVID preview frame: %s\n",e.what());
            state->failed=true;
            platform::requestQuit();
        }
    };
    cb.shutdown=[state]{state->shutdown();};
    const int platformResult=platform::run(config,std::move(cb));
    return platformResult || state->failed || !state->complete ? 1 : 0;
}
