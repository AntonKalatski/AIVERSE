#pragma once
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsTraffic.h"
#include "modules/rts/RtsDriving.h"
#include "modules/rts/RtsArsenal.h"
#include "modules/rts/RtsTopDown.h"
namespace aiverse::game {
// Custody (traffic.services.<id>.custody): who counts as an officer, whether an offender is armed, and the
// stand-in body a fallen hero leaves for the officers to take away. Trigger vars police_* are the script protocol.
inline bool custodyOfficer(aiverse::Simulation& s,aiverse::Entity e){
 using namespace aiverse;using namespace game;auto&m=static_cast<RtsModule&>(s.module());auto*t=m.traffic();
 if(t)if(auto*st=t->store(s))for(const auto&c:st->cars)if(c.service>=0&&t->rules().services[size_t(c.service)].answers==kIncidentCrime)
  if(std::find(c.crew.begin(),c.crew.end(),e)!=c.crew.end())return true;
 const auto*u=s.world().tryGet<Unit>(e);if(!u||!t)return false;
 for(const auto&sv:t->rules().services)if(sv.custody&&(sv.crew==u->type||std::find(sv.officers.begin(),sv.officers.end(),u->type)!=sv.officers.end()))return true;
 return false;
}
inline bool offenderArmed(aiverse::Simulation&s,aiverse::Entity e){
 using namespace aiverse;using namespace game;auto&m=static_cast<RtsModule&>(s.module());auto*u=s.world().tryGet<Unit>(e);if(!u)return false;
 static const std::vector<std::string> none;const auto&bare=m.traffic()?m.traffic()->rules().incidents.unarmed:none;
 auto unarmed=[&](const std::string&id){return std::find(bare.begin(),bare.end(),id)!=bare.end();};
 if(auto*a=m.arsenal())if(auto*st=a->store(s))for(const auto&hand:st->hands)if(hand.hero==e){
  const auto&slots=a->rules().weapons;
  for(size_t i=0;i<slots.size()&&i<hand.ammo.size();++i)if(u->type<slots[i].index.size()&&slots[i].index[u->type]>=0&&!unarmed(slots[i].weapon)&&hand.ammo[i]!=0)return true;
  return false;
 }
 for(const auto&weapon:m.rules().types[u->type].weapons)if(!unarmed(weapon.id))return true;
 return false;
}
inline uint8_t victimPilot(aiverse::Simulation&s,aiverse::Entity victim){
 auto&m=static_cast<aiverse::game::RtsModule&>(s.module());if(auto*t=m.topDown())if(auto*st=t->store(s))for(size_t i=1;i<st->pilots.size();++i)if(st->pilots[i].hero==victim)return uint8_t(i);return 0;
}
inline bool custodyVictim(aiverse::Simulation&s,aiverse::Entity victim){
 auto&m=static_cast<aiverse::game::RtsModule&>(s.module());return victimPilot(s,victim)>0||m.state(s).trig.vars.count("police_body_for_"+std::to_string(victim.packed()));
}
inline aiverse::Entity custodyBody(aiverse::Simulation&s,aiverse::Entity victim,uint16_t type,uint8_t owner,aiverse::Vec3 at){
 using namespace aiverse;using namespace game;auto&m=static_cast<RtsModule&>(s.module());auto&v=m.state(s).trig.vars;
 auto key="police_body_for_"+std::to_string(victim.packed());auto old=v.find(key);
 if(old!=v.end()&&old->second.raw){auto e=Entity::fromPacked(uint64_t(old->second.toInt()-1));if(s.world().alive(e))return e;}
 const uint16_t proxy=m.traffic()?m.traffic()->rules().incidents.patient:UINT16_MAX;auto body=m.spawn(s,proxy!=UINT16_MAX?proxy:type,owner,at);if(!body.valid())return body;
 auto suffix=std::to_string(body.packed());v[key]=Fixed::fromInt(int64_t(body.packed())+1);
 v["police_dead_"+suffix]=Fixed::fromInt(1);v["police_original_"+suffix]=Fixed::fromInt(type);
 v["police_victim_"+suffix]=Fixed::fromInt(int64_t(victim.packed())+1);
 v["resident_transport_"+suffix]=Fixed::fromInt(6);s.world().get<Unit>(body).flags|=kUnitDisabled;
 if(auto*td=m.topDown())if(auto*st=td->store(s))if(owner<st->pilots.size()&&(st->pilots[owner].hero==victim||(!s.world().alive(st->pilots[owner].hero)&&st->pilots[owner].heroType==type))){
  auto&p=st->pilots[owner];p.hero=body;p.respawnAt=0;p.vel={};p.input={};p.target=kNullEntity;
  v["police_"+std::to_string(owner)+"_phase"]=Fixed::fromInt(1);
 }
 return body;
}
}
