#include "MetaHumanAdapter.h"
#include "Face.h"
#include "HumanAppearance.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <map>
#include <cmath>

namespace aiverse::vivid {
namespace {
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower((unsigned char)c));return s;}
// glTF importers append "_<index>" / ".001" to duplicate node names.
std::string bone(std::string n){const auto slash=n.rfind('/');if(slash!=std::string::npos)n=n.substr(slash+1);
    static const std::regex suffix(R"((\.\d+|_\d+)$)");std::string prev;
    while(prev!=n){prev=n;if(n.size()>2&&std::regex_search(n,suffix)&&!std::regex_match(n,std::regex(R"((spine|neck)_0\d)")))n=std::regex_replace(n,suffix,"");}
    return n;}
bool has(const std::string& s,const char* part){return s.find(part)!=std::string::npos;}
}

MetaHumanReport adaptMetaHuman(const render::Model& m) {
    MetaHumanReport r;
    std::set<std::string> bones;
    for(const auto& n:m.nodes){const auto b=bone(n.name);bones.insert(b);
        if(b.rfind("FACIAL_",0)==0)++r.facialJoints;
        const auto l=lower(b);if(has(l,"corrective")||has(l,"_bulge")||has(l,"_half_")||has(l,"latissimus"))++r.correctiveJoints;}
    r.skins=int(m.skins.size());
    const bool ue5=bones.contains("spine_05")&&bones.contains("neck_02")&&bones.contains("head")&&bones.contains("pelvis");
    if(ue5){r.score+=2;r.headBone="head";}
    if(r.facialJoints>50)++r.score;
    // Meshes: FaceMesh (head), groom cards, body / clothing.
    std::set<std::string> face;
    int maxLod=0;static const std::regex lod(R"(_LOD(\d+)$)",std::regex::icase);
    for(const auto& mesh:m.meshes){
        std::smatch mm;if(std::regex_search(mesh.name,mm,lod))maxLod=std::max(maxLod,std::stoi(mm[1]));
        const auto l=lower(mesh.name);
        if(has(l,"facemesh")){face.insert(mesh.name);++r.score;}
        else if(has(l,"eyebrows")||has(l,"eyelashes")||has(l,"lashes"))face.insert(mesh.name);
    }
    r.lodLevels=maxLod+1;
    r.faceMeshes.assign(face.begin(),face.end());
    for(const auto& mat:m.materials){
        const auto l=lower(mat.name);std::string role;
        if(has(l,"head_shader")||has(l,"bodysynthesized")||has(l,"_body_")||has(l,"skin"))role="skin";
        else if(has(l,"eyeleft")||has(l,"eyeright"))role="eyes";
        else if(has(l,"eyelashes"))role="lashes";
        else if(has(l,"eyeshell")||has(l,"eyeedge")||has(l,"saliva")||has(l,"cartilage")){r.hiddenMaterials.push_back(mat.name);continue;}
        else if(has(l,"teeth"))continue; // keeps its source PBR
        else if((has(l,"hair")||has(l,"eyebrows")||has(l,"beard")||has(l,"mustache")||has(l,"fuzz"))&&!has(l,"coverage"))role="hair";
        else if(has(l,"coverage"))continue; // companion coverage cores stay source
        else if(has(l,"cinematic")||has(l,"shirt")||has(l,"pants")||has(l,"slacks")||has(l,"shoe")||has(l,"top_")||has(l,"btm_")||has(l,"shs_"))role="cloth";
        if(!role.empty())r.materialRoles.emplace_back(mat.name,role);
        if(has(l,"head_shader"))++r.score;
    }
    // ARKit coverage over the face meshes' morph names.
    std::set<std::string> morphs;
    for(const auto& mesh:m.meshes)if(face.contains(mesh.name))for(const auto& t:mesh.morphTargetNames)morphs.insert(t);
    for(std::size_t i=0;i<kChannelCount;++i){const std::string c(channelName(Channel(i)));(morphs.contains(c)?r.channelsFound:r.channelsMissing).push_back(c);}
    r.detected=r.score>=3;
    if(!r.detected)r.notes.push_back("Не похоже на экспорт MetaHuman: нет скелета UE5 (spine_05, neck_02, head) и/или FaceMesh / head_shader.");
    if(r.detected) {
        if(r.correctiveJoints>0)r.notes.push_back("Корректирующие кости ("+std::to_string(r.correctiveJoints)+"): суставы *_half и твисты предплечья ведёт VIVID; остальные (bulge, in/out, fwd/bck) стоят в bind pose — RigLogic не исполняется.");
        if(r.facialJoints>0)r.notes.push_back("Лицевые кости FACIAL_* ("+std::to_string(r.facialJoints)+"): челюсть и глаза ведутся по ARKit-каналам, если морфы их ещё не двигают; остальные — через морфы.");
        if(!r.channelsMissing.empty())r.notes.push_back("Нет ARKit-каналов: "+[&]{std::string s;for(const auto& c:r.channelsMissing)s+=(s.empty()?"":", ")+c;return s;}()+" — они не двигаются.");
        if(r.channelsFound.empty())r.notes.push_back("В FaceMesh нет ARKit-морфов: экспортируйте с ARKit blend shapes (или используйте tools/vivid-dna-import).");
        r.notes.push_back("Грум (strand) не импортируется: используются карточки из экспорта; .dna читается только офлайн-инструментом tools/vivid-dna-import; GLB с Draco распаковывается офлайн: tools/vivid-dna-import/decompress_draco.cjs.");
    }
    return r;
}

