#include "engine/character/vivid/HumanCreatorSession.h"
#include "engine/character/vivid/MorphAuthoring.h"
#include "engine/character/vivid/HumanImport.h"
#include <filesystem>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse::vivid;
namespace {
void req(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
void test() {
    HumanCreatorSession session;
    req(session.dna().id=="vivid_default" && !session.dirty(),
        "creator default failed");
    const auto start=session.revision();
    req(session.edit([](HumanDNA& d){d.skin.wetness=0.7f;}),
        "valid DNA edit rejected");
    req(session.revision()==start+1 && session.dirty() &&
        session.dna().skin.wetness==0.7f,"DNA transaction not recorded");
    std::string error;
    req(!session.edit([](HumanDNA& d){d.groom.guideCount=999999;},&error) &&
        !error.empty() && session.dna().groom.guideCount==512,
        "invalid creator edit was not rolled back");
    req(session.undo() && session.dna().skin.wetness==0 &&
        session.canRedo(),"DNA undo failed");
    req(session.redo() && session.dna().skin.wetness==0.7f,
        "DNA redo failed");
    req(session.preset("stylized") &&
        session.dna().anatomy.eyeScale>1.1f,"creator preset not applied");
    req(session.undo() && session.dna().skin.wetness==0.7f,
        "preset undo failed");
    const auto file=(std::filesystem::temp_directory_path()/
        "aiverse-vivid-human-creator-test.json").string();
    req(session.saveFile(file,&error) && !session.dirty(),
        "creator save failed");
    HumanCreatorSession second;
    req(second.loadFile(file,&error) && !second.dirty() &&
        second.dna().toJson()==session.dna().toJson(),
        "creator JSON file load mismatch");
    req(!second.loadJson(aiverse::Json::parse(R"({"version":100})"),&error) &&
        second.dna().toJson()==session.dna().toJson(),
        "creator invalid DNA replaced last-good profile");
    {
        MorphTargetInput shape;
        shape.name="mouthSmileLeft";
        MorphDelta initial;
        initial.vertex=1;
        initial.position={0.1f,0,0};
        shape.deltas.push_back(initial);
        const auto csr=SparseMorphLibrary::compile(4,{shape});
        auto sculpt=MorphAuthoring::fromCompiled(csr);
        req(sculpt.targets()==1 && sculpt.vertices()==4,
            "sculpt target import failed");
        req(sculpt.setPosition(0,1,{0.25f,0.1f,0}) && sculpt.canUndo(),
            "sparse morph edit failed");
        req(sculpt.undo() && sculpt.redo(),
            "sparse morph undo/redo failed");
        const auto exported=sculpt.toJson(4);
        const auto compiled=parseHumanMorphs(exported,4);
        req(compiled.targets()==1 && compiled.deltas()==1,
            "sparse morph JSON roundtrip lost shape");
        std::size_t targetId=0;
        req(compiled.findTarget("mouthSmileLeft",targetId) && targetId==0,
            "sparse sculpt export lost target name");
        req(sculpt.addTarget("browUp") && sculpt.targets()==2,
            "named corrective creation failed");
        req(!sculpt.addTarget("browUp"),
            "duplicate corrective name accepted");
        req(sculpt.setPosition(1,2,{0,0.2f,0}) &&
            sculpt.removeTarget(0) && sculpt.targets()==1,
            "corrective deletion or remapping failed");
        req(sculpt.undo() && sculpt.targets()==2 &&
            sculpt.redo() && sculpt.targets()==1,
            "corrective target deletion undo/redo failed");
        const std::array<MorphVec3,4> positions{{{0,0,0},{0.1f,0,0},
                                                  {0.3f,0,0},{2,0,0}}};
        req(sculpt.brush(0,positions,0,0.35f,{0.05f,0,0}) &&
            sculpt.canUndo(),
            "falloff morph brush rejected");
        const auto brushed=sculpt.compile();
        req(brushed.deltas()>=2,"brush did not affect adjacent source vertices");
        req(!sculpt.brush(0,positions,0,-1.0f,{0.1f,0,0}) &&
            !sculpt.brush(0,positions,0,0.5f,{50.0f,0,0}),
            "invalid brush parameters accepted");
        req(sculpt.undo() && sculpt.redo(),
            "brush stroke history failed");
        req(!sculpt.setPosition(0,4,{1,0,0}) &&
            !sculpt.setPosition(0,1,{100,0,0}),
            "out-of-range sculpt accepted");
        bool rejected=false;
        try { (void)sculpt.toJson(1); }
        catch(const std::invalid_argument&) { rejected=true; }
        req(rejected,"sculpt exported generated vertex outside original topology");
    }
    std::filesystem::remove(file);
}
}
int main(){try{test();std::cout<<"VIVID creator transactions PASS\n";}
catch(const std::exception& e){std::cerr<<"VIVID creator FAIL: "<<e.what()<<"\n";return 1;}return 0;}
