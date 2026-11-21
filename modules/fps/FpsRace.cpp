// The FPS host's adapter for races (rules "race" + map "race": the shared format of mechanics/RaceSessionData.h, the
// same in every project; docs/races.md) over the vehicle-controllers race session (mechanics/RaceSession.h): player i
// starts as the driver of the i-th typed map vehicle in ascending vehicle id (on the map's grid, else where the car
// stands); the countdown holds the brakes, bots (map players "bot": true) drive with the race bot along the racing
// line, the session counts checkpoints, laps, standings, wrong way and respawns, and the round ends with the race
// (winner = team of place 1). The FPS host owns the cars (controller step, static collision) exactly as for other
// typed vehicles.
// ECS shape: the session is match-level data (State::race); each character entity carries CharacterRacer{vehicle}
// (the car it races in); the cars are the Vehicle + VehicleDrive entities. Racer id = slot; order = slot order.
#include "FpsModule.h"
#include "libraries/include/aiverselib/mechanics/VehiclePersistence.h"
#include "libraries/include/aiverselib/mechanics/VehicleConfigured.h"
#include "libraries/include/aiverselib/gameplay/Configuration.h"
#include <algorithm>
namespace aiverse::fps {
using namespace lib::mechanics;
namespace bots=lib::bots;
namespace {
const VehicleSeatDef* driverSeat(const VehicleDefinition& d){for(const auto& seat:d.seats.seats)if(seat.role==VehicleSeatRole::Driver)return &seat;return nullptr;}
}
// Byte layout: enabled, session, racer count, vehicle id per slot (hash and race maps' save block).
void raceIo(Archive& a,RaceRun& r,std::vector<uint32_t>& vehicleOf){
    a(r.enabled);if(!r.enabled)return;
    lib::mechanics::io(a,r.session);
    uint32_t n=a.count(vehicleOf.size());if(n>kRaceMaxRacers){a.fail();return;}if(!a.writing())vehicleOf.resize(n);
    for(auto& v:vehicleOf)a(v);
}
std::vector<uint32_t> Module::racerVehicles(const Simulation& sim) const {
    std::vector<uint32_t> out;const World& w=sim.world();
    const auto* index=w.resourceConst<CharacterIndex>();if(!index)return out;
    for(const Entity e:index->bySlot){const auto* r=w.tryGetConst<CharacterRacer>(e);out.push_back(r?r->vehicle:0);}
    return out;
}
void Module::setupRace(Simulation& sim,const Json& rules,const Json& map){
    auto& s=state(sim);s.race=RaceRun{};raceLine_.reset();raceRules_.reset();raceTrack_={};
    if(!map.has("race")){if(rules.has("race")&&!rules["race"].isObject())throw JsonError("race: expected an object");return;}
    if(!config_.vehicleTypes)throw JsonError("map race: the cars are typed vehicles (fps.vehicleTypes)");
    raceRules_=readRaceRules(rules["race"],"race",config_.tickRate);
    raceTrack_=readRaceTrack(map["race"],"map race",1_fx);   // metre map
    if(raceRules_->laps)raceTrack_.course.laps=raceRules_->laps;
    const auto& r=*raceRules_;const auto n=characterCount(sim);
    World& w=sim.world();
    std::vector<Entity> cars;for(const Entity e:vehicleEntities(sim))if(w.tryGet<VehicleDrive>(e))cars.push_back(e);
    if(cars.size()<n)throw JsonError("map race: one typed map vehicle (\"type\") per player required");
    if(raceTrack_.grid&&raceTrack_.course.grid.size()<n)throw JsonError("map race.grid: one slot per player");
    std::vector<RaceSessionRacer> racers;std::vector<std::pair<Entity,uint32_t>> seated;
    for(size_t i=0;i<n;++i){
        auto& run=*w.tryGet<VehicleDrive>(cars[i]);auto& v=*w.tryGet<Vehicle>(cars[i]);
        auto a=character(sim,uint8_t(i+1));
        const auto& def=config_.vehicleTypes->vehicles.at(run.type);auto* seat=driverSeat(def);
        if(!seat)throw JsonError("fps.race: vehicle type "+std::to_string(run.type)+" needs a driver seat");
        if(raceTrack_.grid){const auto& slot=raceTrack_.course.grid[i];run.controller=makeVehicleState(def.controller,slot.position,slot.yaw);}
        else raceTrack_.course.grid.push_back({run.controller.position,run.controller.yaw});   // the car's own pose
        v.pos=run.controller.position;v.yaw=run.controller.yaw;v.velocity={};
        run.seats.occupants[seat->id]=a.slot;run.seats.team=a.team;run.seats.locked=true;
        a.vehicle=v.id;a.velocity={};a.pos=vehicleWorldPoint(run.controller,seat->entry)+Vec3{{},Fixed::fromRatio(1,2),{}};v.driver=a.slot;
        seated.push_back({a.entity,v.id});racers.push_back({int64_t(a.slot),bool(bots_[i])});
    }
    for(const auto& [e,id]:seated)w.add<CharacterRacer>(e,CharacterRacer{id});
    if(!raceTrack_.line.empty())raceLine_=bots::makeRaceLine(raceTrack_.line);
    auto session=makeRaceSession(raceTrack_.course,r.session,racers,raceLine_?&*raceLine_:nullptr,sim.tick());
    if(!session)throw JsonError(raceLine_?"map race: invalid race":"map race: bots need a racing line (\"line\")");
    auto& race=state(sim).race;race.enabled=true;race.session=std::move(*session);
}
void Module::tickRace(Simulation& sim){
    const auto& r=*raceRules_;const auto& course=raceTrack_.course;auto now=sim.tick();
    const Simulation& view=sim;World& w=sim.world();
    const auto vehicleOf=racerVehicles(view);
    auto runOf=[&](size_t i)->VehicleDrive*{return i<vehicleOf.size()?vehicleDrive(sim,vehicleOf[i]):nullptr;};
    auto carOf=[&](uint32_t id)->Vehicle*{return w.tryGet<Vehicle>(vehicleEntity(view,id));};
    // 1. Pedals: countdown brakes, race bots, players' own input.
    std::vector<RaceDriver> drivers;
    for(size_t i=0;i<vehicleOf.size();++i)if(auto* run=runOf(i))
        drivers.push_back({int64_t(i+1),run->controller.position,run->controller.velocity,run->controller.yaw,driverInput(character(view,uint8_t(i+1)),now)});
    std::vector<std::pair<uint32_t,VehicleInput>> drive;
    if(auto inputs=raceSessionInputs(r.session,raceLine_?&*raceLine_:nullptr,state(sim).race.session,now,drivers))
        for(const auto& x:*inputs)drive.push_back({vehicleOf[size_t(x.racer-1)],x.input});
    else for(auto id:vehicleOf){VehicleInput hold;hold.brake=1_fx;hold.handbrake=true;drive.push_back({id,hold});}
    // 2. The host steps the cars (controller, static collision, seats).
    tickVehicles(sim,drive);
    // 3. Race: checkpoints, laps, standings; wrecked or fallen cars go back to their last checkpoint.
    auto& s=state(sim);
    std::vector<RaceSample> samples;std::vector<int64_t> requests;
    for(size_t i=0;i<vehicleOf.size();++i)if(auto* run=runOf(i)){
        samples.push_back({int64_t(i+1),run->controller.position,run->controller.velocity});
        auto* v=carOf(vehicleOf[i]);if((v&&v->health<=0)||run->controller.position.y< -50_fx)requests.push_back(int64_t(i+1));
    }
    const auto count=int64_t(characterCount(view));
    if(auto report=stepRaceSession(course,r.session,s.race.session,now,samples,requests)){
        for(const auto& spot:report->race.respawns){
            auto* run=runOf(size_t(spot.racer-1));if(!run)continue;auto* v=carOf(vehicleOf[size_t(spot.racer-1)]);
            const auto& def=config_.vehicleTypes->vehicles.at(run->type);
            respawnVehicle(def.controller,run->controller,spot.position,spot.yaw);
            if(v){v->pos=run->controller.position;v->yaw=run->controller.yaw;v->velocity={};if(v->health<=0)v->health=Vehicle{}.health;}
            for(auto [seat,actor]:run->seats.occupants)for(const auto& d:def.seats.seats)if(d.id==seat&&actor>=1&&actor<=count){
                auto a=character(sim,uint8_t(actor));a.pos=vehicleWorldPoint(run->controller,d.entry)+Vec3{{},Fixed::fromRatio(1,2),{}};a.velocity={};}
        }
    }
    // 4. Result: the race decides the round; at the round limit the leader wins.
    if(s.winner)return;
    auto teamOf=[&](int64_t racer){return racer>=1&&racer<=count?int32_t(character(view,uint8_t(racer)).team):-1;};
    if(s.race.session.phase==RacePhase::Finished){auto winner=raceSessionWinner(s.race.session);s.winner=winner?teamOf(winner):-1;}
    else if(now+1>=config_.roundTicks)s.winner=s.race.session.standings.empty()?-1:teamOf(s.race.session.standings.front().racer);
}
bool Module::validRace(const RaceRun& r,const std::vector<uint32_t>& racerVehicles,const std::vector<uint32_t>& typedVehicles) const {
    const size_t n=spawns_.size();
    if(!lib::mechanics::valid(r.session)||racerVehicles.size()!=n||r.session.race.racers.size()!=n)return false;
    for(size_t i=0;i<n;++i){
        if(!r.session.race.racers.contains(int64_t(i+1)))return false;
        if(std::find(typedVehicles.begin(),typedVehicles.end(),racerVehicles[i])==typedVehicles.end())return false;
    }
    for(const auto& [id,b]:r.session.bots){(void)b;if(id<1||id>int64_t(bots_.size())||!bots_[size_t(id-1)])return false;}
    return true;
}
RaceView Module::raceView(Simulation& sim,uint8_t slot) const {
    const auto* s=sim.world().resourceConst<State>();if(!s||!s->race.enabled||!raceRules_)return {};
    return lib::mechanics::raceView(raceTrack_.course,s->race.session,slot,sim.tick());
}
}
