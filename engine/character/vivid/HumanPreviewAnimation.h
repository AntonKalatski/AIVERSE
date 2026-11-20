#pragma once
// Generate presentation clips consumed by the existing Animator/MotionWeave.
#include "engine/render/Animation.h"
#include "engine/data/Json.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <string>
#include <cctype>
#include <stdexcept>
namespace aiverse::vivid {
inline constexpr std::array<const char*,4> humanPreviewPoses={"relaxed","a_pose","t_pose","portrait"};
struct HumanPreviewAnimation {
 std::string pose="relaxed";bool playing=true,faceIdle=true;float speed=1;
 int poseIndex()const{for(size_t i=0;i<humanPreviewPoses.size();++i)if(pose==humanPreviewPoses[i])return int(i);throw std::invalid_argument("Unknown character pose");}
 Json toJson()const{auto j=Json::object();j.set("pose",Json::string(pose));j.set("playing",Json::fromBool(playing));j.set("faceIdle",Json::fromBool(faceIdle));j.set("speed",Json::number(double(speed)));return j;}
 static HumanPreviewAnimation fromJson(const Json& j){if(!j.isObject())throw std::invalid_argument("Invalid preview animation");for(const auto& [k,v]:j.members())if(k!="pose"&&k!="playing"&&k!="faceIdle"&&k!="speed")throw std::invalid_argument("Unknown animation field");HumanPreviewAnimation s;s.pose=j.getStr("pose");(void)s.poseIndex();if(j["playing"].type()!=Json::Type::Bool||j["faceIdle"].type()!=Json::Type::Bool)throw std::invalid_argument("Invalid animation toggle");s.playing=j.getBool("playing");s.faceIdle=j.getBool("faceIdle");if(!j["speed"].isNumber()||!std::isfinite(j["speed"].toDouble())||j["speed"].toDouble()<.25||j["speed"].toDouble()>2)throw std::invalid_argument("Invalid animation speed");s.speed=float(j["speed"].toDouble());return s;}
};
inline std::string humanBoneSemantic(std::string name){const auto slash=name.rfind('/');if(slash!=std::string::npos)name=name.substr(slash+1);const auto pos=name.rfind('_');if(pos!=std::string::npos&&pos+1<name.size()&&std::all_of(name.begin()+pos+1,name.end(),[](unsigned char c){return std::isdigit(c);}))name.resize(pos);return name;}
// Smooth closure is faster than reopening; every event has zero endpoint velocity.
inline float humanIdleBlink(float time){
 if(!std::isfinite(time))return 0;
 const float t=std::fmod(std::max(0.f,time),6.f);
 float result=0;
 for(float peak:{1.7f,4.4f}){
  const float d=t-peak;
  const float x=std::clamp(1.f-std::abs(d)/(d<0?.10f:.20f),0.f,1.f);
  result=std::max(result,x*x*(3-2*x));
 }
 return result;
}
// Preserve the face/breath phase when switching pose or toggling facial idle.
// Reuse the host Animator, including its quaternion crossfade and paused behavior.
inline void playHumanPreview(render::Animator& animator,int clip,bool playing){
 const bool previous=animator.clip(0)>=0;
 const float phase=previous?animator.time(0):0;
 render::Animator::Play play;play.restart=true;play.fade=previous&&playing?.65f:0.f;
 play.startTime=phase;
 animator.play(0,clip,play);
}
inline render::AnimClip humanPreviewClip(const render::Model& model,int poseIndex,bool faceIdle){
 using namespace render;if(poseIndex<0||poseIndex>=4)throw std::invalid_argument("Invalid pose index");
 AnimClip clip;clip.name="__vivid_preview_"+std::to_string(poseIndex)+(faceIdle?"_face":"_body");clip.duration=6;
 std::vector<std::string> names;for(const auto& n:model.nodes)names.push_back(humanBoneSemantic(n.name));
 for(size_t i=0;i<model.nodes.size();++i){const auto& node=model.nodes[i];const auto& name=names[i];
  bool arm=name=="upperarm_l"||name=="upperarm_r",neck=name=="neck_01",spine=name=="spine_01"||name=="spine_02"||name=="spine_03";quat armDelta;
  if(arm){int end=-1;const auto targetName=name=="upperarm_l"?"lowerarm_l":"lowerarm_r";for(size_t k=0;k<names.size();++k)if(names[k]==targetName){for(int parent=model.nodes[k].parent;parent>=0;parent=model.nodes[size_t(parent)].parent)if(parent==int(i)){end=int(k);break;}if(end>=0)break;}
   if(end<0)arm=false;else{auto a=model.bindWorld[i].transformPoint({}),b=model.bindWorld[size_t(end)].transformPoint({});auto from=normalize(b-a);float sign=from.x<0?-1.f:1.f;auto to=normalize(poseIndex==2?float3{sign,0,0}:poseIndex==1?float3{sign*.7f,-.7f,0}:float3{sign*.18f,-1,0});auto axis=cross(from,to);if(length(axis)>1e-5f)armDelta=quat::axisAngle(axis,std::acos(std::clamp(dot(from,to),-1.f,1.f)));}
  }
  if(arm||neck||spine){AnimChannel c;c.node=int(i);c.path=AnimChannel::Rotation;c.comps=4;const auto parent=node.parent<0?quat{}:rotationOf(model.bindWorld[size_t(node.parent)]);const quat inv{-parent.x,-parent.y,-parent.z,parent.w};
   for(int frame=0;frame<=120;++frame){const float t=frame*.05f,wave=std::sin(t*6.28318530718f/6);const bool resting=poseIndex==0||poseIndex==3;
    // Upper-body breathing and small delayed arm settling. Root/legs stay in
    // bind so feet do not slide; this does not pretend to solve weight-shift IK.
    const float breath=std::sin(t*6.28318530718f/3);
    quat world;
    if(arm)world=quat::axisAngle({0,0,1},resting?.006f*wave:0)*quat::axisAngle({1,0,0},resting?.008f*breath:0)*armDelta;
    else if(neck)world=quat::axisAngle({0,1,0},(poseIndex==3?.14f:0)+.025f*wave)*quat::axisAngle({1,0,0},.012f*breath);
    else world=quat::axisAngle({1,0,0},.004f*breath)*quat::axisAngle({0,0,1},.0025f*wave);
    const auto q=qnormalize(inv*world*parent*node.r);c.times.push_back(t);for(float v:{q.x,q.y,q.z,q.w})c.values.push_back(v);}clip.channels.push_back(std::move(c));}
  if(node.mesh>=0){const auto& mesh=model.meshes.at(size_t(node.mesh));const auto count=std::min(mesh.morphTargetNames.size(),size_t(kMaxMorphTargets));if(!count)continue;AnimChannel c;c.node=int(i);c.path=AnimChannel::Weights;c.comps=uint8_t(count);
   for(int frame=0;frame<=120;++frame){float t=frame*.05f;c.times.push_back(t);for(size_t k=0;k<count;++k){const auto& key=mesh.morphTargetNames[k];float v=node.weights[k];
    if(faceIdle){if(key=="eyeBlinkLeft"||key=="eyeBlinkRight")v=humanIdleBlink(t);else if(key=="mouthSmileLeft"||key=="mouthSmileRight")v=.035f+.025f*(1-std::cos(t*6.28318530718f/6));else if(key=="browInnerUp")v=.015f*(1-std::cos(t*6.28318530718f/6));
     else if(key=="eyeLookInLeft"||key=="eyeLookOutRight")v=.12f*std::pow(std::max(0.f,std::sin(t*6.28318530718f/6)),2.f);
     else if(key=="eyeLookOutLeft"||key=="eyeLookInRight")v=.12f*std::pow(std::max(0.f,-std::sin(t*6.28318530718f/6)),2.f);
     else if(key=="eyeLookUpLeft"||key=="eyeLookUpRight")v=.035f*(1-std::cos(t*6.28318530718f/3));}c.values.push_back(v);}}
   clip.channels.push_back(std::move(c));}
 }
 return clip;
}
inline int appendHumanPreviewClips(render::Model& model){const int base=int(model.clips.size());for(int i=0;i<4;++i)for(bool face:{false,true})model.clips.push_back(humanPreviewClip(model,i,face));return base;}
}