Json MetaHumanReport::toJson() const {
    auto j=Json::object();auto arr=[](const std::vector<std::string>& v){auto a=Json::array();for(const auto& s:v)a.push(Json::string(s));return a;};
    j.set("detected",Json::fromBool(detected));j.set("score",Json::number(int64_t(score)));j.set("headBone",Json::string(headBone));
    j.set("faceMeshes",arr(faceMeshes));j.set("hiddenMaterials",arr(hiddenMaterials));
    auto roles=Json::object();for(const auto& [k,v]:materialRoles)roles.set(k,Json::string(v));j.set("materialRoles",roles);
    j.set("channelsFound",Json::number(int64_t(channelsFound.size())));j.set("channelsMissing",arr(channelsMissing));
    j.set("facialJoints",Json::number(int64_t(facialJoints)));j.set("correctiveJoints",Json::number(int64_t(correctiveJoints)));
    j.set("skins",Json::number(int64_t(skins)));j.set("lodLevels",Json::number(int64_t(lodLevels)));j.set("notes",arr(notes));
    return j;
}
Json MetaHumanReport::look() const {
    HumanAppearance a;for(const auto& [k,v]:materialRoles)a.materials[k]=v;return a.toJson();
}
Json MetaHumanReport::face() const {
    auto j=Json::object();j.set("version",Json::number(int64_t(1)));
    auto meshes=Json::array();for(const auto& s:faceMeshes)meshes.push(Json::string(s));
    j.set("meshes",meshes);j.set("values",Json::object());j.set("landmarks",Json::array());return j;
}

namespace {
render::quat conj(render::quat q){return {-q.x,-q.y,-q.z,q.w};}
// Twist of q about unit axis a (swing-twist decomposition).
render::quat twistOf(render::quat q,render::float3 a){const float d=q.x*a.x+q.y*a.y+q.z*a.z;return render::qnormalize(render::quat{a.x*d,a.y*d,a.z*d,q.w});}
render::quat power(render::quat q,float t){return render::slerp(render::quat{},q,t);}
}

