#pragma once
#include "Systems.h"
#include "engine/game/Traffic.h"
#include "engine/game/Driving.h"
#include "engine/game/Arsenal.h"
#include "engine/game/TopDown.h"
#include "engine/game/ServiceState.h"
namespace aiverse::game {
// Custody (traffic.services.<id>.custody): who counts as an officer, whether an offender is armed, and the
// stand-in body a fallen hero leaves for the officers to take away. State: ServiceState.h.
inline bool custodyOfficer(aiverse::Simulation& s,aiverse::Entity e){
 using namespace aiverse;using namespace game;auto&m=hostOf(s);auto*t=m.traffic();
 if(t)if(auto*st=t->store(s))for(const auto&c:st->cars)if(c.service>=0&&t->rules().services[size_t(c.service)].answers==kIncidentCrime)
  if(std::find(c.crew.begin(),c.crew.end(),e)!=c.crew.end())return true;
 const auto*u=s.world().tryGet<Unit>(e);if(!u||!t)return false;
 for(const auto&sv:t->rules().services)if(sv.custody&&(sv.crew==u->type||std::find(sv.officers.begin(),sv.officers.end(),u->type)!=sv.officers.end()))return true;
 return false;
}
inline bool offenderArmed(aiverse::Simulation&s,aiverse::Entity e){
 using namespace aiverse;using namespace game;auto&m=hostOf(s);auto*u=s.world().tryGet<Unit>(e);if(!u)return false;
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
 auto&m=hostOf(s);if(auto*t=m.topDown())if(auto*st=t->store(s))for(size_t i=1;i<st->pilots.size();++i)if(st->pilots[i].hero==victim)return uint8_t(i);return 0;
}
inline bool custodyVictim(aiverse::Simulation&s,aiverse::Entity victim){
 bool standIn=false;s.world().each<StandIn>([&](aiverse::Entity,const StandIn&b){standIn=standIn||b.victim==victim;});
 return victimPilot(s,victim)>0||standIn;
}
inline aiverse::Entity custodyBody(aiverse::Simulation&s,aiverse::Entity victim,uint16_t type,uint8_t owner,aiverse::Vec3 at){
 using namespace aiverse;using namespace game;auto&m=hostOf(s);World&w=s.world();
 Entity existing=kNullEntity;w.each<StandIn>([&](Entity e,const StandIn&b){if(b.victim==victim&&!existing.valid())existing=e;});
 if(existing.valid())return existing;
 const uint16_t proxy=m.traffic()?m.traffic()->rules().incidents.patient:UINT16_MAX;auto body=m.spawn(s,proxy!=UINT16_MAX?proxy:type,owner,at);if(!body.valid())return body;
 w.add<StandIn>(body,StandIn{victim,type});w.add<ServicePassenger>(body).claim=kClaimCustody;w.get<Unit>(body).flags|=kUnitDisabled;
 if(auto*td=m.topDown())if(auto*st=td->store(s))if(owner<st->pilots.size()&&(st->pilots[owner].hero==victim||(!w.alive(st->pilots[owner].hero)&&st->pilots[owner].heroType==type))){
  auto&p=st->pilots[owner];p.hero=body;p.respawnAt=0;p.vel={};p.input={};p.target=kNullEntity;
  serviceLedger(w).player(owner).phase=kHeldCuffed;
 }
 return body;
}
// A prisoner whose custody vehicle is destroyed is free again (a hero gets his controls back).
inline void custodyGuard(aiverse::Simulation&s){
 using namespace aiverse;using namespace game;auto&m=hostOf(s);World&w=s.world();
 std::vector<Entity> freed;
 w.each<InCustody>([&](Entity e,const InCustody&c){const auto*car=w.tryGet<Unit>(c.car);if(!w.alive(c.car)||!car||car->hp.raw<=0)freed.push_back(e);});
 for(Entity e:freed){
  auto&u=w.get<Unit>(e);u.flags&=~kUnitInCargo;w.remove<InCustody>(e);if(auto*sp=w.tryGet<ServicePassenger>(e)){sp->claim=0;sp->aboard=false;}
  if(w.has<StandIn>(e))continue;
  u.flags&=~kUnitDisabled;
  if(auto*td=m.topDown())if(auto*st=td->store(s))for(size_t i=1;i<st->pilots.size();++i)if(st->pilots[i].hero==e){u.flags|=kUnitPossessed;serviceLedger(w).player(i).phase=kHeldNone;}
 }
}
}
