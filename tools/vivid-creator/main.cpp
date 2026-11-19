// VIVID Creator: standalone non-destructive digital-human authoring and
// actual Renderer3D preview. Uses AIVERSE's existing ImGui/RHI/Model and
// MotionWeave animation systems; no duplicate game simulation.
#include "engine/character/vivid/HumanCreatorSession.h"
#include "engine/character/vivid/HumanPreview.h"
#include "engine/character/vivid/HumanImport.h"
#include "engine/character/vivid/MorphAuthoring.h"
#include "engine/character/vivid/ViewportSculptPick.h"
#include "engine/character/vivid/ScalpSkinning.h"
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
#include <filesystem>
#include <iterator>
#include <limits>
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
    int draftGuides=512,draftSegments=8;
    std::array<char,128> rootBone{},headBone{},jawBone{},leftEyeBone{},rightEyeBone{};
    char recipeFile[512]="/tmp/aiverse-vivid-human.json";
    char manifestFile[512]="/tmp/aiverse-human-package.json";
    std::string importedManifest;
    HumanImportManifest importedPackage;
    std::unique_ptr<MorphAuthoring> morphDraft;
    std::size_t morphSourceVertices=0;
    std::vector<MorphVec3> morphSourcePositions;
    float morphBrushRadius=0.1f;
    bool sculptMirrorX=false;
    int sculptMeshIndex=-1;
    bool sculptPickArmed=false;
    int morphTargetIndex=0,morphVertexIndex=0;
    int routeChannelIndex=0;
    char newMorphName[129]="newCorrective";
    render::float3 morphEditOffset{};
    bool morphDirty=false;
    std::array<char,129> scalpProxyName{};
    // Immutable bind-pose vertices and a single rigid MotionWeave node.
    // Updated positions are always supplied in the groom's local space.
    std::vector<render::float3> scalpBindPositions;
    std::vector<render::float3> scalpAnimatedPositions;
    std::vector<render::float3> scalpSkinScratch;
    int scalpPoseNode=-1;
    std::unique_ptr<ScalpSkinning> scalpSkinning;
    render::mat4 scalpBindNodeInverse{};
    render::mat4 scalpBindHeadRigid{};
    bool externalMode=false;
    bool commandLineAsset=false;
    std::string status;
    std::size_t displayedRevision=0;

    CreatorApp():draft(session.dna()) {syncDraft();}
    void syncDraft() {
        draft=session.dna();
        draftGuides=int(draft.groom.guideCount);
        draftSegments=int(draft.groom.segments);
        auto copy=[](std::array<char,128>& dst,const std::string& source) {
            std::snprintf(dst.data(),dst.size(),"%s",source.c_str());
        };
        copy(rootBone,draft.rig.rootBone);
        copy(headBone,draft.rig.headBone);
        copy(jawBone,draft.rig.jawBone);
        copy(leftEyeBone,draft.rig.leftEyeBone);
        copy(rightEyeBone,draft.rig.rightEyeBone);
    }
    void init(gfx::Device& dev) {
        device=&dev;
        if(!renderer.init(dev))throw std::runtime_error("VIVID Creator: renderer init failed");
        rendererReady=true;
        if(!ui.init(dev,AIVERSE_SOURCE_DIR "/engine/assets/fonts/ui.ttf",16.0f))
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
        if(commandLineAsset)
            loadExternal(manifestFile);
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
        scalpBindPositions.clear();
        scalpAnimatedPositions.clear();
        scalpSkinScratch.clear();
        scalpPoseNode=-1;
        scalpSkinning.reset();
        cache->pruneExpired();
        return true;
    }
    static std::string fileContents(const std::filesystem::path& file,
                                    std::size_t maxSize) {
        std::ifstream in(file,std::ios::binary);
        if(!in)throw std::runtime_error("VIVID Creator: unable to open "+file.string());
        std::string bytes((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
        if(bytes.size()>maxSize)
            throw std::runtime_error("VIVID Creator: file budget exceeded");
        return bytes;
    }
    static HumanImportManifest readManifest(const std::string& path) {
        return HumanImportManifest::fromJson(Json::parse(
            fileContents(path,1024*1024)));
    }
    static render::FileReader readerFor(const std::string& path) {
        const auto directory=std::filesystem::absolute(
            std::filesystem::path(path)).parent_path();
        return [directory](const std::string& relative,std::string& bytes) {
            if(relative.empty() || relative[0]=='/' ||
               relative.find("..")!=std::string::npos ||
               relative.find('\\')!=std::string::npos ||
               relative.find(':')!=std::string::npos)return false;
            try {
                bytes=fileContents(directory/relative,64ull*1024ull*1024ull);
                return true;
            }catch(...){return false;}
        };
    }
    void loadExternal(const std::string& path) {
        auto manifest=readManifest(path);
        auto imported=importHumanAsset(manifest,readerFor(path));
        std::string error;
        if(!session.loadJson(imported.dna.toJson(),&error))
            throw std::runtime_error("VIVID Creator: imported DNA invalid: "+error);
        syncDraft();
        morphSourceVertices=imported.sourceVertexCount;
        morphSourcePositions.clear();
        morphSourcePositions.reserve(morphSourceVertices);
        for(std::size_t i=0;i<morphSourceVertices;++i) {
            const auto& p=imported.model->vertices[i].pos;
            morphSourcePositions.push_back({p.x,p.y,p.z});
        }
        morphDraft=std::make_unique<MorphAuthoring>(
            MorphAuthoring::fromCompiled(imported.facialShapes));
        morphTargetIndex=0;
        morphVertexIndex=0;
        morphEditOffset={};
        sculptMeshIndex=-1;
        sculptPickArmed=false;
        morphDirty=false;
        importedManifest=path;
        importedPackage=std::move(manifest);
        std::snprintf(scalpProxyName.data(),scalpProxyName.size(),"%s",
                      importedPackage.scalpColliderMesh.c_str());
        externalMode=true;
        pendingRebuild=true;
        status="Imported open human package: "+path;
    }
    static render::mat4 rigidHeadMatrix(render::mat4 m) {
        for(int col=0;col<3;++col) {
            float lengthSq=0;
            for(int row=0;row<3;++row)
                lengthSq+=m.at(row,col)*m.at(row,col);
            const float inv=1.0f/std::sqrt(std::max(lengthSq,0.00001f));
            for(int row=0;row<3;++row)m.at(row,col)*=inv;
        }
        return m;
    }
    struct ScalpPoseBinding {
        std::vector<render::float3> bindPositions;
        int node=-1;
        std::unique_ptr<ScalpSkinning> skinning;
        render::mat4 nodeBindInverse{};
        render::mat4 headBindRigid{};
    };
    static ScalpPoseBinding installImportedScalpCollider(
        HumanGroomRuntime& groom,const render::Model& model,
        const std::string& meshName,const HumanRigBinding& rig) {
        if(meshName.empty())return {};
        const auto it=std::find_if(model.meshes.begin(),model.meshes.end(),
            [&](const render::MeshDef& mesh){return mesh.name==meshName;});
        if(it==model.meshes.end())
            throw std::runtime_error("VIVID Creator: scalp collider mesh not found");
        const auto meshId=int(it-model.meshes.begin());
        int poseNode=-1;
        for(std::size_t i=0;i<model.nodes.size();++i) {
            const auto& node=model.nodes[i];
            if(node.mesh!=meshId)continue;
            if(poseNode>=0)
                throw std::runtime_error("VIVID Creator: proxy must belong to one model node");
            poseNode=int(i);
        }
        if(poseNode<0 || !rig.valid() ||
           std::size_t(poseNode)>=model.bindWorld.size() ||
           std::size_t(rig.head)>=model.bindWorld.size())
            throw std::runtime_error("VIVID Creator: missing scalp/head bind-pose node");
        // The authoring contract is a closed, bind-pose proxy whose vertices
        // are already expressed in the groom's local coordinate system.
        std::vector<render::float3> positions;
        std::vector<std::uint32_t> indices;
        std::vector<std::uint32_t> sourceIds;
        const bool skinned=model.nodes[std::size_t(poseNode)].skin>=0;
        std::vector<std::uint32_t> remap(model.vertices.size(),
            std::numeric_limits<std::uint32_t>::max());
        for(const auto& primitive:it->primitives) {
            if(skinned!=!primitive.jointPalette.empty())
                throw std::runtime_error("VIVID Creator: scalp proxy skin/palette mismatch");
            const auto begin=std::size_t(primitive.firstIndex);
            const auto end=begin+std::size_t(primitive.indexCount);
            if(begin>model.indices.size() || end>model.indices.size())
                throw std::runtime_error("VIVID Creator: scalp proxy index range invalid");
            for(std::size_t i=begin;i<end;++i) {
                const auto original=model.indices[i];
                if(original>=model.vertices.size())
                    throw std::runtime_error("VIVID Creator: scalp proxy vertex out of bounds");
                auto& mapped=remap[original];
                if(mapped==std::numeric_limits<std::uint32_t>::max()) {
                    mapped=std::uint32_t(positions.size());
                    positions.push_back(model.vertices[original].pos);
                    sourceIds.push_back(original);
                }
                indices.push_back(mapped);
            }
        }
        ScalpPoseBinding binding;
        binding.node=poseNode;
        binding.nodeBindInverse=model.bindWorld[std::size_t(poseNode)].inverse();
        binding.headBindRigid=rigidHeadMatrix(model.bindWorld[std::size_t(rig.head)]);
        if(skinned) {
            binding.skinning=std::make_unique<ScalpSkinning>(
                ScalpSkinning::compile(model,std::size_t(meshId),
                                      std::size_t(poseNode),sourceIds));
            // Skinning produces model-world points. Convert to the same
            // head-local frame used to draw the procedural hair cards.
            if(!binding.skinning->evaluate(model.bindWorld,binding.headBindRigid,
                                           positions))
                throw std::runtime_error("VIVID Creator: invalid skinned scalp bind pose");
        }
        if(!groom.setCollisionMesh(positions,indices))
            throw std::runtime_error("VIVID Creator: scalp proxy must be a closed nondegenerate mesh within the 16K/32K budget");
        if(!groom.attachRootsToCollisionMesh())
            throw std::runtime_error("VIVID Creator: could not attach groom roots to scalp proxy");
        binding.bindPositions=std::move(positions);
        return binding;
    }
    void regenerate() {
        if(!device)return;
        try {
            // Build/validate in CPU scratch first, keeping last-good visual
            // data if DNA or shader-graph authoring fails.
            HumanPreviewAsset proposed;
            ScalpPoseBinding proposedScalp;
            if(externalMode) {
                const auto& manifest=importedPackage;
                auto imported=importHumanAsset(manifest,readerFor(importedManifest));
                proposed.head=std::move(imported.model);
                proposed.rig=imported.rig;
                proposed.facialShapes=morphDraft
                    ? morphDraft->compile() : std::move(imported.facialShapes);
                proposed.facialBinding=FaceShapeBinding::compile(
                    proposed.facialShapes,manifest.routes);
                for(const auto& [name,surface]:manifest.materials) {
                    const auto index=proposed.head->findMaterial(name);
                    if(index>=0)proposed.head->materials[std::size_t(index)]=
                        humanMaterialFromSource(
                            surface,session.dna(),
                            proposed.head->materials[std::size_t(index)]);
                }
                const bool hasAuthoredHair=std::any_of(
                    manifest.materials.begin(),manifest.materials.end(),
                    [](const auto& pair) {
                        return pair.second==HumanSurface::Hair ||
                               pair.second==HumanSurface::Eyelash;
                    });
                if(!hasAuthoredHair) {
                    proposed.groom.generate(session.dna(),groomQuality);
                    proposedScalp=installImportedScalpCollider(
                        proposed.groom,*proposed.head,
                        manifest.scalpColliderMesh,proposed.rig);
                    proposed.hair=proposed.groom.makeCardModel(session.dna());
                }
                // Artist-authored hair meshes are already in proposed.head,
                // with their original strand/UV/alpha maps preserved by
                // humanMaterialFromSource. Never overlay a second wig.
            } else {
                proposed=generateHumanPreview(session.dna(),groomQuality);
            }
            AnimClip idle;
            idle.name="vivid_idle";
            idle.duration=2.0f;
            AnimChannel headMove;
            headMove.node=proposed.head->findNode(session.dna().rig.headBone);
            if(headMove.node<0)
                throw std::runtime_error("VIVID Creator: imported head joint missing");
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
            scalpBindPositions=std::move(proposedScalp.bindPositions);
            scalpAnimatedPositions.resize(scalpBindPositions.size());
            scalpSkinScratch.resize(scalpBindPositions.size());
            scalpPoseNode=proposedScalp.node;
            scalpSkinning=std::move(proposedScalp.skinning);
            scalpBindNodeInverse=proposedScalp.nodeBindInverse;
            scalpBindHeadRigid=proposedScalp.headBindRigid;
            if(!uploadModel(*device,*human.head,true))
                throw std::runtime_error("VIVID Creator: head GPU upload failed");
            // Vertex cards are mutable (secondary motion), so use a ringed
            // Dynamic buffer instead of the loader's Immutable model buffer.
            if(human.hair) {
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
            }
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
            syncDraft();
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
        else {syncDraft();pendingRebuild=true;}
    }
    void boneAlias(const char* label,std::array<char,128>& text,
                   std::string HumanRigDNA::*member) {
        ImGui::InputText(label,text.data(),text.size());
        if(!ImGui::IsItemDeactivatedAfterEdit())return;
        draft.rig.*member=text.data();
        std::string error;
        if(session.edit([this](HumanDNA& profile){profile=draft;},&error))
            pendingRebuild=true;
        else {status=error;syncDraft();}
    }
    void guiControls() {
        ImGui::SetNextWindowPos({14,14},ImGuiCond_Once);
        ImGui::SetNextWindowSize({354,750},ImGuiCond_Once);
        ImGui::Begin("VIVID Creator - Human DNA");
        ImGui::Text("Revision %zu %s",session.revision(),
                    session.dirty()?"*unsaved":"saved");
        ImGui::TextWrapped("%s",status.c_str());
        if(ImGui::Button("Undo") && session.undo()) {
            syncDraft();pendingRebuild=true;
        }
        ImGui::SameLine();
        if(ImGui::Button("Redo") && session.redo()) {
            syncDraft();pendingRebuild=true;
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
            slider("Mouth width",&draft.anatomy.mouthWidth,0,1);
            slider("Ear scale",&draft.anatomy.earScale,0.5f,1.8f);
            slider("Neck length",&draft.anatomy.neckLength,0,1);
            slider("Shoulder width",&draft.anatomy.shoulderWidth,0,1);
            slider("Body build",&draft.anatomy.bodyBuild,0,1);
            slider("Height metres",&draft.anatomy.heightMeters,0.8f,2.6f);
        }
        if(ImGui::CollapsingHeader("Rig / imported bone names")) {
            boneAlias("Root bone",rootBone,&HumanRigDNA::rootBone);
            boneAlias("Head bone",headBone,&HumanRigDNA::headBone);
            boneAlias("Jaw bone",jawBone,&HumanRigDNA::jawBone);
            boneAlias("Left eye bone",leftEyeBone,&HumanRigDNA::leftEyeBone);
            boneAlias("Right eye bone",rightEyeBone,&HumanRigDNA::rightEyeBone);
            ImGui::TextWrapped("Retargets imported glTF/FBX bone names. "
                               "Morph-only jaw and eyes can omit joints.");
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
        if(ImGui::CollapsingHeader("Facial morph / 52 semantic channels")) {
            ImGui::TextWrapped("Author default facial channel weights. "
                               "Live jaw, blink and smile controls override "
                               "their corresponding default channels.");
            for(std::size_t i=0;i<kChannelCount;++i) {
                const auto name=channelName(static_cast<Channel>(i));
                const std::string label(name);
                ImGui::PushID(int(i));
                ImGui::SliderFloat(label.c_str(),
                                   &draft.expressionRest[i],0.0f,1.0f,"%.2f");
                editCommit();
                ImGui::PopID();
            }
            if(ImGui::Button("Reset facial defaults")) {
                std::string error;
                if(session.edit([](HumanDNA& dna) {
                    dna.expressionRest.fill(0.0f);
                },&error)) {
                    syncDraft();
                    pendingRebuild=true;
                } else status=error;
            }
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
            ImGui::SliderInt("Hair guides",&draftGuides,4,8192);
            if(ImGui::IsItemDeactivatedAfterEdit()) {
                draft.groom.guideCount=std::uint32_t(draftGuides);
                std::string error;
                if(session.edit([this](HumanDNA& d){d=draft;},&error))
                    pendingRebuild=true;
                else {status=error;syncDraft();}
            }
            ImGui::SliderInt("Hair segments",&draftSegments,2,32);
            if(ImGui::IsItemDeactivatedAfterEdit()) {
                draft.groom.segments=std::uint32_t(draftSegments);
                std::string error;
                if(session.edit([this](HumanDNA& d){d=draft;},&error))
                    pendingRebuild=true;
                else {status=error;syncDraft();}
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
                syncDraft();pendingRebuild=true;
                status="Loaded "+std::string(recipeFile);
            }
        }
        ImGui::Separator();
        ImGui::InputText("Package manifest",manifestFile,sizeof(manifestFile));
        if(ImGui::Button("Import glTF/FBX/OBJ + DNA + morphs")) {
            try {loadExternal(manifestFile);}
            catch(const std::exception& e){status=e.what();}
        }
        ImGui::SameLine();
        if(ImGui::Button("Use procedural reference")) {
            externalMode=false;
            importedManifest.clear();
            pendingRebuild=true;
        }
        if(externalMode && ImGui::CollapsingHeader("Imported facial morph bindings")) {
            ImGui::TextWrapped("Optional closed, unskinned proxy in groom-local "
                               "coordinates. Leave empty for DNA ellipsoid.");
            ImGui::InputText("Scalp collision mesh",
                             scalpProxyName.data(),scalpProxyName.size());
            if(ImGui::IsItemDeactivatedAfterEdit()) {
                auto proposed=importedPackage;
                proposed.scalpColliderMesh=scalpProxyName.data();
                try {
                    proposed.validate();
                    importedPackage=std::move(proposed);
                    pendingRebuild=true;
                    status="Scalp proxy selection updated";
                } catch(const std::exception& e) {
                    status=e.what();
                    std::snprintf(scalpProxyName.data(),scalpProxyName.size(),
                                  "%s",importedPackage.scalpColliderMesh.c_str());
                }
            }
            if(human.groom.hasCollisionMesh())
                ImGui::Text("Scalp: %s MotionWeave deformation",
                            scalpSkinning?"multi-joint CPU skinning":"rigid-node");
            else
                ImGui::TextUnformatted("Scalp: DNA ellipsoid fallback");
            ImGui::TextWrapped("Adjust gain for each imported ARKit channel -> shape route. "
                               "The source target names and vertex data remain unchanged.");
            for(std::size_t i=0;i<importedPackage.routes.size();++i) {
                auto& route=importedPackage.routes[i];
                ImGui::PushID(int(i));
                ImGui::Text("%s -> %s",
                    std::string(channelName(route.channel)).c_str(),
                    route.targetName.c_str());
                ImGui::SliderFloat("Gain",&route.gain,0.0f,4.0f,"%.2f");
                if(ImGui::IsItemDeactivatedAfterEdit())pendingRebuild=true;
                ImGui::PopID();
            }
            if(morphDraft && ImGui::TreeNode("Sparse morph vertex sculpt")) {
                ImGui::InputText("New morph name",newMorphName,sizeof(newMorphName));
                if(ImGui::Button("Add named target")) {
                    if(morphDraft->addTarget(newMorphName)) {
                        morphTargetIndex=int(morphDraft->targets())-1;
                        morphDirty=true;pendingRebuild=true;
                        status="VIVID named corrective added";
                    } else status="VIVID target creation rejected";
                }
                if(morphDraft->targets()>0) {
                ImGui::TextWrapped("Edit one source vertex position delta in "
                    "the chosen target. Source head geometry stays unchanged.");
                if(human.head && !human.head->meshes.empty()) {
                    const char* current=(sculptMeshIndex>=0 &&
                        std::size_t(sculptMeshIndex)<human.head->meshes.size())
                        ? human.head->meshes[std::size_t(sculptMeshIndex)].name.c_str()
                        : "Select face mesh";
                    if(ImGui::BeginCombo("3D pick mesh",current)) {
                        for(std::size_t mesh=0;mesh<human.head->meshes.size();++mesh) {
                            if(ImGui::Selectable(human.head->meshes[mesh].name.c_str(),
                                sculptMeshIndex==int(mesh)))
                                sculptMeshIndex=int(mesh);
                        }
                        ImGui::EndCombo();
                    }
                    if(ImGui::Button(sculptPickArmed
                        ?"Cancel viewport picking":"Pick vertex in 3D viewport"))
                        sculptPickArmed=!sculptPickArmed;
                    ImGui::TextWrapped("Select the source face mesh, click Pick, "
                        "then click its visible triangles outside inspector.");
                }
                const int maxTarget=int(morphDraft->targets())-1;
                ImGui::SliderInt("Morph target",&morphTargetIndex,0,maxTarget);
                morphTargetIndex=std::clamp(morphTargetIndex,0,maxTarget);
                const auto& target=morphDraft->shapes()[std::size_t(morphTargetIndex)];
                ImGui::Text("Target: %s",target.name.c_str());
                ImGui::SliderInt("Route ARKit channel",&routeChannelIndex,0,
                                 int(kChannelCount)-1);
                ImGui::Text("Channel: %s",
                    std::string(channelName(static_cast<Channel>(routeChannelIndex))).c_str());
                if(ImGui::Button("Bind channel to target")) {
                    auto proposed=importedPackage;
                    const auto channel=static_cast<Channel>(routeChannelIndex);
                    const auto match=std::find_if(proposed.routes.begin(),proposed.routes.end(),
                        [&](const ShapeRoute& route) {
                            return route.channel==channel && route.targetName==target.name;
                        });
                    if(match==proposed.routes.end()) {
                        proposed.routes.push_back({channel,target.name,1.0f});
                        try {
                            proposed.validate();
                            importedPackage=std::move(proposed);
                            pendingRebuild=true;
                            status="VIVID facial route added; save manifest to publish";
                        }catch(const std::exception& e){status=e.what();}
                    }
                }
                bool removedThisFrame=false;
                if(ImGui::Button("Delete selected target")) {
                    const std::string removedName=target.name;
                    if(morphDraft->removeTarget(std::size_t(morphTargetIndex))) {
                        importedPackage.routes.erase(
                            std::remove_if(importedPackage.routes.begin(),
                                           importedPackage.routes.end(),
                                [&](const ShapeRoute& route) {
                                    return route.targetName==removedName;
                                }),
                            importedPackage.routes.end());
                        removedThisFrame=true;
                        morphTargetIndex=0;
                        morphDirty=true;pendingRebuild=true;
                        status="VIVID corrective target deleted";
                    }
                }
                if(!removedThisFrame) {
                const int maxVertex=int(morphSourceVertices)-1;
                ImGui::SliderInt("Source vertex",&morphVertexIndex,0,
                                 std::max(0,maxVertex));
                const auto index=std::uint32_t(morphVertexIndex);
                const auto existing=std::find_if(target.deltas.begin(),target.deltas.end(),
                    [&](const MorphDelta& d){return d.vertex==index;});
                if(ImGui::Button("Read vertex delta")) {
                    morphEditOffset=existing==target.deltas.end()
                        ? float3{} : float3{existing->position[0],
                                          existing->position[1],
                                          existing->position[2]};
                }
                ImGui::DragFloat3("Position delta",&morphEditOffset.x,0.001f,-1.0f,1.0f);
                if(ImGui::Button("Apply vertex delta")) {
                    if(morphDraft->setPosition(
                        std::size_t(morphTargetIndex),index,
                        {morphEditOffset.x,morphEditOffset.y,morphEditOffset.z})) {
                        morphDirty=true;
                        pendingRebuild=true;
                        status="VIVID sparse morph vertex edited";
                    }else status="VIVID sparse morph edit rejected";
                }
                ImGui::SliderFloat("Brush radius",&morphBrushRadius,
                                   0.001f,2.0f,"%.3f");
                ImGui::Checkbox("Mirror brush across X=0",&sculptMirrorX);
                if(ImGui::Button("Apply falloff brush")) {
                    if(morphDraft->brush(std::size_t(morphTargetIndex),
                        morphSourcePositions,index,morphBrushRadius,
                        {morphEditOffset.x,morphEditOffset.y,morphEditOffset.z},
                        sculptMirrorX)) {
                        morphDirty=true;
                        pendingRebuild=true;
                        status="VIVID radius-falloff brush stroke applied";
                    }else status="VIVID brush stroke rejected";
                }
                ImGui::SameLine();
                if(ImGui::Button("Undo sculpt") && morphDraft->undo()) {
                    morphDirty=true;pendingRebuild=true;
                }
                ImGui::SameLine();
                if(ImGui::Button("Redo sculpt") && morphDraft->redo()) {
                    morphDirty=true;pendingRebuild=true;
                }
                } // valid selected target for this frame
                ImGui::TextUnformatted(morphDirty?"Morph changes not exported":"Morph source unchanged");
                } // nonempty morph targets
                if(ImGui::Button("Export morph JSON")) {
                    try {
                        if(importedManifest.empty() || morphSourceVertices==0)
                            throw std::runtime_error("VIVID morph export: no imported source");
                        // Keep author-specified source intact; new packages use
                        // an owned sidecar in the same manifest directory.
                        const auto basename=importedPackage.morphPath.empty()
                            ? std::string("vivid-authored-morphs.json")
                            : importedPackage.morphPath;
                        const auto destination=std::filesystem::absolute(
                            std::filesystem::path(importedManifest)).parent_path()/basename;
                        const auto data=morphDraft->toJson(morphSourceVertices).dump(2);
                        const auto staging=std::filesystem::path(
                            destination.string()+".vivid.tmp");
                        {
                            std::ofstream out(staging,std::ios::binary|std::ios::trunc);
                            if(!out)throw std::runtime_error("Morph staging open failed");
                            out.write(data.data(),std::streamsize(data.size()));
                            out.flush();
                            if(!out)throw std::runtime_error("Morph staging write failed");
                        }
                        std::error_code ec;
                        std::filesystem::rename(staging,destination,ec);
                        if(ec)throw std::runtime_error(
                            "Morph atomic replacement failed: "+ec.message());
                        importedPackage.morphPath=basename;
                        morphDirty=false;
                        status="VIVID morph sidecar saved; save manifest to publish path";
                    }catch(const std::exception& e){status=e.what();}
                }
                ImGui::TreePop();
            }
            if(ImGui::Button("Save imported manifest")) {
                try {
                    importedPackage.validate();
                    const auto data=importedPackage.toJson().dump(2);
                    const std::filesystem::path destination(importedManifest);
                    const auto staging=std::filesystem::path(importedManifest+".vivid.tmp");
                    {
                        std::ofstream out(staging,std::ios::binary|std::ios::trunc);
                        if(!out)throw std::runtime_error("Manifest staging open failed");
                        out.write(data.data(),std::streamsize(data.size()));
                        out.flush();
                        if(!out)throw std::runtime_error("Manifest staging write failed");
                    }
                    std::error_code ec;
                    std::filesystem::rename(staging,destination,ec);
                    if(ec)throw std::runtime_error("Manifest atomic replace failed: "+ec.message());
                    status="Saved imported human manifest: "+importedManifest;
                }catch(const std::exception& e){status=e.what();}
            }
        }
        ImGui::TextWrapped("%s",externalMode ?
            "Imported head: edit material DNA, morph expressions and groom. "
            "Mesh proportions remain authored in the original glTF/FBX asset." :
            "Procedural head: all geometric proportions are editable. "
            "For realistic scanned characters import a glTF/FBX package.");

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
        // The DNA 52-channel neutral pose is applied first. Interactive
        // demonstration controls below override their named channels.
        for(std::size_t i=0;i<kChannelCount;++i)
            controller.set(Layer::Tracking,static_cast<Channel>(i),
                           session.dna().expressionRest[i]);
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
        // Picking uses the actual camera ray and rendered node-world pose,
        // never a guessed nearest projected vertex behind the head.
        if(externalMode && sculptPickArmed && morphDraft && human.head &&
           ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
           !ImGui::GetIO().WantCaptureMouse &&
           sculptMeshIndex>=0 &&
           std::size_t(sculptMeshIndex)<human.head->meshes.size()) {
            const auto mouse=ImGui::GetMousePos();
            const float width=float(dev.framebufferWidth());
            const float height=float(dev.framebufferHeight());
            float3 origin{},direction{};
            if(camera.screenRay(mouse.x,mouse.y,width,height,origin,direction)) {
                const auto pick=pickSculptVertex(*human.head,
                    std::size_t(sculptMeshIndex),facePose,
                    origin,direction,morphSourceVertices);
                if(pick.hit) {
                    morphVertexIndex=int(pick.vertex);
                    sculptPickArmed=false;
                    status="Picked face source vertex "+
                        std::to_string(morphVertexIndex);
                }else status="No source face triangle under cursor";
            }
        }
        if(externalMode && morphSourceVertices && human.head &&
           morphVertexIndex>=0 &&
           std::size_t(morphVertexIndex)<morphSourceVertices) {
            // Visible marker is screen-space only; no extra model/GPU draw.
            float2 screen{};
            const auto p=human.head->vertices[std::size_t(morphVertexIndex)].pos;
            bool found=false;
            for(std::size_t i=0;i<human.head->nodes.size();++i) {
                if(sculptMeshIndex>=0 &&
                   human.head->nodes[i].mesh==sculptMeshIndex) {
                    found=camera.worldToScreen(facePose[i].transformPoint(p),
                         float(dev.framebufferWidth()),
                         float(dev.framebufferHeight()),screen);
                    break;
                }
            }
            if(found && std::isfinite(screen.x) && std::isfinite(screen.y)) {
                ImGui::GetForegroundDrawList()->AddCircle(
                    ImVec2(screen.x,screen.y),7.0f,IM_COL32(255,190,50,255),20,2.0f);
            }
        }
        if(inspectGroom && human.hair) {
            if(scalpPoseNode>=0 && !scalpBindPositions.empty() &&
               std::size_t(scalpPoseNode)<facePose.size()) {
                const auto frameHead=rigidHeadMatrix(
                    facePose[std::size_t(human.rig.head)]);
                bool validPose=false;
                if(scalpSkinning) {
                    // MotionWeave owns all node matrices; the VIVID adapter
                    // only performs presentation CPU skinning of scalp proxy.
                    validPose=scalpSkinning->evaluate(
                        facePose,frameHead,scalpAnimatedPositions,
                        scalpSkinScratch);
                } else {
                    const auto delta=frameHead.inverse() *
                        facePose[std::size_t(scalpPoseNode)] *
                        scalpBindNodeInverse * scalpBindHeadRigid;
                    validPose=true;
                    for(float value:delta.m)
                        validPose=validPose&&std::isfinite(value);
                    if(validPose) {
                        for(std::size_t i=0;i<scalpBindPositions.size();++i) {
                            auto p=delta.transformPoint(scalpBindPositions[i]);
                            if(!std::isfinite(p.x)||!std::isfinite(p.y)||
                               !std::isfinite(p.z)) {
                                validPose=false;
                                break;
                            }
                            scalpAnimatedPositions[i]=p;
                        }
                    }
                }
                if(!validPose ||
                   !human.groom.refitCollisionMesh(scalpAnimatedPositions))
                    status="VIVID Creator: invalid scalp pose, last-good BVH retained";
            }
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
        // Shared render/collision frame: head-local procedural groom,
        // no extra head nonuniform scale applied to its card geometry.
        const mat4 headRigid=rigidHeadMatrix(poseHead);
        if(human.hair) {
            ModelDraw hair;
            hair.model=human.hair.get();
            hair.world=headRigid;
            renderer.drawModel(hair);
        }
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
        }else if(std::strcmp(argv[i],"--preset")==0 && i+1<argc) {
            const std::string preset=argv[++i];
            std::string error;
            if(!application->session.preset(preset,&error)) {
                std::fprintf(stderr,"VIVID Creator preset: %s\n",error.c_str());
                return 2;
            }
            application->syncDraft();
        }else if(std::strcmp(argv[i],"--asset")==0 && i+1<argc) {
            const auto& path=argv[++i];
            if(std::strlen(path)>=sizeof(application->manifestFile)) {
                std::fprintf(stderr,"VIVID Creator: package path too long\n");
                return 2;
            }
            std::strncpy(application->manifestFile,path,
                         sizeof(application->manifestFile)-1);
            application->commandLineAsset=true;
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
