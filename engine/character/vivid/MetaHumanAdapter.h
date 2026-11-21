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
}