MetaHumanRig buildMetaHumanRig(const render::Model& m) {
    MetaHumanRig r;
    std::map<std::string,int> byName;
    for(size_t i=0;i<m.nodes.size();++i)byName.emplace(bone(m.nodes[i].name),int(i));
    if(!byName.contains("spine_05")||!byName.contains("head"))return r; // not a MetaHuman skeleton
    static const std::regex half(R"(^(.+)_half_([lr])$)"),twist(R"(^lowerarm_twist_0(\d)_([lr])$)");
    // Every duplicated skeleton (body, face, clothing skins) gets its own
    // drivers: the base joint is looked up next to the corrective joint.
    auto near=[&](int node,const std::string& want)->int{
        const int p=m.nodes[size_t(node)].parent;if(p<0)return -1;
        if(bone(m.nodes[size_t(p)].name)==want)return p;
        for(int c:m.nodes[size_t(p)].children)if(c!=node&&bone(m.nodes[size_t(c)].name)==want)return c;
        return -1;};
    for(size_t i=0;i<m.nodes.size();++i) {
        const auto name=bone(m.nodes[i].name);std::smatch mm;MetaHumanRig::Driver d;d.node=int(i);
        if(std::regex_match(name,mm,half)) {
            d.base=near(d.node,mm[1].str()+"_"+mm[2].str());if(d.base<0)continue;
            d.factor=m.nodes[i].parent==d.base?-.5f:.5f;
        } else if(std::regex_match(name,mm,twist)) {
            d.base=near(d.node,"hand_"+mm[2].str());if(d.base<0)continue;
            d.twist=true;d.factor=std::clamp(float(std::stoi(mm[1].str()))/3.f,0.f,1.f);
        } else continue;
        d.bindNode=m.nodes[i].r;d.bindBase=m.nodes[size_t(d.base)].r;r.drivers.push_back(d);
    }
    // ARKit morph slots on face meshes, and whether they already move the
    // teeth / eyeballs (then the joints must not move them a second time).
    auto movesMaterial=[&](const char* morph,const char* materialPart){
        for(const auto& n:m.nodes){if(n.mesh<0)continue;const auto& mesh=m.meshes[size_t(n.mesh)];
            const auto it=std::find(mesh.morphTargetNames.begin(),mesh.morphTargetNames.end(),morph);if(it==mesh.morphTargetNames.end())continue;
            const size_t t=size_t(it-mesh.morphTargetNames.begin());
            for(const auto& p:mesh.primitives){if(p.material<0||size_t(p.material)>=m.materials.size())continue;
                if(lower(m.materials[size_t(p.material)].name).find(materialPart)==std::string::npos)continue;
                if(p.morphDeltas.size()<(t+1)*p.vertexCount*6u)return true; // deltas released: assume baked (do not double-drive)
                for(size_t v=0;v<p.vertexCount;++v){const float* d=&p.morphDeltas[(t*p.vertexCount+v)*6];if(std::fabs(d[0])+std::fabs(d[1])+std::fabs(d[2])>1e-4f)return true;}}}
        return false;};
    auto slots=[&](const char* morph){std::vector<std::pair<int,int>> out;
        for(size_t i=0;i<m.nodes.size();++i){const auto& n=m.nodes[i];if(n.mesh<0)continue;const auto& names=m.meshes[size_t(n.mesh)].morphTargetNames;
            const auto it=std::find(names.begin(),names.end(),morph);if(it!=names.end()&&it-names.begin()<render::kMaxMorphTargets)out.emplace_back(int(i),int(it-names.begin()));}
        return out;};
    if(auto j=byName.find("FACIAL_C_Jaw");j!=byName.end()&&!movesMaterial("jawOpen","teeth")){r.jaw=j->second;r.jawOpen=slots("jawOpen");}
    const bool eyesBaked=movesMaterial("eyeLookOutLeft","eyeleft")||movesMaterial("eyeLookUpLeft","eyeleft");
    if(!eyesBaked) {
        if(auto e=byName.find("FACIAL_L_Eye");e!=byName.end())r.eyeL=e->second;
        if(auto e=byName.find("FACIAL_R_Eye");e!=byName.end())r.eyeR=e->second;
        r.lookUpL=slots("eyeLookUpLeft");r.lookDownL=slots("eyeLookDownLeft");r.lookInL=slots("eyeLookInLeft");r.lookOutL=slots("eyeLookOutLeft");
        r.lookUpR=slots("eyeLookUpRight");r.lookDownR=slots("eyeLookDownRight");r.lookInR=slots("eyeLookInRight");r.lookOutR=slots("eyeLookOutRight");
    }
    return r;
}

void applyMetaHumanRig(const MetaHumanRig& r,const render::Model& m,render::Pose& pose) {
    if(r.empty()||pose.size()!=m.nodes.size())return;
    for(const auto& d:r.drivers) {
        const auto delta=conj(d.bindBase)*pose[size_t(d.base)].r;
        const auto part=d.twist?power(twistOf(delta,{1,0,0}),d.factor):power(d.factor<0?conj(delta):delta,std::fabs(d.factor));
        pose[size_t(d.node)].r=render::qnormalize(d.bindNode*part);
    }
    if(r.jaw<0&&r.eyeL<0&&r.eyeR<0)return;
    auto weight=[&](const std::vector<std::pair<int,int>>& s){float w=0;for(const auto& [n,k]:s)w=std::max(w,pose[size_t(n)].w[k]);return w;};
    std::vector<render::mat4> world;render::worldMatrices(m,pose,world);
    // Rotate a joint by a WORLD-space rotation around its own pivot.
    auto turn=[&](int node,render::quat worldTurn){
        const int parent=m.nodes[size_t(node)].parent;
        const auto parentRot=parent>=0?render::rotationOf(world[size_t(parent)]):render::quat{};
        const auto own=render::rotationOf(world[size_t(node)]);
        pose[size_t(node)].r=render::qnormalize(conj(parentRot)*worldTurn*own);};
    if(r.jaw>=0)turn(r.jaw,render::quat::axisAngle({1,0,0},.32f*weight(r.jawOpen)));
    auto eye=[&](int node,float yaw,float pitch){if(node<0)return;
        turn(node,render::quat::axisAngle({0,1,0},yaw)*render::quat::axisAngle({1,0,0},pitch));};
    eye(r.eyeL,.4f*(weight(r.lookOutL)-weight(r.lookInL)),-.35f*(weight(r.lookUpL)-weight(r.lookDownL)));
    eye(r.eyeR,.4f*(weight(r.lookInR)-weight(r.lookOutR)),-.35f*(weight(r.lookUpR)-weight(r.lookDownR)));
}

Json MetaHumanRig::toJson() const {
    auto j=Json::object();size_t halves=0,twists=0;for(const auto& d:drivers)(d.twist?twists:halves)++;
    j.set("halfJoints",Json::number(int64_t(halves)));j.set("twistJoints",Json::number(int64_t(twists)));
    j.set("jawJoint",Json::fromBool(jaw>=0));j.set("eyeJoints",Json::fromBool(eyeL>=0||eyeR>=0));
    return j;
}
}
