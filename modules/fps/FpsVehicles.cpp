// Typed FPS vehicles over the vehicle-controllers package (libraries/.../mechanics/Vehicle*.h): map vehicles with a
// `type` from `fps.vehicleTypes` drive with the package controller (wheeled / tracked / hover / flight) on the
// ground height under them, collide with static geometry through the package's collision response, and carry a
// driver plus passengers / gunners through the package's seats. Vehicles without a type keep the arcade car (FpsSystems actorSystems).
// Each typed vehicle is the Vehicle entity plus its VehicleDrive component; iteration in ascending vehicle id.
// Passengers and gunners may shoot; the driver steers. A destroyed vehicle kills its occupants.
#include "FpsModule.h"
#include "libraries/include/aiverselib/mechanics/VehiclePersistence.h"
#include "libraries/include/aiverselib/mechanics/VehicleDrive.h"
#include "engine/core/FixedMath.h"
#include <algorithm>
namespace aiverse::fps {
using namespace lib::genre;
VehicleDrive* Module::vehicleDrive(Simulation& sim,uint32_t vehicle) const {
    return sim.world().tryGet<VehicleDrive>(vehicleEntity(sim,vehicle));
}
void Module::setupVehicles(Simulation& sim,const Json& map) {
    const auto& list=map["vehicles"];if(!list.isArray())return;
    World& w=sim.world();const auto entities=vehicleEntities(sim);
    for(size_t i=0;i<list.size()&&i<entities.size();++i){
        const auto& j=list[i];if(!j.has("type"))continue;
        if(!config_.vehicleTypes)throw JsonError("fps vehicle.type: requires fps.vehicleTypes");
        auto type=j.getInt("type");auto it=config_.vehicleTypes->vehicles.find(type);
        if(it==config_.vehicleTypes->vehicles.end())throw JsonError("fps vehicle.type: unknown vehicle type "+std::to_string(type));
        if(j.has("yaw")&&!j["yaw"].isNumber())throw JsonError("fps vehicle.yaw: expected turns");
        auto& v=*w.tryGet<Vehicle>(entities[i]);v.yaw=j.getFx("yaw",Fixed{});
        VehicleDrive drive;drive.type=type;drive.controller=makeVehicleState(it->second.controller,v.pos,v.yaw);
        w.add<VehicleDrive>(entities[i],std::move(drive));
    }
}
bool Module::canFireFromVehicle(Simulation& sim,const CharacterRef& a) const {
    auto* run=vehicleDrive(sim,a.vehicle);if(!run)return false;
    const auto& def=config_.vehicleTypes->vehicles.at(run->type);auto seat=vehicleSeatOf(run->seats,a.slot);
    for(const auto& d:def.seats.seats)if(d.id==seat)return d.role!=VehicleSeatRole::Driver;
    return false;
}
// Interact on or in a typed vehicle becomes a seat request handled by tickVehicles this tick. False: not typed.
bool Module::vehicleInteract(Simulation& sim,CharacterRef a) {
    World& w=sim.world();if(!w.countExisting<VehicleDrive>())return false;
    if(state(sim).race.enabled)return true;   // racers stay at the wheel: Interact does nothing during a race
    VehicleSeatRequest r;r.actor=a.slot;r.team=a.team;r.actorPosition=a.pos;
    if(a.vehicle){
        auto* run=vehicleDrive(sim,a.vehicle);if(!run)return false;
        r.id=run->seats.lastRequest+1+int64_t(run->requests.size());r.action=VehicleSeatAction::Exit;r.seat=vehicleSeatOf(run->seats,a.slot);
        run->requests.push_back(r);return true;
    }
    VehicleDrive* best=nullptr;Fixed bestDistance;
    for(const Entity e:vehicleEntities(sim)){
        auto* run=w.tryGet<VehicleDrive>(e);if(!run)continue;
        const auto& v=*w.tryGet<Vehicle>(e);
        if(v.health<=0)continue;auto d=(v.pos-a.pos).lengthSq();if(d>36_fx)continue;
        if(!best||d<bestDistance){best=run;bestDistance=d;}
    }
    if(!best)return false;
    r.id=best->seats.lastRequest+1+int64_t(best->requests.size());r.action=VehicleSeatAction::Enter;r.preferred=VehicleSeatRole::Driver;
    best->requests.push_back(r);return true;
}
// The driver's held controls (released after the input timeout) through the shared axes mapping (mechanics/VehicleDrive.h):
// forward / back -> throttle / brake, right steers, Jump is the handbrake, pitch drives flight.
VehicleInput Module::driverInput(const ConstCharacterRef& a,uint32_t now) const {
    if(now-a.lastInputTick>config_.inputTimeout)return {};
    return vehicleInputFromAxes({Fixed::fromRatio(a.input.forward,1000),Fixed::fromRatio(a.input.right,1000),a.input.pitch,(a.input.buttons&Jump)!=0});
}
void Module::tickVehicles(Simulation& sim,std::span<const std::pair<uint32_t,VehicleInput>> drive) {
    World& w=sim.world();if(!w.countExisting<VehicleDrive>())return;auto now=sim.tick();
    const auto count=int64_t(characterCount(sim));
    for(const Entity entity:vehicleEntities(sim)){
        auto* typed=w.tryGet<VehicleDrive>(entity);if(!typed)continue;
        auto& run=*typed;Vehicle* v=w.tryGet<Vehicle>(entity);
        const auto& def=config_.vehicleTypes->vehicles.at(run.type);
        // Seats: enter / exit requests of this tick; an occupant with a free seat ahead takes the driver seat first.
        auto clear=[&](Vec3 p,int64_t){auto b=bodyBounds(p,config_.motor.height,config_.motor);b.min.y+=Fixed::fromRatio(1,100);return !collision_.blocked(b)&&fxAbs(p.x)<config_.worldLimit&&fxAbs(p.z)<config_.worldLimit;};
        std::vector<VehicleSeatRequest> requests;std::swap(requests,run.requests);
        for(auto [seat,actor]:run.seats.occupants){
            const auto who=character(sim,uint8_t(actor));if(v->health>0&&who.health>0&&!who.downed&&who.vehicle==v->id)continue;   // knocked / killed / thrown out by damage
            VehicleSeatRequest r;r.id=run.seats.lastRequest+1+int64_t(requests.size());r.actor=actor;r.action=VehicleSeatAction::Eject;r.seat=seat;requests.push_back(r);}
        if(auto report=stepVehicleSeats(def.seats,run.seats,now,run.controller,std::span<const VehicleSeatRequest>(requests),clear)){
            for(const auto& e:report->events){
                if(e.actor<=0||e.actor>count)continue;auto a=character(sim,uint8_t(e.actor));
                if(e.kind==VehicleEventKind::SeatEntered){a.vehicle=v->id;a.velocity={};}
            }
            for(const auto& x:report->exits){auto a=character(sim,uint8_t(x.actor));a.vehicle=0;a.pos=x.position;a.velocity=run.controller.velocity;
                if(v->health<=0)damage(sim,0,a,1000);}
        }
        auto driver=vehicleDriver(def.seats,run.seats);v->driver=uint8_t(driver);
        // Controller step: the driver's held input (released after the input timeout) -> pedals and steering.
        VehicleInput in;
        if(driver&&v->health>0){
            in=driverInput(character(static_cast<const Simulation&>(sim),uint8_t(driver)),now);
            for(const auto& [id,x]:drive)if(id==v->id){in=x;break;}
        }else in.brake=1_fx;
        auto under=run.controller.position+Vec3{{},2_fx,{}};auto hit=collision_.cast(under,{{},-64_fx,{}});
        VehicleContacts k;k.groundHeight=hit.hit?under.y-64_fx*hit.fraction:groundBelow(run.controller.position);k.groundNormal=hit.hit&&hit.normal.y>Fixed{}?hit.normal:Vec3{{},1_fx,{}};
        k.grounded=run.controller.position.y<=k.groundHeight+Fixed::fromRatio(1,10);
        auto before=run.controller.position;
        if(stepVehicle(def.controller,run.controller,now,sim.dt(),in,k)){
            auto delta=run.controller.position-before;Vec3 half{1_fx,Fixed::fromRatio(1,2),2_fx};
            auto sweep=collision_.cast(before+Vec3{{},1_fx,{}},Vec3{delta.x,{},delta.z},half);
            if(sweep.hit&&sweep.normal.y<Fixed::fromRatio(1,2)){
                auto stop=before+Vec3{delta.x,{},delta.z}*fxMax(Fixed{},sweep.fraction-Fixed::fromRatio(1,100));stop.y=run.controller.position.y;
                auto impulse=vehicleResolveCollision(run.controller,stop,sweep.normal);
                if(impulse>8_fx&&(!raceRules_||raceRules_->crashDamage))v->health=std::max(0,v->health-int32_t((impulse-8_fx).toInt()*10));   // hard crash
            }
            auto& p=run.controller.position;p.x=fxClamp(p.x,-config_.worldLimit,config_.worldLimit);p.z=fxClamp(p.z,-config_.worldLimit,config_.worldLimit);
        }
        v->pos=run.controller.position;v->yaw=run.controller.yaw;v->velocity=run.controller.velocity;
        for(auto [seat,actor]:run.seats.occupants){
            if(actor<=0||actor>count)continue;auto a=character(sim,uint8_t(actor));
            for(const auto& d:def.seats.seats)if(d.id==seat)a.pos=vehicleWorldPoint(run.controller,d.entry)+Vec3{{},Fixed::fromRatio(1,2),{}};
            a.velocity=run.controller.velocity;a.grounded=true;
        }
    }
}
void ioVehicleDrive(Archive& a,uint32_t& vehicle,VehicleDrive& d) {
    a(vehicle,d.type);lib::genre::io(a,d.controller);lib::genre::io(a,d.seats);
}
}
