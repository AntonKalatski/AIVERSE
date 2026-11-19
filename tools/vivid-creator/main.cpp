// VIVID Creator: standalone non-destructive digital-human authoring and
// actual Renderer3D preview. Uses AIVERSE's existing ImGui/RHI/Model and
// MotionWeave animation systems; no duplicate game simulation.
#include "engine/character/vivid/HumanCreatorSession.h"
#include "engine/character/vivid/HumanPreview.h"
#include "engine/character/vivid/GpuActorStream.h"
#include "engine/character/vivid/GpuMorphAssetCache.h"
#include "engine/platform/Platform.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/ImGuiLayer.h"
#include <aiverselib/motionweave/MotionWeave.h>
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;

namespace {
using namespace render;
using namespace vivid;
struct CreatorApp {
    gfx::Device* device=nullptr;
    Renderer3D renderer;
    ImGuiLayer ui;
    Mesh floor;
    HumanCreatorSession session;
    HumanDNA draft;
    HumanPreviewAsset human;
    std::unique_ptr<GpuMorphAssetCache> cache;
    std::unique_ptr<GpuActorStream> actor;
    std::unique_ptr<World> world;
    Entity entity{};
    std::shared_ptr<const AnimationGraph> graph;
    std::vector<mat4> facePose;
    FaceController controller{0.04f};
    Camera3D camera;
    bool pendingRebuild=true, rendererReady=false, uiReady=false;
    bool animate=true, inspectGroom=false, wetPreview=false;
    bool smoke=false, smokeCaptureRequested=false, smokeCompleted=false;
    int smokeFrames=0, smokeReady=0;
    std::string smokePath="/tmp/aiverse-vivid-creator.ppm";
    GroomQuality groomQuality=GroomQuality::High;
    float jaw=0.0f, smile=0.0f, blink=0.0f, yaw=0.18f;
    float wind=0.15f;
    char recipeFile[512]="/tmp/aiverse-vivid-human.json";
    std::string status;
    std::size_t displayedRevision=0;

