// Standalone VIVID Creator: genre-neutral interactive DNA editor and preview.
// Uses the existing human asset/procedural groom/material pipeline and the
// engine Renderer3D/ImGui RHI. No second animation system is introduced.
// Startup: aiverse_vivid_creator [--dna character.vivid.json]
#include "engine/platform/Platform.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/character/vivid/HumanCreatorSession.h"
#include "engine/character/vivid/HumanPreview.h"
#include "engine/character/vivid/Face.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

using namespace aiverse;
namespace {

struct Creator {
    vivid::HumanCreatorSession session;
    vivid::HumanPreviewAsset asset;
    render::Renderer3D renderer;
    render::ImGuiLayer gui;
    render::RTSCamera camera;
    render::Mesh floor;
    gfx::Device* device=nullptr;
    char filename[512]="vivid-human.json";
    bool initialized=false;
    bool needRebuild=false;
    bool changePending=false;
    bool dragging=false;
    float mouseX=0,mouseY=0;
    std::string error;
    vivid::GroomQuality groomQuality=vivid::GroomQuality::Medium;
    vivid::FaceController face{0.0f};
    float jawOpen=0.0f,blink=0.0f,smile=0.0f;

    void rebuild() {
        if(!device)return;
        // Renderer3D and source geometry can have pending commands on the
        // ordered device queue. Never destroy model buffers in-flight.
        if(!device->waitForResourceIdle()) {
            error="GPU is not idle; preserving last-good human preview";
            return;
        }
        vivid::HumanPreviewAsset candidate;
        try {
            candidate=vivid::generateHumanPreview(session.dna(),groomQuality);
            if(!candidate.head || !candidate.hair)
                throw std::runtime_error("VIVID creator: missing head or groom");
            if(!render::uploadModel(*device,*candidate.head,true) ||
               !render::uploadModel(*device,*candidate.hair,true))
                throw std::runtime_error("VIVID creator: head or hair upload failed");
        } catch(const std::exception& ex) {
            if(candidate.head)render::releaseModel(*device,*candidate.head);
            if(candidate.hair)render::releaseModel(*device,*candidate.hair);
            error=ex.what();
            return;
        }
        if(asset.head)render::releaseModel(*device,*asset.head);
        if(asset.hair)render::releaseModel(*device,*asset.hair);
        asset=std::move(candidate);
        error.clear();
        needRebuild=false;
    }
    void initialize(gfx::Device& d) {
        device=&d;
        if(!renderer.init(d))throw std::runtime_error("VIVID Creator Renderer3D init failed");
        initialized=true;
        if(!gui.init(d,"engine/fonts/ui.ttf",16))
            throw std::runtime_error("VIVID Creator ImGui RHI init failed");
        renderer.setShadowQuality(render::ShadowQuality::Medium);
        renderer.setMaterialQuality(render::MaterialQuality::High);
        renderer.setModelLodEnabled(false);
        renderer.setTraceEnabled(false);
        renderer.lighting.fogDensity=0;
        camera.target={0,0,0};
        camera.distance=5.0f;
        camera.yaw=0.13f;
        camera.pitch=0.08f;
        camera.minDistance=1.5f;
        floor=render::MeshBuilder().box({0,-1.15f,0},{8,0.13f,8}).upload(d);
        needRebuild=true;
        rebuild();
    }
    void edit(const std::function<void(vivid::HumanDNA&)>& modifier) {
        if(!session.edit(modifier,&error))return;
        needRebuild=true;
    }
    void floatField(const char* label,float value,float minimum,float maximum,
                    const std::function<void(vivid::HumanDNA&,float)>& write) {
        if(ImGui::SliderFloat(label,&value,minimum,maximum,"%.3f")) {
            edit([&,value](vivid::HumanDNA& dna){write(dna,value);});
        }
    }
    void drawControls() {
        ImGui::SetNextWindowPos({10,10},ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({400,800},ImGuiCond_FirstUseEver);
        ImGui::Begin("VIVID Creator");
        ImGui::Text("Version %u | Revision %zu | %s",session.dna().version,
                    session.revision(),session.dirty()?"Modified":"Saved");
        if(ImGui::Button("Undo")){if(session.undo())needRebuild=true;}
        ImGui::SameLine();
        if(ImGui::Button("Redo")){if(session.redo())needRebuild=true;}
        ImGui::SameLine();
        if(ImGui::Button("Reset neutral")) {
            if(session.preset("neutral",&error))needRebuild=true;
        }
        const char* presets[]={"neutral","warm","cool","stylized"};
        for(const char* id:presets) {
            if(ImGui::Button(id)) {
                if(session.preset(id,&error))needRebuild=true;
            }
            if(std::strcmp(id,"stylized")!=0)ImGui::SameLine();
        }
        ImGui::InputText("DNA file",filename,sizeof filename);
        if(ImGui::Button("Load DNA")) {
            if(session.loadFile(filename,&error))needRebuild=true;
        }
        ImGui::SameLine();
        if(ImGui::Button("Save DNA"))session.saveFile(filename,&error);
        ImGui::Separator();
        const auto& d=session.dna();
        if(ImGui::CollapsingHeader("Anatomy",ImGuiTreeNodeFlags_DefaultOpen)) {
            floatField("Head scale",d.anatomy.headScale,0.5f,1.6f,
                [](auto& d,float v){d.anatomy.headScale=v;});
            floatField("Head width",d.anatomy.headWidth,0.5f,1.6f,
                [](auto& d,float v){d.anatomy.headWidth=v;});
            floatField("Jaw width",d.anatomy.jawWidth,0.5f,1.6f,
                [](auto& d,float v){d.anatomy.jawWidth=v;});
            floatField("Cheek fullness",d.anatomy.cheekFullness,0,1,
                [](auto& d,float v){d.anatomy.cheekFullness=v;});
            floatField("Nose width",d.anatomy.noseWidth,0,1,
                [](auto& d,float v){d.anatomy.noseWidth=v;});
            floatField("Eye spacing",d.anatomy.eyeSpacing,0,1,
                [](auto& d,float v){d.anatomy.eyeSpacing=v;});
            floatField("Eye scale",d.anatomy.eyeScale,0.6f,1.6f,
                [](auto& d,float v){d.anatomy.eyeScale=v;});
            floatField("Mouth width",d.anatomy.mouthWidth,0,1,
                [](auto& d,float v){d.anatomy.mouthWidth=v;});
        }
        if(ImGui::CollapsingHeader("Skin",ImGuiTreeNodeFlags_DefaultOpen)) {
            floatField("Melanin",d.skin.melanin,0,1,
                [](auto& d,float v){d.skin.melanin=v;});
            floatField("Hemoglobin",d.skin.hemoglobin,0,1,
                [](auto& d,float v){d.skin.hemoglobin=v;});
            floatField("Scattering (mm)",d.skin.scatteringMm,0,6,
                [](auto& d,float v){d.skin.scatteringMm=v;});
            floatField("Roughness",d.skin.roughness,0.05f,1,
                [](auto& d,float v){d.skin.roughness=v;});
            floatField("Pores",d.skin.poreScale,0,1,
                [](auto& d,float v){d.skin.poreScale=v;});
            floatField("Wetness",d.skin.wetness,0,1,
                [](auto& d,float v){d.skin.wetness=v;});
            float rgb[3]={d.skin.melaninColor[0],d.skin.melaninColor[1],
                           d.skin.melaninColor[2]};
            if(ImGui::ColorEdit3("Skin pigment",rgb))
                edit([&](auto& dna){dna.skin.melaninColor={rgb[0],rgb[1],rgb[2]};});
        }
        if(ImGui::CollapsingHeader("Eyes",ImGuiTreeNodeFlags_DefaultOpen)) {
            floatField("Pupil",d.eyes.pupil,0.05f,0.95f,
                [](auto& d,float v){d.eyes.pupil=v;});
            floatField("Limbal ring",d.eyes.limbus,0,1,
                [](auto& d,float v){d.eyes.limbus=v;});
            floatField("Tear film",d.eyes.tearFilm,0,1,
                [](auto& d,float v){d.eyes.tearFilm=v;});
            floatField("Cornea IOR",d.eyes.corneaIor,1.30f,1.45f,
                [](auto& d,float v){d.eyes.corneaIor=v;});
            float rgb[3]={d.eyes.irisColor[0],d.eyes.irisColor[1],
                          d.eyes.irisColor[2]};
            if(ImGui::ColorEdit3("Iris color",rgb))
                edit([&](auto& dna){dna.eyes.irisColor={rgb[0],rgb[1],rgb[2]};});
        }
        if(ImGui::CollapsingHeader("Groom",ImGuiTreeNodeFlags_DefaultOpen)) {
            floatField("Hair length",d.groom.lengthMeters,0.01f,0.7f,
                [](auto& d,float v){d.groom.lengthMeters=v;});
            floatField("Hair density",d.groom.density,0,1,
                [](auto& d,float v){d.groom.density=v;});
            floatField("Hair curl",d.groom.curl,0,1,
                [](auto& d,float v){d.groom.curl=v;});
            floatField("Hair stiffness",d.groom.stiffness,0,1,
                [](auto& d,float v){d.groom.stiffness=v;});
            floatField("Hair wind",d.groom.wind,0,1,
                [](auto& d,float v){d.groom.wind=v;});
            float rgb[3]={d.groom.color[0],d.groom.color[1],d.groom.color[2]};
            if(ImGui::ColorEdit3("Hair color",rgb))
                edit([&](auto& dna){dna.groom.color={rgb[0],rgb[1],rgb[2]};});
            const char* quality[]={"Low","Medium","High"};
            int selected=int(groomQuality);
            if(ImGui::Combo("Groom LOD",&selected,quality,3)) {
                groomQuality=vivid::GroomQuality(selected);
                needRebuild=true;
            }
        }
        if(ImGui::CollapsingHeader("Expression preview")) {
            ImGui::SliderFloat("Jaw open",&jawOpen,0,1);
            ImGui::SliderFloat("Blink",&blink,0,1);
            ImGui::SliderFloat("Smile",&smile,0,1);
        }
        if(!error.empty())ImGui::TextColored({1,0.35f,0.32f,1},
                                             "%s",error.c_str());
        if(asset.head)ImGui::Text("Vertices: %zu | Materials: %zu",
                                 asset.head->vertices.size(),
                                 asset.head->materials.size());
        ImGui::End();
    }
    void frame(gfx::Device& dev,double dt) {
        if(needRebuild)rebuild();
        gui.newFrame(dt);
        drawControls();
        // Live geometry/material reload on the next platform frame prevents
        // destructive edits while render passes are actively encoding.
        renderer.beginScene(camera,dev.framebufferWidth(),dev.framebufferHeight());
        if(floor.vb.valid())
            renderer.drawMesh(floor,{}, {0.28f,0.31f,0.34f,1});
        if(asset.head && asset.head->vb.valid()) {
            render::ModelDraw draw;
            draw.model=asset.head.get();
            draw.world=render::mat4::translate({0,0,0});
            renderer.drawModel(draw);
        }
        if(asset.hair && asset.hair->vb.valid()) {
            render::ModelDraw draw;
            draw.model=asset.hair.get();
            renderer.drawModel(draw);
        }
        gfx::PassDesc pass{};
        pass.clearColor[0]=0.09f;pass.clearColor[1]=0.11f;
        pass.clearColor[2]=0.15f;
        renderer.render(pass);
        gui.render();
        dev.endPass();
        if(!renderer.modelError().empty())
            error=renderer.modelError();
    }
    void event(const platform::Event& e) {
        if(gui.handleEvent(e))return;
        using platform::EventType;
        if(e.type==EventType::MouseDown){dragging=true;mouseX=e.x;mouseY=e.y;}
        else if(e.type==EventType::MouseUp)dragging=false;
        else if(e.type==EventType::MouseMove && dragging) {
            camera.yaw-=0.01f*(e.x-mouseX);
            camera.pitch=std::clamp(camera.pitch+0.01f*(e.y-mouseY),-1.2f,1.2f);
            mouseX=e.x;mouseY=e.y;
        } else if(e.type==EventType::MouseWheel)
            camera.distance=std::clamp(camera.distance*
                (e.wheelY>0?0.9f:1.1f),1.7f,20.0f);
    }
    void shutdown() {
        if(!device)return;
        if(!device->waitForResourceIdle()) {
            std::fprintf(stderr,"VIVID Creator GPU barrier failed; retaining GPU resources\n");
            return;
        }
        if(asset.head)render::releaseModel(*device,*asset.head);
        if(asset.hair)render::releaseModel(*device,*asset.hair);
        if(floor.vb.valid())device->destroyBuffer(floor.vb);
        if(floor.ib.valid())device->destroyBuffer(floor.ib);
        gui.shutdown();
        if(initialized)renderer.shutdown();
        device=nullptr;
    }
};

} // namespace

int aiverseMain(int argc,char** argv) {
    auto app=std::make_shared<Creator>();
    for(int i=1;i<argc;++i)
        if(std::strcmp(argv[i],"--dna")==0 && i+1<argc) {
            std::snprintf(app->filename,sizeof(app->filename),"%s",argv[++i]);
            std::string error;
            if(!app->session.loadFile(app->filename,&error))
                std::fprintf(stderr,"VIVID Creator input: %s\n",error.c_str());
        }
    platform::AppConfig config;
    config.title="AIVERSE VIVID Creator";
    config.width=1440;config.height=900;
    platform::AppCallbacks cb;
    cb.init=[app](gfx::Device& device){
        try{app->initialize(device);}catch(const std::exception& e){
            app->error=e.what();std::fprintf(stderr,"VIVID Creator init: %s\n",e.what());
        }
    };
    cb.frame=[app](gfx::Device& device,double dt){
        try{app->frame(device,dt);}catch(const std::exception& e){
            app->error=e.what();std::fprintf(stderr,"VIVID Creator frame: %s\n",e.what());
            platform::requestQuit();
        }
    };
    cb.event=[app](const platform::Event& e){app->event(e);};
    cb.shutdown=[app]{app->shutdown();};
    return platform::run(config,std::move(cb));
}
