#include "MetaHumanAdapter.h"
#include "Face.h"
#include "HumanAppearance.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

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
        if(r.correctiveJoints>0)r.notes.push_back("Корректирующие кости ("+std::to_string(r.correctiveJoints)+") остаются в bind pose: драйверы RigLogic не исполняются, позы локтей/плеч без коррекции.");
        if(r.facialJoints>0)r.notes.push_back("Лицевые кости FACIAL_* ("+std::to_string(r.facialJoints)+") не анимируются: мимика идёт через ARKit-морфы.");
        if(!r.channelsMissing.empty())r.notes.push_back("Нет ARKit-каналов: "+[&]{std::string s;for(const auto& c:r.channelsMissing)s+=(s.empty()?"":", ")+c;return s;}()+" — они не двигаются.");
        if(r.channelsFound.empty())r.notes.push_back("В FaceMesh нет ARKit-морфов: экспортируйте с ARKit blend shapes (или используйте tools/vivid-dna-import).");
        r.notes.push_back("Грум (strand) не импортируется: используются карточки из экспорта; .dna читается только офлайн-инструментом tools/vivid-dna-import.");
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
}
