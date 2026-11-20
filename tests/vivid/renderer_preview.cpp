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
#include "engine/character/vivid/SculptGpuSubmittedView.h"
#include "engine/character/vivid/SculptGpuIdPass.h"
#include "engine/character/vivid/SculptGpuColorPixelOracle.h"
#include "engine/character/vivid/SculptGpuVertexSource.h"
#include "engine/character/vivid/SkinnedWeightPick.h"
#include <array>
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
    // Stage 7AX: the independent ID target and a real *presented*
    // Renderer3D colour screenshot must both describe one identical GPU
    // frame. Callbacks can arrive in either order; never compare them with
    // a later MotionWeave pose.
    vivid::SculptGpuIdPass pixelIdPass;
    gfx::TextureHandle colourPixelSnapshot_{};
    bool pixelIdReady=false, imageDiffPassed=false;
    struct PixelProbe {
        bool requested=false, colourReady=false, idReady=false, checked=false;
        bool sceneDepthReady=false, idDepthReady=false;
        uint64_t frameSerial=0;
        uint32_t x=0,y=0;
        float sceneDepth=1.f,idDepth=1.f;
        std::array<uint8_t,4> colour{},id{};
    };
    std::array<PixelProbe,3> pixelProbes{};

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
        if(actorCount==1) {
            // readTexturePixelAsync needs an offscreen RGBA8 target.
            // copyColor from this exact screen frame requires a readable
            // Metal drawable rather than captureNextFrame (may lag).
            dev.setScreenReadable(true);
            gfx::TextureDesc snapshot{};
            snapshot.width=kWidth;
            snapshot.height=kHeight;
            snapshot.format=gfx::PixelFormat::RGBA8;
            snapshot.renderTarget=true;
            snapshot.filter=gfx::Filter::Nearest;
            snapshot.label="VIVID same-frame Renderer3D colour sample";
            colourPixelSnapshot_=dev.createTexture(snapshot);
            require(colourPixelSnapshot_.valid(),
                    "VIVID same-frame colour sample target unavailable");
            pixelIdReady=pixelIdPass.initialize(dev);
            require(pixelIdReady && pixelIdPass.supportsActualGpuVertexStream(),
                    "VIVID Stage 7AY requires native Metal GPU-source ID shader");
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
            // Full-frame screenshot is only an A/B visual artifact. A
            // copyColor->RGBA8 async readback below owns strict same-frame
            // pixel parity; screenshot callbacks may lag by several frames.
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
                // Captures and ID readbacks may finish on different
                // beginFrame callbacks. Check scene-pixel parity only once
                // BOTH same-frame results have been delivered.
                imageDiffPassed=morphed>200 && moved>100 && animated;
                std::printf("VIVID Renderer3D visible morph pixels=%zu, MotionWeave moved pixels=%zu, capture-pass=%d\n",
                            morphed,moved,int(imageDiffPassed));
            }
        });
    }

    void frameStep(gfx::Device& dev) {
        if(actorCount==1 && pixelIdReady) {
            vivid::SculptGpuIdPass::Completed completedId;
            if(pixelIdPass.takeCompleted(completedId)) {
                const auto& request=completedId.request;
                require(request.generation>=2 && request.generation<=3,
                        "VIVID parity readback had unexpected request generation");
                auto& probe=pixelProbes[std::size_t(request.generation-1u)];
                require(probe.requested && !probe.idReady &&
                        probe.frameSerial==request.frameSerial &&
                        probe.x==request.x && probe.y==request.y &&
                        completedId.actualOverride &&
                        completedId.depthValid &&
                        completedId.rgba.size()==4,
                        "VIVID parity ID readback was stale or not GPU-sourced");
                for(size_t c=0;c<4;++c)probe.id[c]=completedId.rgba[c];
                probe.idReady=true;
                probe.idDepth=completedId.depth01;
                probe.idDepthReady=true;
            }
            for(int stageIndex=1;stageIndex<=2;++stageIndex) {
                auto& probe=pixelProbes[std::size_t(stageIndex)];
                if(!probe.requested || !probe.colourReady ||
                   !probe.idReady || !probe.sceneDepthReady ||
                   !probe.idDepthReady || probe.checked)continue;
                const auto comparison=vivid::compareSculptGpuColorIdCoverage(
                    probe.colour,probe.id,{20,26,33},36);
                require(comparison.verdict==
                            vivid::SculptGpuColorPixelResult::Verdict::Covered,
                    "VIVID same-frame Metal Renderer3D colour and GPU actor ID pixel coverage differ");
                constexpr float kMaxDepthDifference=0.003f;
                const float depthDelta=std::fabs(
                    probe.sceneDepth-probe.idDepth);
                require(std::isfinite(depthDelta) &&
                        probe.sceneDepth>=0.f && probe.sceneDepth<1.f &&
                        probe.idDepth>=0.f && probe.idDepth<1.f &&
                        depthDelta<=kMaxDepthDifference,
                    "VIVID real Metal Renderer3D screen vs GPU ID depth mismatch");
                probe.checked=true;
                std::printf("VIVID same-frame GPU colour/ID coverage stage=%d frame=%llu xy=%u,%u distance=%d id=%u,%u,%u,%u depth_scene=%f depth_id=%f delta=%f PASS\n",
                    stageIndex,static_cast<unsigned long long>(probe.frameSerial),
                    probe.x,probe.y,comparison.distanceFromClear,
                    unsigned(probe.id[0]),unsigned(probe.id[1]),
                    unsigned(probe.id[2]),unsigned(probe.id[3]),
                    double(probe.sceneDepth),double(probe.idDepth),
                    double(depthDelta));
            }
        }
        if(stage>=3 && captures.size()==3 &&
           (actorCount!=1 || (pixelProbes[1].checked &&
                               pixelProbes[2].checked))) {
            complete=imageDiffPassed && animated;
            failed=failed || !complete;
            platform::requestQuit();
            return;
        }
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
        if(actorCount==1)renderer.trackModelSubmissions(model.get());
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
        // 7AW: staging a GPU morph vertexOverride is NOT a submission.
        // After the real colour pass, inspect the actual accepted renderer
        // indexed command; never count pending pipeline-warm preflight.
        if(actorCount==1 && renderer.profile().draws[1]>0 &&
           !renderer.hasPendingModelPipelineJobs()) {
            const auto submission=renderer.modelSubmissionProvenance(*model);
            require(submission.frameSerial==device->frameSerial() &&
                    submission.mainPassEncoded && !submission.truncated,
                    "renderer accepted-command frame provenance missing");
            const bool direct=std::any_of(
                submission.commands.begin(),submission.commands.end(),
                [&](const auto& command) {
                    return command.phase==1u &&
                        command.kind==
                            render::Renderer3D::ModelSubmissionKind::Direct &&
                        command.pipeline.valid() &&
                        command.source.model==model.get() &&
                        (!faceActive ||
                         (command.source.vertexOverride.valid() &&
                          command.source.vertexOverrideCount==
                              actors[0]->vertexCount()));
                });
            require(direct,
                "VIVID colour-pass command did not submit the GPU actor");
            const auto staged=renderer.modelViewProvenance(*model);
            const auto matched=vivid::validateSculptGpuSubmittedView(
                staged,submission,*model,0,device->frameSerial());
            require(matched.accepted,
                "VIVID renderer staged and accepted model source disagree");
        }
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
        bool recordParityPixel=false;
        if (!capturePending && stage < 3 && readyFrames >= 4 &&
            motionReady && (!pixelIdReady || !pixelIdPass.pending())) {
            capturePending = true;
            recordParityPixel=actorCount==1 && stage>0;
            const char* suffix = stage == 0 ? "-baseline" :
                                 stage == 1 ? "-morph" : "-motionweave";
            std::printf("VIVID preview render ready for capture: stage=%d frame=%d shadow=%u colour=%u\n",
                        stage,frame,profile.draws[0],profile.draws[1]);
            capture(dev,suffix,stage);
        }
        dev.endPass();
        if(recordParityPixel) {
            require(pixelIdReady && stage>0 && stage<3,
                    "VIVID Metal pixel parity was not initialized");
            const auto staged=renderer.modelViewProvenance(*model);
            const auto submitted=renderer.modelSubmissionProvenance(*model);
            const auto accepted=vivid::validateSculptGpuSubmittedView(
                staged,submitted,*model,0,dev.frameSerial());
            require(accepted.accepted,
                "VIVID parity colour model was not a matching encoded draw");
            const render::Renderer3D::ModelViewPrimitive* selected=nullptr;
            for(const auto& primitive:staged.primitives) {
                if(primitive.meshIndex==0u) {
                    if(selected && (selected->world.m[12]!=primitive.world.m[12] ||
                                    selected->vertexOverride!=primitive.vertexOverride))
                        throw std::runtime_error("ambiguous GPU actor world");
                    selected=&primitive;
                }
            }
            require(selected && selected->customGpuStream &&
                    selected->vertexOverride.valid(),
                    "VIVID parity lost original 60B GPU actor source");
            vivid::SkinnedWeightSurface reference;
            require(vivid::prepareSkinnedWeightSurface(
                        *model,0,pose.matrices,reference),
                    "VIVID parity CPU source map could not be prepared");
            vivid::SculptGpuVertexSource realGpu;
            require(vivid::prepareSculptGpuVertexSource(
                        *model,0,pose.matrices,selected->world,
                        selected->vertexOverride,
                        selected->vertexOverrideCount,
                        {},model->vertices.size(),realGpu),
                    "VIVID parity real GPU actor source could not be prepared");
            // Project the head's world centre, well inside the visible
            // procedural sphere. Both RHI and screenshot use top-left pixels.
            const render::float4 clip=renderer.viewProj()*
                render::float4{0.f,1.15f,0.f,1.f};
            require(std::isfinite(clip.w) && clip.w>0.f,
                    "VIVID parity projected head behind the camera");
            const float ndcX=clip.x/clip.w,ndcY=clip.y/clip.w;
            require(std::isfinite(ndcX) && std::isfinite(ndcY) &&
                    std::abs(ndcX)<0.95f && std::abs(ndcY)<0.95f,
                    "VIVID parity head centre is outside the viewport");
            auto& probe=pixelProbes[std::size_t(stage)];
            require(!probe.requested,
                    "VIVID parity stage was recorded more than once");
            probe.x=uint32_t(std::clamp(
                int((ndcX*0.5f+0.5f)*float(kWidth)),0,kWidth-1));
            probe.y=uint32_t(std::clamp(
                int((0.5f-ndcY*0.5f)*float(kHeight)),0,kHeight-1));
            vivid::SculptGpuPickRequest request;
            request.generation=uint64_t(stage+1);
            request.modelRevision=1;
            request.frameSerial=dev.frameSerial();
            request.meshId=0;
            request.width=kWidth;request.height=kHeight;
            request.x=probe.x;request.y=probe.y;
            probe.frameSerial=request.frameSerial;
            probe.requested=true;
            const int probeStage=stage;
            const uint64_t snapshotFrame=request.frameSerial;
            require(colourPixelSnapshot_.valid() &&
                    dev.copyColor({},colourPixelSnapshot_),
                    "VIVID current presented framebuffer could not be copied");
            require(dev.readTexturePixelAsync(
                        colourPixelSnapshot_,int(request.x),int(request.y),
                        [this,probeStage,snapshotFrame](
                                std::vector<std::uint8_t> bytes) {
                            auto& target=pixelProbes[std::size_t(probeStage)];
                            if(!target.requested ||
                               target.frameSerial!=snapshotFrame ||
                               target.colourReady || bytes.size()!=4u) {
                                failed=true;
                                std::fprintf(stderr,
                                    "VIVID exact-frame colour GPU callback invalid\n");
                                platform::requestQuit();
                                return;
                            }
                            for(std::size_t i=0;i<4u;++i)
                                target.colour[i]=bytes[i];
                            target.colourReady=true;
                        }),
                    "VIVID exact-frame native RHI colour readback failed");
            require(dev.readTextureDepthPixelAsync(
                        {},int(request.x),int(request.y),
                        [this,probeStage,snapshotFrame](bool valid,float depth) {
                            auto& target=pixelProbes[std::size_t(probeStage)];
                            if(!target.requested ||
                               target.frameSerial!=snapshotFrame ||
                               target.sceneDepthReady || !valid ||
                               !std::isfinite(depth)) {
                                failed=true;
                                std::fprintf(stderr,
                                    "VIVID scene GPU depth callback missing/stale/invalid\n");
                                platform::requestQuit();
                                return;
                            }
                            target.sceneDepth=depth;
                            target.sceneDepthReady=true;
                        }),
                    "VIVID actual Metal main-scene GPU depth readback unavailable");
            require(pixelIdPass.record(
                        *model,reference,model->vertices.size(),{},
                        renderer.viewProj(),request,{}, {},&realGpu,true),
                    "VIVID real-actor colour/ID/depth pixel parity pass not encoded");
            // No main-pass resume needed: preview ends the frame here.
        }
        ++frame;
    }
    void shutdown() {
        if(!device)return;
        // GPU ID targets and its async scratch are retained until a
        // verified idle barrier, independently of actor resource ownership.
        if(pixelIdReady && !pixelIdPass.shutdownAfterIdle()) {
            std::fprintf(stderr,"VIVID parity: ID pass GPU idle retirement failed\n");
            failed=true;
            return;
        }
        pixelIdReady=false;
        // No in-flight read/copy can survive the successful GPU idle proof.
        if(colourPixelSnapshot_.valid()) {
            device->destroyTexture(colourPixelSnapshot_);
            colourPixelSnapshot_={};
        }
        device->setScreenReadable(false);
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
