#pragma once
// Battle-royale presentation for the FPS client: the zone wall in the world, a minimap (circles, carrier, crates,
// bombardment, own position and facing), the phase timer and counters, skydive hints, an outside-the-zone warning,
// the kill feed and the result banner. Reads only the snapshot's RoyaleView (presentation never writes the sim).
#include "engine/i18n/Localization.h"
#include "engine/net/authority/Session.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/SpatialWorld.h"
#include "third_party/imgui/imgui.h"
#include <cmath>
#include <string>
namespace aiverse::fps::view {
// Wall segments of the current circle within `reach` metres of the viewer (the rest is too far to matter).
template<class Visual>
void drawZoneWall(render::Renderer3D& renderer,const render::Mesh& cube,const RoyaleView& v,Vec3 viewer,world::Cell origin,Visual visual,float reach=400){
    if(!v.enabled||v.circle.radius<=Fixed{})return;
    double cx=v.circle.center.x.toDouble(),cz=v.circle.center.y.toDouble(),r=v.circle.radius.toDouble();
    double px=viewer.x.toDouble(),pz=viewer.z.toDouble();double base=std::atan2(px-cx,pz-cz);
    double span=std::min(3.14159265,reach/std::max(r,1.0));int steps=int(std::min(400.0,span*r/6))+1;
    for(int i=-steps;i<=steps;++i){
        double a=base+span*i/steps,a2=base+span*(i+1)/steps;
        double x=cx+std::sin(a)*r,z=cz+std::cos(a)*r,x2=cx+std::sin(a2)*r,z2=cz+std::cos(a2)*r;
        double len=std::hypot(x2-x,z2-z);if(len<0.01)continue;
        Vec3 at{Fixed::fromRatio(int64_t(x*1000),1000),viewer.y-20_fx,Fixed::fromRatio(int64_t(z*1000),1000)};
        auto p=visual(world::relativeTo(at,origin,32));
        auto t=render::mat4::translate(p)*render::mat4::rotateY(float(std::atan2(x2-x,z2-z)))*render::mat4::scale({0.2f,80.f,float(len)});
        renderer.drawMesh(cube,t,{0.25f,0.55f,1.f,1});
    }
}
inline ImVec2 add(ImVec2 a,float x,float y){return {a.x+x,a.y+y};}
inline std::string clock(uint32_t ticks,uint32_t rate){auto s=ticks/std::max(1u,rate);char b[16];std::snprintf(b,sizeof b,"%u:%02u",s/60,s%60);return b;}
inline void drawRoyaleHud(const i18n::Localization& lang,const net::authority::Snapshot& snap,const Actor& self,const Config& config){
    const auto& v=snap.royale;if(!v.enabled||!config.royale)return;
    auto* draw=ImGui::GetForegroundDrawList();auto size=ImGui::GetIO().DisplaySize;float w=size.x,h=size.y;
    // Minimap (top right): the playable bounds scaled into a square.
    const auto& b=config.royale->bounds;float side=std::min(220.f,h*0.3f),x0=w-side-16,y0=16;
    double bw=(b.max.x-b.min.x).toDouble(),bh=(b.max.y-b.min.y).toDouble();
    auto map=[&](double x,double z){return ImVec2(x0+float((x-b.min.x.toDouble())/bw)*side,y0+float((z-b.min.y.toDouble())/bh)*side);};
    auto scale=[&](Fixed r){return float(r.toDouble()/bw)*side;};
    draw->AddRectFilled({x0,y0},{x0+side,y0+side},IM_COL32(20,28,24,200),4);
    draw->PushClipRect({x0,y0},{x0+side,y0+side},true);
    if(v.bombardment)draw->AddCircleFilled(map(v.bombArea.center.x.toDouble(),v.bombArea.center.y.toDouble()),scale(v.bombArea.radius),IM_COL32(220,40,30,110),40);
    draw->AddCircle(map(v.circle.center.x.toDouble(),v.circle.center.y.toDouble()),scale(v.circle.radius),IM_COL32(70,140,255,255),96,2);
    draw->AddCircle(map(v.target.center.x.toDouble(),v.target.center.y.toDouble()),scale(v.target.radius),IM_COL32_WHITE,96,1.5f);
    for(auto c:v.crates){auto p=map(c.x.toDouble(),c.z.toDouble());draw->AddRectFilled(add(p,-3,-3),add(p,3,3),IM_COL32(255,200,40,255));}
    if(v.divePhase==0)draw->AddCircleFilled(map(v.carrier.x.toDouble(),v.carrier.z.toDouble()),4,IM_COL32(230,230,230,255));
    auto me=map(self.pos.x.toDouble(),self.pos.z.toDouble());float yaw=float(self.yaw.toDouble()*6.2831853);
    draw->AddTriangleFilled(add(me,std::sin(yaw)*8,std::cos(yaw)*8),add(me,std::sin(yaw+2.5f)*5,std::cos(yaw+2.5f)*5),add(me,std::sin(yaw-2.5f)*5,std::cos(yaw-2.5f)*5),IM_COL32(255,240,80,255));
    draw->PopClipRect();
    // Phase line and counters (top centre).
    auto phase=lang.tr(v.stage==0?"fps.royale.waiting":v.stage==1?"fps.royale.wait":v.stage==2?"fps.royale.shrink":"fps.royale.closed",
        i18n::Args().num("phase",int(v.phase)+1).str("time",clock(v.ticksLeft,config.tickRate)));
    auto counts=lang.tr("fps.royale.counts",i18n::Args().num("alive",int(v.alive)).num("teams",int(v.teamsIn)).num("kills",int(v.kills)));
    auto line=phase+"   "+counts;auto ts=ImGui::CalcTextSize(line.c_str());
    draw->AddRectFilled({w/2-ts.x/2-10,8},{w/2+ts.x/2+10,ts.y+20},IM_COL32(0,0,0,140),4);draw->AddText({w/2-ts.x/2,14},IM_COL32_WHITE,line.c_str());
    // Hints and warnings (centre).
    std::string hint;
    if(v.divePhase==0)hint=lang.tr(v.doorOpen?"fps.royale.jump":"fps.royale.doorClosed");
    else if(v.divePhase==1)hint=lang.tr("fps.royale.freefall");
    else if(v.outside&&self.health>0)hint=lang.tr("fps.royale.outside");
    if(!hint.empty()){auto hs=ImGui::CalcTextSize(hint.c_str());draw->AddText({w/2-hs.x/2,h*0.62f},v.outside&&v.divePhase==3?IM_COL32(255,90,70,255):IM_COL32(230,230,230,255),hint.c_str());}
    if(v.outside&&v.divePhase==3)draw->AddRect(ImVec2(2,2),ImVec2(w-2,h-2),IM_COL32(220,40,30,160),0.f,0,6.f);
    // Kill feed (right, under the minimap).
    float fy=y0+side+12;
    for(auto it=v.feed.rbegin();it!=v.feed.rend();++it){
        const auto& e=*it;const char* key=e.kind==lib::genre::KillFeedKind::Knocked?"fps.royale.feed.knocked":e.kind==lib::genre::KillFeedKind::Eliminated?"fps.royale.feed.eliminated":
            e.kind==lib::genre::KillFeedKind::Revived?"fps.royale.feed.revived":e.kind==lib::genre::KillFeedKind::TeamOut?"fps.royale.feed.teamOut":nullptr;
        if(!key)continue;
        auto text=lang.tr(key,i18n::Args().num("victim",int(e.subject)).num("actor",int(e.actor)).num("place",int(e.value)));
        if(e.kind!=lib::genre::KillFeedKind::TeamOut&&!e.actor)text=lang.tr("fps.royale.feed.environment",i18n::Args().num("victim",int(e.subject)));
        auto fs=ImGui::CalcTextSize(text.c_str());draw->AddText({w-16-fs.x,fy},e.subject==self.slot?IM_COL32(255,120,90,255):e.actor==self.slot?IM_COL32(255,230,120,255):IM_COL32(220,220,220,255),text.c_str());fy+=fs.y+3;
    }
    // Result banner.
    if(v.finished||(self.health<=0&&v.placement)){
        auto text=v.placement==1?lang.tr("fps.royale.winner"):lang.tr("fps.royale.placed",i18n::Args().num("place",int(v.placement)).num("teams",int(v.teams)));
        ImGui::SetWindowFontScale(1);auto rs=ImGui::CalcTextSize(text.c_str());float sc=2.f;
        draw->AddRectFilled({w/2-rs.x*sc/2-20,h*0.4f-10},{w/2+rs.x*sc/2+20,h*0.4f+rs.y*sc+10},IM_COL32(0,0,0,170),6);
        draw->AddText(ImGui::GetFont(),ImGui::GetFontSize()*sc,{w/2-rs.x*sc/2,h*0.4f},IM_COL32(255,215,90,255),text.c_str());
    }
}
}
