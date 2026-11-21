#pragma once
// MetaHuman compatibility adapter (no Unreal at runtime).
//
// Input: an ordinary render::Model imported from a permitted MetaHuman export
// (glTF/GLB/FBX produced by Unreal's exporter or the metahuman-to-glb
// companion). The adapter never reads proprietary .dna at runtime and never
// links Epic code: it recognises MetaHuman naming conventions and produces the
// VIVID setup for that character, plus an explicit report of what is and is
// not supported:
//  - detection (UE5 MetaHuman skeleton + FaceMesh / head shader naming);
//  - material roles for HumanAppearance (skin, eyes, lashes, hair, cloth;
//    eye shells, tear/edge, saliva and cartilage are reported as hidden);
//  - face mesh selection for HumanFaceShape (face, brows, lashes);
//  - ARKit facial channel coverage per face mesh against VIVID's 52 channels;
//  - skeleton facts: head bone, FACIAL_* joints, corrective joints (whose
//    RigLogic drivers are NOT evaluated), skins, LOD levels.
#include "engine/data/Json.h"
#include "engine/render/Model.h"
#include "engine/render/Animation.h"
#include <string>
#include <vector>
namespace aiverse::vivid {
struct MetaHumanReport {
    bool detected=false;
    int score=0;                          // evidence count (>=3 = MetaHuman)
    std::string headBone;
    std::vector<std::string> faceMeshes;  // for HumanFaceShape::meshes
    std::vector<std::pair<std::string,std::string>> materialRoles; // material -> role
    std::vector<std::string> hiddenMaterials; // eye shell / edge / saliva / cartilage
    std::vector<std::string> channelsFound,channelsMissing; // ARKit vs VIVID 52
    int facialJoints=0,correctiveJoints=0,skins=0,lodLevels=1;
    std::vector<std::string> notes;       // unsupported / partial features, plainly
    Json toJson() const;
    Json look() const;                    // HumanAppearance JSON with the roles
    Json face() const;                    // HumanFaceShape JSON with the meshes
};
MetaHumanReport adaptMetaHuman(const render::Model& model);

// Presentation-only runtime drivers for a MetaHuman skeleton (NOT RigLogic):
//  - "<bone>_half_<side>" joints follow half of their base joint's rotation;
//  - "lowerarm_twist_0N_<side>" share the hand's roll (N/3);
//  - FACIAL_C_Jaw / FACIAL_L_Eye / FACIAL_R_Eye follow the ARKit channels
//    (jawOpen, eyeLook*) ONLY when the face morphs do not already move the
//    teeth / eyeballs (measured once from the morph deltas).
// Build once per model (needs CPU morph deltas), apply after the clip pose.
struct MetaHumanRig {
    struct Driver{int node=-1,base=-1;render::quat bindNode,bindBase;float factor=0;bool twist=false;};
    std::vector<Driver> drivers;
    int jaw=-1,eyeL=-1,eyeR=-1;
    // Face mesh nodes and the ARKit morph slots they carry: channel -> (node, slot).
    std::vector<std::pair<int,int>> jawOpen,lookUpL,lookDownL,lookInL,lookOutL,lookUpR,lookDownR,lookInR,lookOutR;
    bool empty() const {return drivers.empty()&&jaw<0&&eyeL<0&&eyeR<0;}
    Json toJson() const;
};
MetaHumanRig buildMetaHumanRig(const render::Model& model);
void applyMetaHumanRig(const MetaHumanRig& rig,const render::Model& model,render::Pose& pose);
}
