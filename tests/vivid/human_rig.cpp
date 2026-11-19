#include "engine/character/vivid/HumanRig.h"
#include "engine/character/vivid/RigAuthoring.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace aiverse;
using namespace aiverse::vivid;
namespace {
void req(bool v,const char* err){if(!v)throw std::runtime_error(err);}
void check() {
    render::Model model;
    auto node=[&](const char* name,int parent) {
        render::Node n;n.name=name;n.parent=parent;model.nodes.push_back(n);
    };
    node("root",-1);node("head",0);node("jaw",1);node("eye.L",1);node("eye.R",1);
    const auto binding=HumanRigBinding::compile(model,HumanDNA{}.rig);
    req(binding.valid() && binding.hasFacialJoints(),"human joint roles missing");
    std::vector<render::mat4> motion(5), result(5);
    for(auto& m:motion)m=render::mat4::translate({2,3,4});
    FaceFrame face{};
    face.values[static_cast<std::size_t>(Channel::JawOpen)]=1.0f;
    face.values[static_cast<std::size_t>(Channel::EyeLookUpLeft)]=0.5f;
    req(binding.compose(motion,result,face),"valid facial joint compose refused");
    req(result[0].m[12]==motion[0].m[12] && result[1].m[13]==motion[1].m[13],
        "MotionWeave root/head pose modified");
    req(std::fabs(result[2].at(2,1)-motion[2].at(2,1))>0.01f,
        "jaw rotation missing");
    req(std::fabs(result[3].at(1,2)-motion[3].at(1,2))>0.01f,
        "eye gaze rotation missing");
    FaceFrame invalid=face;invalid.values[0]=-1.0f;
    auto before=result;
    req(!binding.compose(motion,result,invalid) && result[2].at(2,1)==before[2].at(2,1),
        "invalid facial values partially changed output");
    req(!binding.compose(motion,motion,face),"face overlay mutated MotionWeave source");
    model.nodes[2].parent=0;
    bool thrown=false;
    try{(void)HumanRigBinding::compile(model,HumanDNA{}.rig);}
    catch(const std::invalid_argument&){thrown=true;}
    req(thrown,"detached jaw accepted");
    model.nodes[2].parent=1;
    {
        RigAuthoring edits;
        req(edits.setTranslation("head",{0,0.10f,0}),
            "rig correction edit rejected");
        std::vector<render::mat4> corrected(motion.size());
        req(edits.apply(model,motion,corrected),
            "rig correction overlay could not compose");
        req(std::fabs(corrected[1].at(1,3)-(motion[1].at(1,3)+0.10f))<1.e-5f &&
            std::fabs(corrected[2].at(1,3)-(motion[2].at(1,3)+0.10f))<1.e-5f &&
            corrected[0].at(1,3)==motion[0].at(1,3),
            "rig correction did not propagate to descendants");
        auto loaded=RigAuthoring::fromJson(edits.toJson());
        std::vector<render::mat4> roundtrip(motion.size());
        req(loaded.apply(model,motion,roundtrip) &&
            std::fabs(roundtrip[2].at(1,3)-corrected[2].at(1,3))<1.e-5f,
            "rig JSON offset roundtrip failed");
        req(loaded.undo()==false &&
            loaded.setTranslation("head",{0,0.2f,0}) &&
            loaded.undo() && loaded.redo(),
            "rig correction undo redo failed");
        req(!edits.apply(model,motion,motion),
            "rig correction modified immutable MotionWeave input");
    }
    model.nodes.resize(2); // missing jaw and eyes must be legal for morph-only rigs
    const auto fallback=HumanRigBinding::compile(model,HumanDNA{}.rig);
    req(fallback.valid()&&!fallback.hasFacialJoints(),"morph-only rig rejected");
}
}
int main(){try{check();std::cout<<"VIVID human rig PASS\n";}
catch(const std::exception& e){std::cerr<<"VIVID human rig FAIL: "<<e.what()<<"\n";return 1;}return 0;}
