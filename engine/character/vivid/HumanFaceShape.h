#pragma once
#include "HumanAppearance.h"
namespace aiverse::vivid {
// Topology-preserving identity edits in the selected head's world bind frame.
// Landmarks use normalized head bounds; photo XY landmarks preserve depth.
struct HumanFaceShape {
 std::vector<std::string> meshes;
 std::map<std::string,float> values;
 struct Landmark {render::float3 from,to;float radius=.25f,weight=1;};
 std::vector<Landmark> landmarks;
 static HumanFaceShape fromJson(const Json&);
 Json toJson() const;
 static Json schema();
};
void applyHumanFaceShape(render::Model&,const HumanFaceShape&);
// A scan supplies sparse surface correspondences in aligned world coordinates.
// This is bounded template fitting, not topology/rig generation from a scan.
HumanFaceShape faceShapeFromScan(const render::Model& head,const render::Model& scan,
 const HumanFaceShape& selection,float maximumDistance,float strength);
}
