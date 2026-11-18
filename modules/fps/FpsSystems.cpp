#include "FpsModule.h"
#include "libraries/include/aiverselib/gameplay/Inventory.h"
#include "libraries/include/aiverselib/genre/EcsHitscanResolutionSystems.h"
#include "libraries/include/aiverselib/genre/EcsOrderedImpactSystems.h"
#include "libraries/include/aiverselib/genre/EcsProjectileSystems.h"
#include "libraries/include/aiverselib/genre/EcsDamageSystems.h"
#include <algorithm>
#include <optional>
namespace aiverse::fps {
namespace {
Vec3 position(const Json& j) {
    if(!j.isArray()||j.size()!=3)throw JsonError("fps object: expected position [x,y,z]");
    Vec3 v;Fixed* coordinates[]={&v.x,&v.y,&v.z};for(size_t i=0;i<3;++i){if(!j[i].isNumber()||fxAbs(j[i].fx())>100000_fx)throw JsonError("fps object: invalid coordinate");*coordinates[i]=j[i].fx();}return v;
}
bool near(Vec3 a,Vec3 b,Fixed radius){return (a-b).lengthSq()<=radius*radius;}
bool inside(Vec3 p,const world::Bounds& b){return p.x>=b.min.x&&p.x<=b.max.x&&p.y>=b.min.y&&p.y<=b.max.y&&p.z>=b.min.z&&p.z<=b.max.z;}
std::optional<lib::gameplay::ItemType> item(int64_t id){if(id<1||id>9)return {};return lib::gameplay::ItemType{id,20,1,0};}
bool bag(Actor& actor,int32_t type,int32_t count,size_t slots=12) {
    std::vector<lib::gameplay::ItemStack> items;for(auto i:actor.bag)items.push_back({i.type,i.count});
    auto result=count>0?lib::gameplay::addItem(items,{slots,100},{type,count},item):lib::gameplay::removeItem(items,{type,-count});
    if(!result)return false;
    actor.bag.clear();
    for(auto i:items)actor.bag.push_back({int32_t(i.type),int32_t(i.count)});
    return true;
}
bool has(const Actor& actor,int32_t type){for(auto i:actor.bag)if(i.type==type&&i.count>0)return true;return false;}
uint32_t ticks(const Simulation& sim){return sim.tick();}
}
void Module::setupSystems(Simulation& sim,const Json& map,State& s) {
    ladders_.clear();sites_.clear();penetrable_.clear();initialLoot_.clear();initialVehicles_.clear();
    if(!config_.systems.enabled)return;
    for(auto& actor:s.actors){actor.armor=config_.systems.startArmor;actor.money=config_.systems.startMoney;}
    auto boxes=[&](const char* key,std::vector<world::Bounds>& output){
        if(!map.has(key))return;
        if(!map[key].isArray()||map[key].size()>128)throw JsonError(std::string("fps.")+key+": expected at most 128 boxes");
        for(const auto& box:map[key].items()){world::Bounds b{position(box["min"]),position(box["max"])};if(b.min.x>=b.max.x||b.min.y>=b.max.y||b.min.z>=b.max.z)throw JsonError("fps region: invalid bounds");output.push_back(b);}
    };
    boxes("ladders",ladders_);boxes("bombSites",sites_);
    if(config_.mode=="bomb"){
        bool attack=false,defense=false;for(const auto& actor:s.actors){if(actor.team>2)throw JsonError("fps bomb: teams must be 1 or 2");attack|=actor.team==1;defense|=actor.team==2;}
        if(!attack||!defense||sites_.empty())throw JsonError("fps bomb: both teams and a bomb site are required");
        for(const auto& actor:s.actors)if(actor.team==1){s.match.carrier=actor.slot;break;}
    }
    for(const auto& box:map["colliders"].items())if(box.has("penetration")) {
        if(!box["penetration"].isNumber()||box["penetration"].fx()<=Fixed{}||box["penetration"].fx()>10_fx)throw JsonError("fps collider.penetration: expected (0,10]");
        penetrable_[uint32_t(box.getInt("id"))]=box["penetration"].fx();
    }
    auto array=[&](const char* key,size_t limit){if(map.has(key)&&(!map[key].isArray()||map[key].size()>limit))throw JsonError(std::string("fps.")+key+": invalid array size");};
    array("loot",256);array("vehicles",32);
    for(const auto& j:map["loot"].items()) {
        auto type=j.getInt("type"),count=j.getInt("count",1);
        if(!((type>=1&&type<=9)||(type>=100&&type<100+int64_t(config_.weapons.size()))||(type>=200&&config_.gear.contains(type-200)))||count<1||count>20)throw JsonError("fps loot: invalid type or count");
        Loot l{s.nextObject++,position(j["pos"]),int32_t(type),int32_t(count)};
        if(type>=200){l.count=1;l.ammo=int32_t(config_.gear.at(type-200).durability);}
        else if(type>=100){l.count=1;l.ammo=config_.weapons[size_t(type-100)].magazine;l.reserve=config_.weapons[size_t(type-100)].reserve;}
        s.loot.push_back(l);
    }
    for(const auto& j:map["vehicles"].items()) {
        auto speed=j.getFx("speed",15_fx);if(speed<1_fx||speed>50_fx)throw JsonError("fps vehicle.speed: expected 1..50");
        Vehicle v;v.id=s.nextObject++;v.pos=position(j["pos"]);v.speed=speed;
        if(collision_.blocked({v.pos-Vec3{1_fx,{},2_fx},v.pos+Vec3{1_fx,2_fx,2_fx}}))throw JsonError("fps vehicle spawn intersects collision");
        s.vehicles.push_back(v);
    }
    s.match.start=ticks(sim);initialLoot_=s.loot;initialVehicles_=s.vehicles;
}
void Module::damage(Simulation& sim,uint8_t source,Actor& targetView,int32_t amount,bool head) {
    auto& s=state(sim);
    Actor* attackerView=source&&source<=s.actors.size()?&s.actors[source-1]:nullptr;
    Actor* target=&targetView;
    Actor* attacker=attackerView;
    ActorRuntime* targetRuntime=nullptr;
    ActorRuntime* attackerRuntime=nullptr;
    if(config_.ecsAuthority) {
        // Damage the actual ECS gameplay payload, not the legacy working
        // vector. Copy in only to account for preceding actor-side tick work.
        targetRuntime=sim.world().tryGet<ActorRuntime>(actorEntity(sim,targetView.slot));
        if(!targetRuntime)throw std::logic_error("FPS target ActorRuntime missing");
        targetRuntime->value=targetView;
        target=&targetRuntime->value;
        if(attackerView) {
            if(source==targetView.slot)attacker=target;
            else {
                attackerRuntime=sim.world().tryGet<ActorRuntime>(actorEntity(sim,source));
                if(!attackerRuntime)throw std::logic_error("FPS attacker ActorRuntime missing");
                attackerRuntime->value=*attackerView;
                attacker=&attackerRuntime->value;
            }
        }
    }
    // Keep historical side-effect ordering (gear -> hits -> knockdown ->
    // kill/loot -> event). The lambda's early returns still commit ECS data
    // to the compatibility vector before returning from this method.
    const auto apply=[&](Actor& target,Actor* attacker) {
        if(target.health<=0||amount<=0)return;
        if(attacker&&source!=target.slot&&!config_.friendlyFire&&attacker->team==target.team)return;
        if(!config_.gear.empty()){   // gear-tiers: helmets cover zone 1 (head shots), everything else hits zone 2
            if(auto hit=lib::systems::absorbHit(config_.gear,s.gear[target.slot-1],head?1u:2u,amount)){
                amount=int32_t(hit->damage);for(auto slot:hit->broken)sim.emit({9,target.slot,{},{},int64_t(slot),target.pos});
            }
        }else if(config_.systems.enabled&&!head){auto absorbed=std::min(target.armor,amount/2);target.armor-=absorbed;amount-=absorbed;}
        target.lastDamage=sim.tick();target.activeAction=Action::None;target.actionUntil=0;
        if(s.royale.enabled&&attacker&&source!=target.slot){s.royale.lastAttacker[target.slot-1]=source;s.royale.lastWeapon[target.slot-1]=uint8_t(attacker->weaponIndex+1);}
        if(attacker)++attacker->hits;
        if(config_.systems.enabled&&config_.systems.knockdown&&!target.downed&&amount>=target.health) {
            bool teammate=false;for(const auto& a:s.actors)if(a.slot!=target.slot&&a.team==target.team&&a.health>0&&!a.downed)teammate=true;
            if(teammate){for(auto& v:s.vehicles)if(v.driver==target.slot)v.driver=0;target.vehicle=0;target.downed=true;target.health=30;target.bleedUntil=sim.tick()+config_.systems.bleedTicks;return;}
        }
        // Use the identical immediate integer HP commit as the generic ECS
        // DamageResolutionSystem. Armor/gear, knockdown, kill credit, loot
        // and events remain in their original same-tick order.
        lib::genre::applyIntegerHitPointDamage(target.health,amount);
        if(target.health)return;
        target.downed=false;target.respawnAt=sim.tick()+config_.respawnTicks;
        if(attacker&&source!=target.slot&&attacker->team!=target.team){++attacker->kills;attacker->money=std::min(16000,attacker->money+config_.systems.killReward);}
        if(config_.systems.enabled){
            if(s.match.carrier==target.slot){s.match.carrier=0;s.match.bombPos=target.pos;}
            if(s.loot.size()<256&&(target.owned&(1u<<target.weaponIndex)))s.loot.push_back({s.nextObject++,target.pos,100+target.weaponIndex,1,target.ammo,target.reserve,target.attachments[target.weaponIndex]});
            if(!s.gear.empty()){for(const auto& [slot,piece]:s.gear[target.slot-1].slots)if(s.loot.size()<256)s.loot.push_back({s.nextObject++,target.pos,int32_t(200+piece.def),1,int32_t(piece.durability)});s.gear[target.slot-1]={};}
            for(auto& v:s.vehicles)if(v.driver==target.slot)v.driver=0;
            target.vehicle=0;
        }
        sim.emit({2,source,{}, {},target.slot,target.pos});
    };
    apply(*target,attacker);
    if(targetRuntime) {
        targetView=targetRuntime->value;
        if(attackerRuntime)*attackerView=attackerRuntime->value;
    }
}
bool Module::traceShot(Simulation& sim,uint8_t source,Vec3 eye,Vec3 delta,int32_t amount,const WeaponConfig& weapon,const std::vector<Actor>& targets,Projectile* projectile) {
    auto& s=state(sim);Fixed best=1_fx;uint8_t victim=0;const Actor* pose=nullptr;
    // Rewind pose selection still comes from the original tick history.
    // Share only the deterministic nearest-hit policy; keep vehicle and
    // world occlusion queries below in their exact legacy order.
    // The shared synchronous ECS intent policy is also used by the
    // independently schedulable HitscanResolutionSystem. Resolve inline
    // here to retain the old per-shooter damage/event order and FPS rewind.
    lib::genre::HitscanIntentComponent shotIntent;
    shotIntent.origin=eye;
    shotIntent.delta=delta;
    shotIntent.maximumFraction=best;
    shotIntent.requested=true;
    const auto actorHit=lib::genre::resolveHitscanIntent(
        shotIntent,actorEntity(sim,source),targets,
        [&](const Actor& target) {
            return target.slot!=source && target.health>0 &&
                target.life==s.actors[target.slot-1].life;
        },
        [&](const Actor& target) {return actorBounds(target,config_.motor);},
        [&](const Actor& target) {return actorEntity(sim,target.slot);});
    if(actorHit) {
        best=actorHit.event.fraction;
        victim=actorHit.pose->slot;
        pose=actorHit.pose;
    }
    // Vehicles use the same deterministic ray-candidate policy as Actors.
    // Check them after actors so an exact equal-distance hit retains the
    // original actor winner; nearer vehicles still occlude a character.
    uint32_t vehicleId=0;
    const auto vehicleHit=lib::genre::selectClosestHitscanCandidate(
        s.vehicles,eye,delta,best,
        [&](const Vehicle& v) {return v.health>0 && v.driver!=source;},
        [&](const Vehicle& v) {
            return world::Bounds{
                v.pos-Vec3{1_fx,{},2_fx},v.pos+Vec3{1_fx,2_fx,2_fx}};
        });
    if(vehicleHit) {
        best=vehicleHit.fraction;
        victim=0;
        vehicleId=vehicleHit.target->id;
    }
    auto penetration=projectile?projectile->penetration:weapon.penetration;bool blocked=false;
    Vec3 end=eye+delta*best;
    world::Bounds bounds{{fxMin(eye.x,end.x),fxMin(eye.y,end.y),fxMin(eye.z,end.z)},{fxMax(eye.x,end.x),fxMax(eye.y,end.y),fxMax(eye.z,end.z)}};
    for(auto id:collision_.query(bounds)) {
        auto hit=collision_.castCollider(id,eye,delta);const auto entry=hit.fraction;
        if(!hit.hit||entry>best)continue;
        bool passed=false;if(projectile)for(uint8_t i=0;i<projectile->piercedCount;++i)passed|=projectile->pierced[i]==id;
        if(passed)continue;
        auto material=penetrable_.find(id);
        if(config_.systems.enabled&&material!=penetrable_.end()&&penetration>=material->second&&(!projectile||projectile->piercedCount<8)){penetration-=material->second;amount=amount*3/4;if(projectile)projectile->pierced[projectile->piercedCount++]=id;}
        else {best=entry;victim=0;vehicleId=0;blocked=true;}
    }
    if(projectile){projectile->penetration=penetration;projectile->damage=amount;}
    auto point=eye+delta*best;sim.emit({1,source,{}, {},victim,point});
    if(vehicleId){for(auto& v:s.vehicles)if(v.id==vehicleId){v.health=std::max(0,v.health-amount);if(!v.health&&v.driver){auto& driver=s.actors[v.driver-1];v.driver=0;driver.vehicle=0;damage(sim,source,driver,1000);}}return true;}
    if(!victim)return blocked;
    bool head=false;
    if(config_.systems.enabled&&pose){auto height=stanceHeight(*pose,config_.motor);auto relative=point.y-pose->pos.y;head=relative>height*Fixed::fromRatio(4,5);if(head)amount=int32_t((Fixed::fromInt(amount)*weapon.headMultiplier).floorInt());else if(relative<height*Fixed::fromRatio(2,5))amount=int32_t((Fixed::fromInt(amount)*weapon.limbMultiplier).floorInt());}
    // Both RTS and FPS dispatch accepted direct hits through the same
    // ordered ECS impact policy. FPS remains responsible for all head/limb
    // modifiers, armor, knockdown, kill and replay side effects in damage().
    // An instantaneous ray has no splash candidates or deferred on-hit stage.
    const Entity victimEntity=actorEntity(sim,victim);
    lib::genre::resolveOrderedProjectileImpact(
        {kNullEntity,actorEntity(sim,source),victimEntity,point,
         Fixed::fromInt(amount),Fixed{}},
        [&](Entity) { damage(sim,source,s.actors[victim-1],amount,head); },
        [](Entity) {},
        [](Vec3,Fixed,auto&&) {},
        [](Entity) {return false;},
        [](Entity) {});
    return true;
}
void Module::action(Simulation& sim,Actor& a,Action kind,uint32_t argument) {
    auto& s=state(sim);const auto& c=config_.systems;auto now=sim.tick();
    if(a.health<=0||a.downed||a.activeAction!=Action::None)return;
    if(kind==Action::Buy) {
        if(config_.mode!="bomb"||s.match.end||now-s.match.start>c.buyTicks||s.match.planted||argument>=config_.weapons.size()||!near(a.pos,spawns_[a.slot-1],6_fx))return;
        const auto& w=config_.weapons[argument];if(a.money<w.price)return;
        a.money-=w.price;a.owned|=uint8_t(1u<<argument);a.storedAmmo[argument]=w.magazine;a.storedReserve[argument]=w.reserve;
        if(a.weaponIndex==argument){a.ammo=w.magazine;a.reserve=w.reserve;}return;
    }
    if(a.vehicle&&kind!=Action::Interact)return;
    if(kind==Action::Frag||kind==Action::Smoke||kind==Action::Flash) {
        if(s.projectiles.size()>=256||!bag(a,kind==Action::Frag?3:kind==Action::Smoke?4:5,-1))return;
        uint8_t grenade=kind==Action::Frag?1:kind==Action::Smoke?2:3;
        auto eye=a.pos+Vec3{{},stanceHeight(a,config_.motor)-Fixed::fromRatio(1,10),{}};
        s.projectiles.push_back({s.nextObject++,now+c.grenadeFuse,a.slot,grenade,eye,direction(a.yaw,a.pitch)*15_fx+Vec3{{},3_fx,{}},100,{},0});return;
    }
    if(kind==Action::Drop) {
        if(s.loot.size()>=256||!(a.owned&(1u<<a.weaponIndex)))return;
        s.loot.push_back({s.nextObject++,a.pos,100+a.weaponIndex,1,a.ammo,a.reserve,a.attachments[a.weaponIndex]});a.owned&=uint8_t(~(1u<<a.weaponIndex));a.ammo=a.reserve=0;a.attachments[a.weaponIndex]=0;a.reloadUntil=0;return;
    }
    if(kind==Action::Attach){if(argument<6||argument>8||!(a.owned&(1u<<a.weaponIndex)))return;auto bit=uint8_t(1u<<(argument-6));if(!(a.attachments[a.weaponIndex]&bit)&&bag(a,int32_t(argument),-1))a.attachments[a.weaponIndex]|=bit;return;}
    if(kind==Action::Vault) {
        if(!a.grounded||a.prone)return;
        auto dir=direction(a.yaw,{})*2_fx;auto wall=collision_.cast(a.pos+Vec3{{},Fixed::fromRatio(1,2),{}},dir);
        if(!wall.hit)return;
        auto top=collision_.colliders().at(wall.id).bounds.max.y;
        auto up=top-a.pos.y+Fixed::fromRatio(1,10);
        if(up<=Fixed{}||up>Fixed::fromRatio(15,10))return;
        auto height=stanceHeight(a,config_.motor);Vec3 center=a.pos+Vec3{{},height/2,{}},half{config_.motor.radius,height/2,config_.motor.radius};
        if(collision_.cast(center,{{},up,{}},half).hit||collision_.cast(center+Vec3{{},up,{}},dir,half).hit)return;
        a.pos+=Vec3{{},up,{}}+dir;a.velocity={};return;
    }
    if(a.activeAction!=Action::None)return;
    auto begin=[&](Action action,uint32_t duration,uint32_t target){a.activeAction=action;a.actionUntil=now+duration;a.actionTarget=target;a.actionOrigin=a.pos;};
    if(kind==Action::Heal){if(a.health<100&&has(a,1))begin(kind,c.healTicks,0);return;}
    if(kind==Action::Plant) {
        if(config_.mode!="bomb"||a.team!=1||s.match.carrier!=a.slot||s.match.planted||now<s.match.start+c.freezeTicks)return;
        for(auto site:sites_){
            if(inside(a.pos,site)){
                begin(kind,c.plantTicks,0);
                return;
            }
        }
        return;
    }
    if(kind!=Action::Interact)return;
    if(a.vehicle&&vehicleInteract(sim,a))return;
    if(a.vehicle){for(auto& v:s.vehicles)if(v.id==a.vehicle){Actor exit=a;exit.pos=v.pos+Vec3{2_fx,Fixed::fromRatio(1,10),{}};exit.vehicle=0;auto box=actorBounds(exit,config_.motor);box.min.y+=Fixed::fromRatio(1,100);if(collision_.blocked(box))return;a.pos=exit.pos;v.driver=0;a.vehicle=0;}return;}
    if(a.climbing){a.climbing=false;return;}
    auto accessible=[&](Vec3 p){return near(a.pos,p,c.interactRange)&&!collision_.cast(a.pos+Vec3{{},1_fx,{}},p-a.pos).hit;};
    if(config_.mode=="bomb") {
        if(s.match.planted&&a.team==2&&accessible(s.match.bombPos)){begin(Action::Interact,c.defuseTicks,0);return;}
        if(!s.match.planted&&!s.match.carrier&&a.team==1&&accessible(s.match.bombPos)){s.match.carrier=a.slot;return;}
    }
    for(auto& target:s.actors)if(target.downed&&target.team==a.team&&accessible(target.pos)){begin(Action::Interact,c.reviveTicks,target.slot);return;}
    for(auto it=s.loot.begin();it!=s.loot.end();++it)if(accessible(it->pos)) {
        if(it->type>=100&&it->type<200){auto index=uint32_t(it->type-100);if(a.owned&(1u<<index))continue;a.owned|=uint8_t(1u<<index);a.storedAmmo[index]=it->ammo;a.storedReserve[index]=it->reserve;a.attachments[index]=it->attachments;if(a.weaponIndex==index){a.ammo=it->ammo;a.reserve=it->reserve;}}
        else if(it->type==2){a.armor=100;}
        else if(it->type==9){a.reserve=std::min(100000,a.reserve+30*it->count);}
        else if(it->type>=200){
            if(s.gear.empty())continue;auto& loadout=s.gear[a.slot-1];
            lib::systems::GearPiece piece{it->type-200,it->ammo};
            auto previous=lib::systems::equipGear(config_.gear,loadout,piece);if(!previous)continue;
            if(previous->def){it->type=int32_t(200+previous->def);it->ammo=int32_t(previous->durability);it->count=1;return;}   // swap in place
        }
        else if(!bag(a,it->type,it->count,size_t(std::min<int64_t>(24,12+(s.gear.empty()?0:lib::systems::gearCapacity(config_.gear,s.gear[a.slot-1]))))))continue;
        s.loot.erase(it);return;
    }
    if(vehicleInteract(sim,a))return;
    for(auto& v:s.vehicles)if(v.health>0&&!v.driver&&!vehicleRun(s,v.id)&&accessible(v.pos)){v.driver=a.slot;a.vehicle=v.id;a.pos=v.pos;return;}
    for(auto ladder:ladders_)if(inside(a.pos,ladder)){a.climbing=true;return;}
}
void Module::actorSystems(Simulation& sim,Actor& a,Input& input) {
    auto& s=state(sim);auto now=sim.tick();const auto& c=config_.systems;
    a.recoil=fxMax(Fixed{},a.recoil-Fixed::fromRatio(1,config_.tickRate*5));a.aiming=(input.buttons&Aim)!=0;
    if(a.downed&&now>=a.bleedUntil){damage(sim,0,a,1000);input={};return;}
    bool frozen=config_.mode=="bomb"&&(s.match.end||now<s.match.start+c.freezeTicks);
    a.frozen=frozen;
    if(input.action!=Action::None&&(!frozen||input.action==Action::Buy))action(sim,a,input.action,input.argument);
    a.input.action=Action::None;
    if(a.activeAction!=Action::None) {
        bool valid=near(a.pos,a.actionOrigin,Fixed::fromRatio(1,2))&&!(input.buttons&Fire)&&!input.forward&&!input.right;
        if(a.activeAction==Action::Heal)valid&=has(a,1)&&a.health<100;
        if(a.activeAction==Action::Plant)valid&=!s.match.planted&&s.match.carrier==a.slot;
        if(a.activeAction==Action::Interact&&a.actionTarget){const auto& target=s.actors[a.actionTarget-1];valid&=target.downed&&target.health>0&&near(a.pos,target.pos,c.interactRange)&&!collision_.cast(a.pos+Vec3{{},1_fx,{}},target.pos-a.pos).hit;}
        if(a.activeAction==Action::Interact&&!a.actionTarget)valid&=s.match.planted&&near(a.pos,s.match.bombPos,c.interactRange);
        if(!valid){a.activeAction=Action::None;a.actionUntil=0;}
        else if(now>=a.actionUntil) {
            if(a.activeAction==Action::Heal&&bag(a,1,-1))a.health=std::min(100,a.health+60);
            if(a.activeAction==Action::Plant){s.match.planted=true;s.match.bombPos=a.pos;s.match.carrier=0;s.match.bombExplodes=now+c.bombTicks;}
            if(a.activeAction==Action::Interact){if(a.actionTarget){auto& target=s.actors[a.actionTarget-1];target.downed=false;target.health=40;target.bleedUntil=0;}else finishRound(sim,2);}
            a.activeAction=Action::None;a.actionUntil=0;
        }
    }
    if(a.climbing){bool in=false;for(auto ladder:ladders_)in|=inside(a.pos,ladder);if(!in)a.climbing=false;}
    if(a.vehicle&&!vehicleRun(s,a.vehicle))for(auto& v:s.vehicles)if(v.id==a.vehicle) {
        if(!frozen){v.yaw+=Fixed::fromRatio(input.right,1000)*sim.dt()/4;auto delta=direction(v.yaw,{})*v.speed*Fixed::fromRatio(input.forward,1000)*sim.dt();auto hit=collision_.cast(v.pos+Vec3{{},1_fx,{}},delta,{1_fx,1_fx,2_fx});v.pos+=delta*(hit.hit?fxMax(Fixed{},hit.fraction-Fixed::fromRatio(1,100)):1_fx);v.pos.x=fxClamp(v.pos.x,-config_.worldLimit,config_.worldLimit);v.pos.z=fxClamp(v.pos.z,-config_.worldLimit,config_.worldLimit);v.velocity=delta/sim.dt();}
        a.pos=v.pos+Vec3{{},1_fx,{}};a.velocity=v.velocity;
    }
    if(frozen||a.downed||a.vehicle){if(frozen||a.downed||!canFireFromVehicle(s,a))input.buttons&=uint32_t(Prone);else input.buttons&=uint32_t(Fire|Aim|Reload|WeaponMask);if(frozen||a.vehicle)input.forward=input.right=0;}   // passengers / gunners may shoot
}
void Module::finishRound(Simulation& sim,int32_t team) {
    auto& s=state(sim);if(s.match.end)return;s.match.winner=team;s.match.end=sim.tick()+config_.tickRate*3;
    if(team==1)++s.match.scoreA;else if(team==2)++s.match.scoreB;
    for(auto& a:s.actors){a.money=std::min(16000,a.money+(a.team==team?config_.systems.winReward:config_.systems.lossReward));a.activeAction=Action::None;a.actionUntil=0;}
    if(s.match.scoreA>=int(config_.systems.roundsToWin)||s.match.scoreB>=int(config_.systems.roundsToWin))s.winner=team;
}
// The existing non-programmed flight/impact behavior is one deterministic
// policy, regardless of whether the shot is owned by the legacy compatibility
// array or by a canonical ProjectileRuntime ECS component.
bool Module::stepOrdinaryProjectile(Simulation& sim,Projectile& p) {
    auto& s=state(sim);
    const auto now=sim.tick();
    const auto& c=config_.systems;
    bool remove=false;
    if(p.kind==0) {
        auto delta=p.velocity*sim.dt();
        auto length=delta.length();
        if(length>p.remaining&&length>Fixed{})
            delta=delta*(p.remaining/length);
        remove=traceShot(sim,p.owner,p.pos,delta,p.damage,
                         config_.weapons[p.weapon],s.actors,&p);
        p.pos+=delta;
        p.remaining-=length;
        p.velocity.y-=config_.weapons[p.weapon].bulletGravity*sim.dt();
        remove|=p.remaining<=Fixed{}||now>=p.expires;
    } else if(p.kind==4) {
        remove=now>=p.expires;
    } else if(now<p.expires) {
        // Physical bounce is a shared policy; fuse, blast, smoke and flash
        // still belong to the independent gameplay effect stages below.
        // The default config reproduces the exact previous fixed-point
        // gravity/cast/contact-inset/reflection/division sequence.
        lib::genre::stepBouncingProjectile(p.pos,p.velocity,
                                            collision_,sim.dt());
    } else {
        if(p.kind==2) {
            p.kind=4;
            p.expires=now+c.smokeTicks;
            p.velocity={};
        } else {
            for(auto& target:s.actors)if(target.health>0) {
                auto eye=target.pos+Vec3{{},stanceHeight(target,config_.motor)
                                                  -Fixed::fromRatio(1,10),{}};
                auto delta=eye-p.pos;
                auto distance=delta.length();
                auto radius=p.kind==1?c.blastRadius:c.flashRadius;
                if(distance>radius||collision_.cast(p.pos,delta).hit)continue;
                if(p.kind==1)
                    damage(sim,p.owner,target,
                           int32_t((100_fx*(1_fx-distance/radius)).floorInt()));
                else {
                    auto facing=direction(target.yaw,target.pitch).dot(Vec3{}-delta);
                    auto duration=facing>Fixed{}?c.flashTicks:c.flashTicks/4;
                    target.blindUntil=std::max(target.blindUntil,now+duration);
                }
            }
            remove=true;
        }
    }
    remove|=fxAbs(p.pos.x)>config_.worldLimit||
            fxAbs(p.pos.y)>config_.worldLimit||
            fxAbs(p.pos.z)>config_.worldLimit;
    return remove;
}

void Module::tickOrdinaryProjectiles(Simulation& sim) {
    auto& current=state(sim);
    if(!config_.ecsAuthority) {
        // Keep the default OFF path exactly in its old vector order.
        for(auto it=current.projectiles.begin();it!=current.projectiles.end();) {
            if(it->program.enabled) {++it;continue;}
            if(stepOrdinaryProjectile(sim,*it))
                it=current.projectiles.erase(it);
            else ++it;
        }
        return;
    }

    // On the authority path every ordinary projectile is advanced directly
    // in its writable ECS payload. The State vector is a read projection
    // between this barrier and the main FPS round/actor stage.
    World& world=sim.world();
    auto* index=world.resource<DynamicEntityIndex>();
    if(!index)
        throw std::logic_error("FPS DynamicEntityIndex missing in ordinary projectile stage");
    struct Ordered {Entity entity;uint32_t order,id;};
    std::vector<Ordered> ordered;
    world.eachExistingConst<ProjectileRuntime>(
        [&](Entity entity,const ProjectileRuntime& runtime) {
            if(world.alive(entity))
                ordered.push_back({entity,runtime.order,runtime.value.id});
        });
    std::sort(ordered.begin(),ordered.end(),[](const Ordered& a,const Ordered& b) {
        return a.order!=b.order?a.order<b.order:a.id<b.id;
    });
    std::vector<std::pair<Entity,uint32_t>> expired;
    for(const auto& item:ordered) {
        auto* runtime=world.tryGet<ProjectileRuntime>(item.entity);
        if(!runtime)
            throw std::logic_error("FPS ordinary projectile runtime vanished");
        if(runtime->value.program.enabled)continue;
        if(stepOrdinaryProjectile(sim,runtime->value))
            expired.emplace_back(item.entity,item.id);
    }
    // Defer structural changes until the entire source ordering is traversed.
    for(const auto& [entity,id]:expired) {
        world.destroy(entity);
        index->byKey.erase((uint64_t(1)<<32)|id);
    }
    current.projectiles.clear();
    current.projectiles.reserve(ordered.size()-expired.size());
    uint32_t position=0;
    for(const auto& item:ordered) {
        auto* runtime=world.tryGet<ProjectileRuntime>(item.entity);
        if(!runtime)continue;
        runtime->order=position++;
        current.projectiles.push_back(runtime->value);
    }
}

void Module::tickSystems(Simulation& sim) {
    auto& s=state(sim);auto now=sim.tick();const auto& c=config_.systems;
    if(config_.mode!="bomb")return;
    if(s.match.end) {
        if(now<s.match.end)return;
        int scoreA=s.match.scoreA,scoreB=s.match.scoreB;s.match={};s.match.start=now;s.match.scoreA=scoreA;s.match.scoreB=scoreB;s.match.carrier=0;++s.round;
        for(auto& a:s.actors){auto slot=a.slot,team=a.team;auto money=a.money,kills=a.kills;auto life=a.life,sequence=a.lastSequence;a=Actor{};a.slot=slot;a.team=team;a.life=life+1;a.money=money;a.kills=kills;a.lastSequence=sequence;a.pos=spawns_[slot-1];equipInitial(a);a.armor=c.startArmor;if(team==1&&!s.match.carrier)s.match.carrier=slot;}
        s.projectiles.clear();s.loot=initialLoot_;s.vehicles=initialVehicles_;s.history.clear();return;
    }
    if(now<s.match.start+c.freezeTicks)return;
    bool attack=false,defense=false;for(const auto& a:s.actors)if(a.health>0&&!a.downed){attack|=a.team==1;defense|=a.team==2;}
    if(s.match.planted&&now>=s.match.bombExplodes)finishRound(sim,1);
    else if(!defense)finishRound(sim,1);
    else if(!attack&&!s.match.planted)finishRound(sim,2);
    else if(!s.match.planted&&now-s.match.start>=config_.roundTicks)finishRound(sim,2);
}
}