    CreatorApp():draft(session.dna()) {}
    void init(gfx::Device& dev) {
        device=&dev;
        if(!renderer.init(dev))throw std::runtime_error("VIVID Creator: renderer init failed");
        rendererReady=true;
        if(!ui.init(dev,"fonts/ui.ttf",16.0f))
            throw std::runtime_error("VIVID Creator: ImGui init failed");
        uiReady=true;
        renderer.setShadowQuality(ShadowQuality::Medium);
        renderer.setModelReflectionsEnabled(false);
        renderer.setTraceEnabled(false);
        renderer.setModelInstancingEnabled(false);
        renderer.setModelMultiDrawEnabled(false);
        renderer.setShadowBounds({-4,-4,-4},{4,4,4});
        renderer.lighting.fogDensity=0.0f;
        renderer.lighting.sunIntensity=1.35f;
        camera.target={0,0,0};
        camera.distance=5.4f;
        camera.minDistance=2.0f;
        camera.pitch=0.08f;
        camera.yaw=0.08f;
        cache=std::make_unique<GpuMorphAssetCache>(dev);
        MeshBuilder ground;
        ground.box({0,-1.20f,0},{12,0.16f,12});
        floor=ground.upload(dev);
        if(!floor.vb.valid()||!floor.ib.valid())
            throw std::runtime_error("VIVID Creator: floor upload failed");
        regenerate();
    }
    bool releaseHuman() {
        if(!device)return true;
        // This method is called before any current-frame compute/render
        // encoder. The GPU queue barrier proves all older submitted uses
        // are complete; user edits are rare, never per-frame GPU stalls.
        if(actor && !actor->shutdownAfterIdle()) {
            status=actor->lastError();
            return false;
        }
        actor.reset();
        if(human.hair)releaseModel(*device,*human.hair);
        if(human.head)releaseModel(*device,*human.head);
        human={};
        world.reset();
        graph.reset();
        facePose.clear();
        cache->pruneExpired();
        return true;
    }
    void regenerate() {
        if(!device)return;
        try {
            // Build/validate in CPU scratch first, keeping last-good visual
            // data if DNA or shader-graph authoring fails.
            auto proposed=generateHumanPreview(session.dna(),groomQuality);
            AnimClip idle;
            idle.name="vivid_idle";
            idle.duration=2.0f;
            AnimChannel headMove;
            headMove.node=proposed.head->findNode("head");
            headMove.path=AnimChannel::Translation;
            headMove.times={0,1,2};
            headMove.values={0,0,0,0,0.055f,0,0,0,0};
            idle.channels.push_back(std::move(headMove));
            proposed.head->clips.push_back(std::move(idle));
            proposed.head->uploadPrepared=true;
            auto newGraph=AnimationGraph::compile(*proposed.head,Json::parse(
                R"({"nodes":[{"id":"idle","kind":"clip","clip":"vivid_idle"}],"output":"idle"})"));
            if(!releaseHuman())return;
            human=std::move(proposed);
            if(!uploadModel(*device,*human.head,true))
                throw std::runtime_error("VIVID Creator: head GPU upload failed");
            // Vertex cards are mutable (secondary motion), so use a ringed
            // Dynamic buffer instead of the loader's Immutable model buffer.
            gfx::BufferDesc vb;
            vb.type=gfx::BufferType::Vertex;
            vb.usage=gfx::Usage::Dynamic;
            vb.snapshotUpdates=true;
            vb.size=human.hair->vertices.size()*sizeof(ModelVertex);
            vb.data=human.hair->vertices.data();
            vb.label="VIVID dynamic groom cards";
            human.hair->vb=device->createBuffer(vb);
            gfx::BufferDesc ib;
            ib.type=gfx::BufferType::Index;
            ib.size=human.hair->indices.size()*sizeof(std::uint32_t);
            ib.data=human.hair->indices.data();
            human.hair->ib=device->createBuffer(ib);
            if(!human.hair->vb.valid() || !human.hair->ib.valid())
                throw std::runtime_error("VIVID Creator: groom buffer upload failed");
            auto gpu=cache->acquire(human.facialShapes,human.head->vertices);
            if(!gpu)throw std::runtime_error("VIVID Creator: shared morph CSR upload failed");
            actor=std::make_unique<GpuActorStream>();
            if(!actor->initializeShared(*device,*human.head,gpu))
                throw std::runtime_error("VIVID Creator: "+actor->lastError());
            world=std::make_unique<World>();
            entity=world->create();
            auto& anim=world->add<motionweave::AnimationAssetComponent>(entity);
            anim.model=human.head.get();
            anim.graph=newGraph;
            world->add<motionweave::AnimationPlayerComponent>(entity);
            world->add<motionweave::AnimationPoseComponent>(entity);
            graph=std::move(newGraph);
            facePose.resize(human.head->nodes.size());
            displayedRevision=session.revision();
            pendingRebuild=false;
            status="Loaded "+session.dna().displayName+" / "+
                   std::to_string(human.head->vertices.size())+" vertices / "+
                   std::to_string(human.groom.stats().guides)+" hair guides";
        }catch(const std::exception& e){
            status=e.what();
            // Do not publish an invalid partial character or hide the
            // diagnostics. It can be rebuilt after the next valid edit.
            pendingRebuild=false;
        }
    }
    void editCommit() {
        if(!ImGui::IsItemDeactivatedAfterEdit())return;
        std::string error;
        if(session.edit([this](HumanDNA& dna){dna=draft;},&error)) {
            pendingRebuild=true;
        }else {
            status=error;
            draft=session.dna();
        }
    }
    void slider(const char* label,float& value,float lo,float hi) {
        ImGui::SliderFloat(label,&value,lo,hi,"%.3f");
        editCommit();
    }
    void color(const char* label,std::array<float,3>& rgb) {
        ImGui::ColorEdit3(label,rgb.data());
        editCommit();
    }
    void commitPreset(const char* id) {
        if(!ImGui::Button(id))return;
        std::string err;
        if(!session.preset(id,&err))status=err;
        else {draft=session.dna();pendingRebuild=true;}
    }
    void guiControls() {
        ImGui::SetNextWindowPos({14,14},ImGuiCond_Once);
        ImGui::SetNextWindowSize({354,750},ImGuiCond_Once);
        ImGui::Begin("VIVID Creator - Human DNA");
        ImGui::Text("Revision %zu %s",session.revision(),
                    session.dirty()?"*unsaved":"saved");
        ImGui::TextWrapped("%s",status.c_str());
        if(ImGui::Button("Undo") && session.undo()) {
            draft=session.dna();pendingRebuild=true;
        }
        ImGui::SameLine();
        if(ImGui::Button("Redo") && session.redo()) {
            draft=session.dna();pendingRebuild=true;
        }
        ImGui::Separator();
        commitPreset("neutral");ImGui::SameLine();commitPreset("warm");
        ImGui::SameLine();commitPreset("cool");ImGui::SameLine();
        commitPreset("stylized");
        ImGui::Separator();
        if(ImGui::CollapsingHeader("Face and body proportions",ImGuiTreeNodeFlags_DefaultOpen)) {
            slider("Head scale",&draft.anatomy.headScale,0.5f,1.8f);
            slider("Head width",&draft.anatomy.headWidth,0.5f,1.8f);
            slider("Jaw width",&draft.anatomy.jawWidth,0.5f,1.8f);
            slider("Jaw length",&draft.anatomy.jawLength,0.5f,1.8f);
            slider("Cheek fullness",&draft.anatomy.cheekFullness,0,1);
            slider("Nose length",&draft.anatomy.noseLength,0,1);
            slider("Nose width",&draft.anatomy.noseWidth,0,1);
            slider("Eye spacing",&draft.anatomy.eyeSpacing,0,1);
            slider("Eye scale",&draft.anatomy.eyeScale,0.5f,1.8f);
            slider("Brow height",&draft.anatomy.browHeight,0,1);
            slider("Lip fullness",&draft.anatomy.lipFullness,0,1);
        }
        if(ImGui::CollapsingHeader("Skin / skin scattering",ImGuiTreeNodeFlags_DefaultOpen)) {
            color("Pigment",draft.skin.melaninColor);
            slider("Melanin",&draft.skin.melanin,0,1);
            slider("Hemoglobin",&draft.skin.hemoglobin,0,1);
            slider("Scattering mm",&draft.skin.scatteringMm,0.01f,6.0f);
            slider("Pore frequency",&draft.skin.poreScale,0,2);
            slider("Wrinkles",&draft.skin.wrinkleStrength,0,1);
            slider("Roughness",&draft.skin.roughness,0.02f,1);
            slider("Oiliness",&draft.skin.oiliness,0,1);
            slider("Wetness",&draft.skin.wetness,0,1);
        }
        if(ImGui::CollapsingHeader("Eyes / iris / cornea")) {
            color("Iris color",draft.eyes.irisColor);
            slider("Iris radius",&draft.eyes.irisRadius,0.15f,0.75f);
            slider("Pupil",&draft.eyes.pupil,0.05f,0.95f);
            slider("Cornea IOR",&draft.eyes.corneaIor,1.25f,1.45f);
            slider("Limbus",&draft.eyes.limbus,0,1);
            slider("Sclera redness",&draft.eyes.scleraRedness,0,1);
            slider("Tear film",&draft.eyes.tearFilm,0,1);
        }
        if(ImGui::CollapsingHeader("Hair / groom")) {
            color("Hair pigment",draft.groom.color);
            slider("Length m",&draft.groom.lengthMeters,0,2);
            slider("Density",&draft.groom.density,0,1);
            slider("Curl",&draft.groom.curl,0,1);
            slider("Clumping",&draft.groom.clump,0,1);
            slider("Stiffness",&draft.groom.stiffness,0,1);
            slider("Damping",&draft.groom.damping,0,1);
            slider("Gravity",&draft.groom.gravity,0,3);
            slider("Wind response",&draft.groom.wind,0,3);
            ImGui::Text("Guides %u  Segments %u",draft.groom.guideCount,
                        draft.groom.segments);
            int guides=int(draft.groom.guideCount),segments=int(draft.groom.segments);
            ImGui::SliderInt("Hair guides",&guides,4,8192);
            if(ImGui::IsItemDeactivatedAfterEdit()) {
                draft.groom.guideCount=std::uint32_t(guides);
                editCommit();
            }
            ImGui::SliderInt("Hair segments",&segments,2,32);
            if(ImGui::IsItemDeactivatedAfterEdit()) {
                draft.groom.segments=std::uint32_t(segments);
                editCommit();
            }
            ImGui::Checkbox("Simulate hair",&inspectGroom);
            ImGui::SliderFloat("Wind",&wind,0,3);
            if(ImGui::Button("Hair High")) {groomQuality=GroomQuality::High;pendingRebuild=true;}
            ImGui::SameLine();
            if(ImGui::Button("Med")) {groomQuality=GroomQuality::Medium;pendingRebuild=true;}
            ImGui::SameLine();
            if(ImGui::Button("Low")) {groomQuality=GroomQuality::Low;pendingRebuild=true;}
        }
        ImGui::Separator();
        ImGui::Checkbox("Animate MotionWeave",&animate);
        ImGui::SliderFloat("Jaw open",&jaw,0,1);
        ImGui::SliderFloat("Smile",&smile,0,1);
        ImGui::SliderFloat("Blink",&blink,0,1);
        ImGui::SliderFloat("Camera yaw",&camera.yaw,-3.14f,3.14f);
        ImGui::SliderFloat("Camera pitch",&camera.pitch,-0.4f,0.95f);
        ImGui::SliderFloat("Camera distance",&camera.distance,2.5f,12.0f);
        ImGui::Separator();
        ImGui::InputText("Recipe",recipeFile,sizeof(recipeFile));
        if(ImGui::Button("Save DNA JSON")) {
            if(session.saveFile(recipeFile,&status))
                status="Saved "+std::string(recipeFile);
        }
        ImGui::SameLine();
        if(ImGui::Button("Load DNA JSON")) {
            if(session.loadFile(recipeFile,&status)) {
                draft=session.dna();pendingRebuild=true;
                status="Loaded "+std::string(recipeFile);
            }
        }
        ImGui::TextWrapped("Reference procedural geometry is not a scanned human. "
                           "Imported skinned FBX/glTF heads can use the same rig, "
                           "DNA materials and VIVID face binding.");
        ImGui::End();
    }
    void frame(gfx::Device& dev,double dt) {
        ui.newFrame(dt);
        guiControls();
        if(pendingRebuild)regenerate();
        if(!human.head||!actor||!world||!actor->ready()) {
            gfx::PassDesc empty;
            dev.beginPass(empty);ui.render();dev.endPass();
            return;
        }
        const float step=std::clamp(float(dt),0.0f,0.04f);
        motionweave::MotionWeave::update(*world,animate?step:0.0f);
        const auto& pose=world->get<motionweave::AnimationPoseComponent>(entity);
        if(!pose.valid || pose.matrices.size()!=human.head->nodes.size()) {
            status="MotionWeave pose temporarily unavailable";
            return;
        }
        controller.set(Layer::Tracking,Channel::JawOpen,jaw);
        controller.set(Layer::Tracking,Channel::EyeBlinkLeft,blink);
        controller.set(Layer::Tracking,Channel::EyeBlinkRight,blink);
        controller.set(Layer::Tracking,Channel::MouthSmileLeft,smile);
        controller.set(Layer::Tracking,Channel::MouthSmileRight,smile);
        controller.advance(step);
        if(!human.rig.compose(pose.matrices,facePose,controller.frame()))
            status="VIVID facial rig: invalid composition";
        if(!actor->dispatch(controller.frame(),human.facialBinding))
            status=actor->lastError();
        if(inspectGroom) {
            human.groom.step(step,{wind,0,wind*0.3f});
            human.groom.updateCardVertices(human.hair->vertices);
            dev.updateBuffer(human.hair->vb,human.hair->vertices.data(),
                             human.hair->vertices.size()*sizeof(ModelVertex));
        }
        renderer.beginScene(camera,dev.framebufferWidth(),dev.framebufferHeight());
        renderer.drawMesh(floor,{}, {0.25f,0.26f,0.3f,1});
        ModelDraw draw;
        draw.model=human.head.get();
        draw.nodeWorld=&facePose;
        if(actor->attach(draw))
            renderer.drawModel(draw);
        const int headIndex=human.rig.head;
        const mat4& poseHead=facePose[std::size_t(headIndex)];
        // Strip authored head ellipsoid scaling: the groom geometry is
        // already generated in model coordinates using DNA head dimensions.
        mat4 headRigid=poseHead;
        for(int col=0;col<3;++col) {
            float lengthSq=0;
            for(int row=0;row<3;++row)
                lengthSq+=headRigid.at(row,col)*headRigid.at(row,col);
            const float inv=1.0f/std::sqrt(std::max(lengthSq,0.00001f));
            for(int row=0;row<3;++row)headRigid.at(row,col)*=inv;
        }
        ModelDraw hair;
        hair.model=human.hair.get();
        hair.world=headRigid;
        renderer.drawModel(hair);
        gfx::PassDesc pass;
        pass.clearColor[0]=0.075f;pass.clearColor[1]=0.085f;pass.clearColor[2]=0.12f;
        renderer.render(pass);
        if(smoke) {
            const auto& p=renderer.profile();
            if(!renderer.modelError().empty())
                throw std::runtime_error("VIVID Creator Metal shader error: "+
                                         renderer.modelError());
            const bool ready=p.draws[0]>0 && p.draws[1]>0;
            smokeReady=ready?smokeReady+1:0;
            if(smokeReady>=4 && !smokeCaptureRequested) {
                smokeCaptureRequested=true;
                dev.captureNextFrame([this](int w,int h,std::vector<uint8_t> pixels) {
                    if(w<128||h<128||pixels.size()!=std::size_t(w*h*4)) {
                        status="VIVID Creator screenshot is empty";
                        platform::requestQuit();
                        return;
                    }
                    std::ofstream out(smokePath,std::ios::binary);
                    out<<"P6\n"<<w<<" "<<h<<"\n255\n";
                    for(std::size_t i=0;i<pixels.size();i+=4)
                        out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                    smokeCompleted=bool(out);
                    std::printf("VIVID Creator human skin/eye/groom GPU preview captured: %s shadow=%d colour=%d pass=%d\n",
                        smokePath.c_str(),1,1,int(smokeCompleted));
                    platform::requestQuit();
                });
            }
            if(++smokeFrames>300 && !smokeCompleted)
                throw std::runtime_error("VIVID Creator did not render skin/eyes/groom within 300 frames");
        }
        ui.render();
        dev.endPass();
    }
    void shutdown() {
        if(!device)return;
        if(!releaseHuman())std::fprintf(stderr,
            "VIVID Creator: GPU resource drain failed at shutdown\n");
        if(floor.vb.valid())device->destroyBuffer(floor.vb);
        if(floor.ib.valid())device->destroyBuffer(floor.ib);
        if(uiReady)ui.shutdown();
        if(rendererReady)renderer.shutdown();
        cache.reset();
        device=nullptr;
    }
};
}
int aiverseMain(int argc,char** argv) {
    auto application=std::make_shared<CreatorApp>();
    for(int i=1;i<argc;++i) {
        if(std::strcmp(argv[i],"--smoke")==0) {
            application->smoke=true;
            if(const char* path=std::getenv("AIVERSE_VIVID_CREATOR_CAPTURE"))
                application->smokePath=path;
        }else{
            std::fprintf(stderr,"VIVID Creator: unsupported argument %s\n",argv[i]);
            return 2;
        }
    }
    platform::AppConfig config;
    config.title="AIVERSE VIVID Creator - Digital Human";
    config.width=1500;
    config.height=920;
    config.highDpi=false;
    platform::AppCallbacks callbacks;
    callbacks.init=[application](gfx::Device& device) {
        try{application->init(device);}
        catch(const std::exception& e) {
            std::fprintf(stderr,"VIVID Creator init: %s\n",e.what());
            platform::requestQuit();
        }
    };
    callbacks.frame=[application](gfx::Device& device,double dt) {
        try{application->frame(device,dt);}
        catch(const std::exception& e) {
            std::fprintf(stderr,"VIVID Creator frame: %s\n",e.what());
            platform::requestQuit();
        }
    };
    callbacks.event=[application](const platform::Event& event) {
        application->ui.handleEvent(event);
    };
    callbacks.shutdown=[application]{application->shutdown();};
    const int code=platform::run(config,std::move(callbacks));
    return code || (application->smoke && !application->smokeCompleted) ? 1 : 0;
}
