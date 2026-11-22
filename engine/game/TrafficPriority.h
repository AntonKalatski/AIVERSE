#pragma once
// Emergency priority: a vehicle answering a call (or one a script turned on with VehicleSignals) has its beacon on.
#include "Systems.h"
#include "engine/game/Traffic.h"
#include "engine/game/Driving.h"
#include "engine/game/ServiceState.h"
namespace aiverse::game {
inline bool activeResponse(aiverse::game::GameHost& m,aiverse::Simulation& sim,aiverse::Entity e){
 if(auto*d=m.driving())if(auto*car=d->car(sim,e))if(car->player)return false;
 auto*t=m.traffic();auto*store=t?t->store(sim):nullptr;if(!store)return false;
 const auto* carry=sim.world().tryGet<aiverse::game::CarryCar>(e);
 const bool transporting=carry&&carry->owned;
 for(const auto&c:store->cars)if(c.e==e)return c.kind==aiverse::game::kTrafficEmergency && (c.task==aiverse::game::kTaskNone||c.task==aiverse::game::kTaskGoing||(c.task==aiverse::game::kTaskHome&&transporting));
 return false;
}
inline bool beaconEnabled(aiverse::game::GameHost& m,aiverse::Simulation& sim,aiverse::Entity e){
 if(!sim.world().alive(e)||sim.world().get<aiverse::game::Unit>(e).hp.raw<=0)return false;
 if(const auto*sig=sim.world().tryGet<aiverse::game::VehicleSignals>(e);sig&&sig->beacon)return sig->beacon>0;
 return activeResponse(m,sim,e);
}
template<class Host> void syncTrafficSignals(aiverse::Simulation& sim,Host& host){
 auto&m=hostOf(sim);auto*d=m.driving();if(!d||!d->active())return;
 for(const aiverse::Entity e:aiverse::vehicles::vehicleEntities(sim.world())){bool beacon=beaconEnabled(m,sim,e);const auto*sig=sim.world().tryGet<aiverse::game::VehicleSignals>(e);
  bool siren=beacon&&(sig&&sig->siren?sig->siren>0:activeResponse(m,sim,e));
  host.trafficSignals(e.packed(),beacon,siren);
 }
}
}
