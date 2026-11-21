// FPS races (`fps.mode` "race" + `fps.race`) over the vehicle-controllers race session (genre/RaceSession.h): every
// player starts as the driver of the typed map vehicle with its index, on the grid of the `fps.vehicleTypes` track;
// the countdown holds the brakes, bots drive with the race bot along the track's racing line, the session counts
// checkpoints / laps, standings, wrong way and respawns, and the round ends with the race (winner = team of place 1).
// The FPS host owns the cars (controller step, static collision) exactly as for other typed vehicles.
#include "FpsModule.h"
#include "libraries/include/aiverselib/genre/VehiclePersistence.h"
#include "libraries/include/aiverselib/genre/VehicleConfigured.h"
#include "libraries/include/aiverselib/gameplay/Configuration.h"
#include <algorithm>
namespace aiverse::fps {
using namespace lib::genre;
namespace gameplay=lib::gameplay;
namespace bots=lib::bots;
namespace {
uint32_t raceTicks(const Json& j,const char* key,uint32_t fallback){
    if(!j.has(key))return fallback;
    auto v=j[key];if(!v.isNumber())throw JsonError(std::string("fps.race.")+key+": expected ticks");
    auto x=v.fx();if(x.raw%Fixed::kOne||x<Fixed{}||x>Fixed::fromInt(kRaceSessionMaxTicks))throw JsonError(std::string("fps.race.")+key+": expected 0.."+std::to_string(kRaceSessionMaxTicks)+" ticks");
    return uint32_t(x.floorInt());
}
const VehicleSeatDef* driverSeat(const VehicleDefinition& d){for(const auto& seat:d.seats.seats)if(seat.role==VehicleSeatRole::Driver)return &seat;return nullptr;}
}
RaceRules readRace(const Json& j,const VehicleDefinitions& defs,uint32_t tickRate){
    for(const auto& [key,value]:j.members()){(void)value;
        if(key!="track"&&key!="laps"&&key!="countdownTicks"&&key!="finishGraceTicks"&&key!="timeoutTicks"&&key!="crashDamage"&&key!="vehicles")
            throw JsonError("fps.race."+key+": unknown key");}
    RaceRules r;
    if(!j["track"].isNumber())throw JsonError("fps.race.track: expected the id of a fps.vehicleTypes track");
    r.track=j.getInt("track");auto track=defs.tracks.find(r.track);
    if(track==defs.tracks.end())throw JsonError("fps.race.track: unknown track "+std::to_string(r.track));
    r.course=track->second.course;r.line=track->second.line;
    if(j.has("laps")){auto laps=raceTicks(j,"laps",3);if(laps<1||laps>1000)throw JsonError("fps.race.laps: expected 1..1000");r.course.laps=laps;}
    r.session.countdownTicks=raceTicks(j,"countdownTicks",3*tickRate);
    r.session.finishGraceTicks=raceTicks(j,"finishGraceTicks",10*tickRate);
    r.session.timeoutTicks=raceTicks(j,"timeoutTicks",0);
    if(j.has("crashDamage")){if(j["crashDamage"].type()!=Json::Type::Bool)throw JsonError("fps.race.crashDamage: expected boolean");r.crashDamage=j["crashDamage"].boolean();}
    if(j.has("vehicles")){
        // The generated `vehicles` settings section (wrong way, stuck respawn, race bot knobs).
        Json root=Json::object();root.set("schemaVersion",Json::number(int64_t(1)));root.set("vehicles",j["vehicles"]);
        auto settings=gameplay::readGameplaySettings(root);
        if(!settings.value)throw JsonError("fps.race."+(settings.errors.empty()?std::string("vehicles: invalid"):settings.errors.front()));
        auto errors=vehicleSettingsErrors(*settings.value);if(!errors.empty())throw JsonError("fps.race."+errors.front());
        r.session.race=vehicleRaceConfig(*settings.value);r.session.bot=bots::raceBotConfig(*settings.value);
        if(j["vehicles"].has("raceLaps")&&!j.has("laps"))r.course.laps=vehicleRaceLaps(*settings.value);
    }
    if(!valid(r.course)||!valid(r.session))throw JsonError("fps.race: invalid track or race settings");
    if(!r.line.empty()&&!bots::makeRaceLine(r.line))throw JsonError("fps.race.track: invalid racing line (closed loop of at least 3 distinct points)");
    return r;
}
void raceIo(Archive& a,RaceRun& r){
    a(r.enabled);if(!r.enabled)return;
    lib::genre::io(a,r.session);
    uint32_t n=a.count(r.vehicleOf.size());if(n>kRaceMaxRacers){a.fail();return;}if(!a.writing())r.vehicleOf.resize(n);
    for(auto& v:r.vehicleOf)a(v);
}
void io(Archive& a,RaceView& v){
    a(v.enabled);if(!v.enabled)return;
    a(v.phase,v.place,v.racers,v.lap,v.laps,v.checkpoint,v.checkpoints,v.countdownTicks,v.wrongWay,v.finished,v.winner);
    uint32_t n=a.count(v.order.size());if(n>kRaceMaxRacers){a.fail();return;}if(!a.writing())v.order.resize(n);for(auto& x:v.order)a(x);
}
void Module::setupRace(Simulation& sim,const Json&,State& s){
    s.race=RaceRun{};raceLine_.reset();
    if(!config_.race)return;
    const auto& r=*config_.race;const auto n=s.actors.size();
    if(s.vehicleRuns.size()<n)throw JsonError("fps.race: one typed map vehicle (\"type\") per player required");
    if(r.course.grid.size()<n)throw JsonError("fps.race: the track grid needs one slot per player");
    std::vector<RaceSessionRacer> racers;
    for(size_t i=0;i<n;++i){
        auto& run=s.vehicleRuns[i];auto& a=s.actors[i];
        Vehicle* v=nullptr;for(auto& x:s.vehicles)if(x.id==run.vehicle)v=&x;
        const auto& def=config_.vehicleTypes->vehicles.at(run.type);auto* seat=driverSeat(def);
        if(!v||!seat)throw JsonError("fps.race: vehicle type "+std::to_string(run.type)+" needs a driver seat");
        const auto& slot=r.course.grid[i];
        run.controller=makeVehicleState(def.controller,slot.position,slot.yaw);
        v->pos=slot.position;v->yaw=slot.yaw;v->velocity={};
        run.seats.occupants[seat->id]=a.slot;run.seats.team=a.team;run.seats.locked=true;
        a.vehicle=v->id;a.velocity={};a.pos=vehicleWorldPoint(run.controller,seat->entry)+Vec3{{},Fixed::fromRatio(1,2),{}};v->driver=a.slot;
        s.race.vehicleOf.push_back(v->id);racers.push_back({int64_t(a.slot),bool(bots_[i])});
    }
    if(!r.line.empty())raceLine_=bots::makeRaceLine(r.line);
    auto session=makeRaceSession(r.course,r.session,racers,raceLine_?&*raceLine_:nullptr,sim.tick());
    if(!session)throw JsonError(raceLine_?"fps.race: invalid race":"fps.race: bots need the track's racing line (\"line\")");
    s.race.enabled=true;s.race.session=std::move(*session);
}
void Module::tickRace(Simulation& sim){
    auto& s=state(sim);const auto& r=*config_.race;auto now=sim.tick();
    auto runOf=[&](size_t i)->VehicleRun*{return i<s.race.vehicleOf.size()?vehicleRun(s,s.race.vehicleOf[i]):nullptr;};
    auto vehicleOf=[&](uint32_t id)->Vehicle*{for(auto& v:s.vehicles)if(v.id==id)return &v;return nullptr;};
    // 1. Pedals: countdown brakes, race bots, players' own input.
    std::vector<RaceDriver> drivers;
    for(size_t i=0;i<s.actors.size();++i)if(auto* run=runOf(i))
        drivers.push_back({int64_t(i+1),run->controller.position,run->controller.velocity,run->controller.yaw,driverInput(s.actors[i],now)});
    std::vector<std::pair<uint32_t,VehicleInput>> drive;
    if(auto inputs=raceSessionInputs(r.session,raceLine_?&*raceLine_:nullptr,s.race.session,now,drivers))
        for(const auto& x:*inputs)drive.push_back({s.race.vehicleOf[size_t(x.racer-1)],x.input});
    else for(auto id:s.race.vehicleOf){VehicleInput hold;hold.brake=1_fx;hold.handbrake=true;drive.push_back({id,hold});}
    // 2. The host steps the cars (controller, static collision, seats).
    tickVehicles(sim,drive);
    // 3. Race: checkpoints, laps, standings; wrecked or fallen cars go back to their last checkpoint.
    std::vector<RaceSample> samples;std::vector<int64_t> requests;
    for(size_t i=0;i<s.actors.size();++i)if(auto* run=runOf(i)){
        samples.push_back({int64_t(i+1),run->controller.position,run->controller.velocity});
        auto* v=vehicleOf(run->vehicle);if((v&&v->health<=0)||run->controller.position.y< -50_fx)requests.push_back(int64_t(i+1));
    }
    if(auto report=stepRaceSession(r.course,r.session,s.race.session,now,samples,requests)){
        for(const auto& spot:report->race.respawns){
            auto* run=runOf(size_t(spot.racer-1));if(!run)continue;auto* v=vehicleOf(run->vehicle);
            const auto& def=config_.vehicleTypes->vehicles.at(run->type);
            respawnVehicle(def.controller,run->controller,spot.position,spot.yaw);
            if(v){v->pos=run->controller.position;v->yaw=run->controller.yaw;v->velocity={};if(v->health<=0)v->health=Vehicle{}.health;}
            for(auto [seat,actor]:run->seats.occupants)for(const auto& d:def.seats.seats)if(d.id==seat&&actor>=1&&actor<=int64_t(s.actors.size())){
                auto& a=s.actors[size_t(actor-1)];a.pos=vehicleWorldPoint(run->controller,d.entry)+Vec3{{},Fixed::fromRatio(1,2),{}};a.velocity={};}
        }
    }
    // 4. Result: the race decides the round; at the round limit the leader wins.
    if(s.winner)return;
    auto teamOf=[&](int64_t racer){return racer>=1&&racer<=int64_t(s.actors.size())?int32_t(s.actors[size_t(racer-1)].team):-1;};
    if(s.race.session.phase==RacePhase::Finished){auto w=raceSessionWinner(s.race.session);s.winner=w?teamOf(w):-1;}
    else if(now+1>=config_.roundTicks)s.winner=s.race.session.standings.empty()?-1:teamOf(s.race.session.standings.front().racer);
}
bool Module::validRace(const State& c) const {
    const auto& r=c.race;
    if(!lib::genre::valid(r.session)||r.vehicleOf.size()!=c.actors.size()||r.session.race.racers.size()!=c.actors.size())return false;
    for(size_t i=0;i<r.vehicleOf.size();++i){
        if(!r.session.race.racers.contains(int64_t(i+1)))return false;
        if(std::none_of(c.vehicleRuns.begin(),c.vehicleRuns.end(),[&](const VehicleRun& v){return v.vehicle==r.vehicleOf[i];}))return false;
    }
    for(const auto& [id,b]:r.session.bots){(void)b;if(id<1||id>int64_t(bots_.size())||!bots_[size_t(id-1)])return false;}
    return true;
}
RaceView Module::raceView(Simulation& sim,uint8_t slot) const {
    RaceView v;const auto* s=sim.world().resourceConst<State>();if(!s||!s->race.enabled||!config_.race)return v;
    const auto& session=s->race.session;v.enabled=true;v.phase=uint8_t(session.phase);
    v.racers=uint16_t(session.race.racers.size());v.laps=uint16_t(config_.race->course.laps);v.checkpoints=uint16_t(config_.race->course.checkpoints.size());
    v.countdownTicks=raceSessionCountdown(session,sim.tick());v.winner=uint8_t(std::max<int64_t>(0,raceSessionWinner(session)));
    for(const auto& st:session.standings){
        v.order.push_back(uint8_t(st.racer));
        if(st.racer==slot){v.place=uint16_t(st.place);v.lap=uint16_t(std::min<uint32_t>(st.lap+(st.finished?0:1),config_.race->course.laps));v.checkpoint=uint16_t(st.nextCheckpoint);v.wrongWay=st.wrongWay;v.finished=st.finished;}
    }
    return v;
}
}
