#include "engine/character/vivid/HumanRig.h"
#include "engine/character/vivid/RigAuthoring.h"
#include "engine/character/vivid/BindPoseRebind.h"
#include "engine/character/vivid/SkinWeightAuthoring.h"
#include "engine/character/vivid/SkinnedWeightPick.h"
#include <cmath>
#include <iostream>
#include <limits>
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
    {
        auto invalidPose=motion;
        invalidPose[1].at(0,3)=std::numeric_limits<float>::quiet_NaN();
        const auto last=result;
        req(!binding.compose(invalidPose,result,face),
            "VIVID facial rig accepted invalid MotionWeave source matrix");
        req(result[1].at(0,3)==last[1].at(0,3) &&
            result[2].at(2,1)==last[2].at(2,1),
            "invalid MotionWeave source partially changed facial render pose");
    }
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
    {
        render::Model rig;
        render::Node root;
        root.name="root";
        rig.nodes.push_back(root);
        render::Node head;
        head.name="head";
        head.parent=0;
        head.mesh=0;
        head.skin=0;
        rig.nodes.push_back(head);
        render::Skin skin;
        skin.joints={1};
        skin.inverseBind={render::mat4{}};
        // Imported skin may include a non-identity mesh/root binding frame.
        // Rebinding must retain it, not blindly set inverse(newJoint).
        skin.inverseBind[0]=render::mat4::translate({0,0,-0.5f});
        rig.skins.push_back(skin);
        rig.vertices.resize(1);
        rig.vertices[0].pos={1,0,0};
        rig.vertices[0].weights[0]=255;
        rig.indices={0,0,0};
        render::MeshDef mesh;
        render::Primitive primitive;
        primitive.firstIndex=0;
        primitive.indexCount=3;
        primitive.firstVertex=0;
        primitive.vertexCount=1;
        primitive.jointPalette={0};
        mesh.primitives.push_back(primitive);
        rig.meshes.push_back(mesh);
        RigAuthoring authoring;
        req(authoring.setTranslation("head",{0,0.25f,0}),
            "bind-pose translation authoring rejected");
        const auto changed=BindPoseRebind::apply(rig,authoring);
        req(changed.editedNodes==1 && changed.reboundSkins==1 &&
            changed.influencedVertices==1,
            "skin binding stage did not validate influenced vertex");
        req(std::fabs(rig.nodes[1].t.y-0.25f)<1.e-6f &&
            std::fabs(rig.skins[0].inverseBind[0].at(1,3)+0.25f)<1.e-6f &&
            std::fabs(rig.skins[0].inverseBind[0].at(2,3)+0.5f)<1.e-6f,
            "skin inverse bind was not recomputed from edited skeleton bind");
        const auto neutral=(rig.bindWorld[1]*rig.skins[0].inverseBind[0])
            .transformPoint(rig.vertices[0].pos);
        req(std::fabs(neutral.x-1.0f)<1.e-6f &&
            std::fabs(neutral.y)<1.e-6f &&
            std::fabs(neutral.z+0.5f)<1.e-6f,
            "skin did not preserve neutral mesh after bind-pose change");
        const auto last=rig.nodes[1].t.y;
        rig.meshes[0].primitives[0].jointPalette={};
        bool rejected=false;
        try {(void)BindPoseRebind::apply(rig,authoring);}
        catch(const std::invalid_argument&){rejected=true;}
        req(rejected && rig.nodes[1].t.y==last,
            "invalid skin palette partially modified skeleton");
    }
    {
        render::Model weighted;
        render::Node shoulder;
        shoulder.name="shoulder";
        weighted.nodes.push_back(shoulder);
        render::Node elbow;
        elbow.name="elbow";elbow.parent=0;
        weighted.nodes.push_back(elbow);
        render::Node surface;
        surface.name="surface";surface.mesh=0;surface.skin=0;
        weighted.nodes.push_back(surface);
        render::Skin skin;
        skin.joints={0,1};skin.inverseBind.resize(2);
        weighted.skins.push_back(skin);
        weighted.vertices.resize(2);
        weighted.vertices[0].joints[0]=0;
        weighted.vertices[0].weights[0]=255;
        weighted.vertices[1].joints[0]=0;
        weighted.vertices[1].weights[0]=255;
        weighted.indices={0,1,0};
        render::MeshDef mesh;
        render::Primitive primitive;
        primitive.firstVertex=0;primitive.vertexCount=2;
        primitive.firstIndex=0;primitive.indexCount=3;
        primitive.jointPalette={0,1};
        mesh.primitives.push_back(primitive);
        weighted.meshes.push_back(mesh);
        SkinWeightAuthoring weights;
        req(weights.set(0,"elbow",0.25f),"skin weights edit rejected");
        const auto result=weights.apply(weighted);
        req(result.verticesChanged==1 &&
            weighted.vertices[0].weights[0]+weighted.vertices[0].weights[1]==255 &&
            weighted.vertices[0].joints[0]==0 &&
            weighted.vertices[0].joints[1]==1,
            "skin weights U8 normalization/joint slot order failed");
        const auto json=weights.toJson();
        const auto restored=SkinWeightAuthoring::fromJson(json);
        req(restored.edits().size()==1 &&
            std::fabs(restored.edits().begin()->second.weight-0.25f)<1.e-5f,
            "skin weights sidecar JSON roundtrip failed");
        auto before=weighted.vertices[0];
        weighted.meshes[0].primitives[0].jointPalette={0};
        bool rejected=false;
        try {(void)weights.apply(weighted);}
        catch(const std::invalid_argument&){rejected=true;}
        req(rejected && weighted.vertices[0].weights[0]==before.weights[0] &&
            weighted.vertices[0].joints[1]==before.joints[1],
            "invalid weight palette partially changed prepared mesh");
        // Two primitives can share a global vertex only if the skin palette
        // resolves every byte-coded slot identically.
        weighted.meshes[0].primitives[0].jointPalette={0,1};
        render::Primitive seam=weighted.meshes[0].primitives[0];
        seam.jointPalette={1,0};
        weighted.meshes[0].primitives.push_back(seam);
        rejected=false;
        try {(void)weights.apply(weighted);}
        catch(const std::invalid_argument&){rejected=true;}
        req(rejected && weighted.vertices[0].weights[0]==before.weights[0],
            "shared seam with conflicting palettes was silently overwritten");
    }
    {
        render::Model painted;
        painted.nodes.resize(3);
        painted.nodes[0].name="root";
        painted.nodes[1].name="jaw";painted.nodes[1].parent=0;
        painted.nodes[2].name="mesh";painted.nodes[2].skin=0;
        painted.nodes[2].mesh=0;
        render::Skin skin;
        skin.joints={0,1};
        skin.inverseBind.resize(2);
        painted.skins.push_back(skin);
        painted.vertices.resize(3);
        painted.vertices[0].pos={-0.1f,0,0};
        painted.vertices[1].pos={0.1f,0,0};
        painted.vertices[2].pos={2.0f,0,0};
        for(auto& v:painted.vertices) {
            v.weights[0]=200;v.joints[0]=0;
            v.weights[1]=55;v.joints[1]=1;
        }
        painted.indices={0,1,2};
        render::Primitive primitive;
        primitive.firstIndex=0;primitive.indexCount=3;
        primitive.firstVertex=0;primitive.vertexCount=3;
        primitive.jointPalette={0,1};
        render::MeshDef mesh;mesh.primitives.push_back(primitive);
        painted.meshes.push_back(mesh);
        SkinWeightAuthoring authoring;
        req(authoring.brush(painted,0,0,"jaw",1.f,0.16f,0.8f,true),
            "mirrored skin brush rejected valid mesh");
        req(authoring.edits().count(0)==1 &&
            authoring.edits().count(1)==1 &&
            authoring.edits().count(2)==0 &&
            authoring.edits().at(0).weight>0.55f &&
            authoring.edits().at(1).weight>0.55f,
            "mirrored skin brush failed smooth falloff/radius isolation");
        req(authoring.canUndo() && authoring.undo() &&
            authoring.edits().empty() &&
            authoring.redo() && authoring.edits().size()==2,
            "weight brush undo redo did not preserve the full stroke");
        auto damaged=painted;
        damaged.meshes[0].primitives[0].jointPalette={0};
        const auto prior=authoring.toJson();
        req(!authoring.brush(damaged,0,0,"jaw",0.5f,0.16f,1.f,true) &&
            authoring.toJson()==prior,
            "invalid skin brush modified authoring state");
        auto updated=painted;
        const auto changes=authoring.apply(updated);
        req(changes.verticesChanged==2 &&
            updated.vertices[0].weights[0]+updated.vertices[0].weights[1]==255 &&
            updated.vertices[1].weights[0]+updated.vertices[1].weights[1]==255,
            "skin weight brush failed to publish normalized palettes");
    }
    {
        render::Model posed;
        posed.nodes.resize(3);
        posed.nodes[0].name="root";
        posed.nodes[1].name="joint";posed.nodes[1].parent=0;
        posed.nodes[2].name="skinMesh";posed.nodes[2].mesh=0;
        posed.nodes[2].skin=0;
        render::Skin skin;
        skin.joints={1};
        skin.inverseBind={render::mat4{}};
        posed.skins.push_back(skin);
        posed.vertices.resize(3);
        posed.vertices[0].pos={0,0,0};
        posed.vertices[1].pos={1,0,0};
        posed.vertices[2].pos={0,1,0};
        for(auto& v:posed.vertices){v.weights[0]=255;v.joints[0]=0;}
        posed.indices={0,1,2};
        render::MeshDef mesh;
        render::Primitive primitive;
        primitive.firstIndex=0;primitive.indexCount=3;
        primitive.firstVertex=0;primitive.vertexCount=3;
        primitive.jointPalette={0};
        mesh.primitives.push_back(primitive);
        posed.meshes.push_back(mesh);
        std::vector<render::mat4> pose(3);
        pose[1]=render::mat4::translate({0,0,2});
        SkinnedWeightSurface surface;
        req(prepareSkinnedWeightSurface(posed,0,pose,surface) &&
            surface.ready && std::fabs(surface.world[0].z-2)<1.e-5f,
            "skinned picker failed to follow animated joint world matrices");
        const auto hit=pickSkinnedWeightVertex(
            posed,surface,{0.05f,0.05f,4},{0,0,-1});
        req(hit.hit && hit.vertex==0 && std::fabs(hit.rayDistance-2)<1.e-5f,
            "skinned raycast used source undeformed triangles");
        pose[1].at(0,0)=0; // singularity is rejected upstream during rig load
        pose[1].at(0,3)=std::numeric_limits<float>::quiet_NaN();
        const auto old=surface.world[0];
        req(!prepareSkinnedWeightSurface(posed,0,pose,surface) &&
            surface.world[0].z==old.z,
            "invalid source pose partially published skinned pick cache");
    }
    {
        render::Model painted;
        painted.nodes.resize(3);
        painted.nodes[0].name="root";
        painted.nodes[1].name="jaw";painted.nodes[1].parent=0;
        painted.nodes[2].name="skinMesh";painted.nodes[2].skin=0;
        painted.nodes[2].mesh=0;
        render::Skin skin;skin.joints={0,1};skin.inverseBind.resize(2);
        painted.skins.push_back(skin);
        painted.vertices.resize(3);
        painted.vertices[0].pos={-0.1f,0,0};
        painted.vertices[1].pos={0.1f,0,0};
        painted.vertices[2].pos={2.f,0,0};
        for(auto& v:painted.vertices) {
            v.weights[0]=200;v.joints[0]=0;
            v.weights[1]=55;v.joints[1]=1;
        }
        painted.indices={0,1,2};
        render::Primitive p;p.firstIndex=0;p.indexCount=3;
        p.firstVertex=0;p.vertexCount=3;p.jointPalette={0,1};
        render::MeshDef mesh;mesh.primitives.push_back(p);
        painted.meshes.push_back(mesh);
        SkinWeightAuthoring stroke;
        req(stroke.beginStroke() &&
            stroke.strokeBrush(painted,0,0,"jaw",1.f,0.16f,0.4f,true) &&
            stroke.strokeBrush(painted,0,1,"jaw",1.f,0.16f,0.4f,true) &&
            stroke.endStroke() && stroke.canUndo(),
            "continuous weight drag did not commit as one stroke");
        req(stroke.undo() && stroke.edits().empty() &&
            !stroke.undo() && stroke.redo() && stroke.edits().size()==2,
            "continuous weight paint generated multiple undo steps");
        const auto saved=stroke.toJson();
        req(stroke.beginStroke() &&
            stroke.strokeBrush(painted,0,0,"jaw",0.2f,0.16f,0.5f,true) &&
            !stroke.endStroke(false) && stroke.toJson()==saved,
            "cancelled weight drag changed committed authored weights");
    }
    model.nodes.resize(2); // missing jaw and eyes must be legal for morph-only rigs
    const auto fallback=HumanRigBinding::compile(model,HumanDNA{}.rig);
    req(fallback.valid()&&!fallback.hasFacialJoints(),"morph-only rig rejected");
}
}
int main(){try{check();std::cout<<"VIVID human rig PASS\n";}
catch(const std::exception& e){std::cerr<<"VIVID human rig FAIL: "<<e.what()<<"\n";return 1;}return 0;}
