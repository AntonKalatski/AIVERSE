// FPS projectile collision/damage adapter with a single deterministic solver
// invoked from either direct ProjectileRuntime ECS ownership or legacy vectors.
#include "FpsModule.h"
#include "libraries/include/aiverselib/genre/EcsSweepQuerySystems.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace aiverse::fps {

// Shared per-projectile implementation. No gameplay state is mirrored into
// another projectile owner while this function runs. All direct effects
// (damage, shields, loot, hit events, scripted bullet branches) execute at
// the original time, before the next projectile is advanced.
bool Module::stepProgrammedProjectile(Simulation& sim,Projectile& p,
                                     std::vector<Projectile>& children,
                                     std::size_t liveCount) {
    auto& state_=state(sim);
    const Vec3 up{Fixed{},1_fx,Fixed{}};
    auto& s=p.program;
        const auto& weapon=config_.weapons[p.weapon];const auto& f=weapon.flight;
        bool alive=s.distanceLeft>Fixed{}&&s.lifetimeLeft>Fixed{};
        struct Candidate {Entity id;Vec3 centre;world::Bounds bounds;};
        auto candidates=[&](){
            std::vector<Candidate> out;
            for(const auto& actor:state_.actors){
                const Entity id{actor.slot,actor.life};
                if(actor.slot==p.owner||actor.health<=0||projectile::seen(s,id))continue;
                if(!config_.friendlyFire&&actor.team==state_.actors[p.owner-1].team)continue;
                auto bounds=actorBounds(actor,config_.motor);
                out.push_back({id,(bounds.min+bounds.max)/2_fx,bounds});
            }
            for(const auto& v:state_.vehicles)if(v.health>0&&v.driver!=p.owner){
                const Entity id{201u+v.id,0};
                if(!projectile::seen(s,id))out.push_back({id,v.pos+up, {v.pos-Vec3{1_fx,Fixed{},2_fx},v.pos+Vec3{1_fx,2_fx,2_fx}}});
            }
            std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.id.index<b.id.index;});
            return out;
        };
        auto spawn=[&](projectile::Branch branch){
            if(liveCount+children.size()>=256)return;
            Projectile child=p;child.id=state_.nextObject++;
            child.program=std::move(branch.state);child.programSpeed=branch.speed;child.programTarget=branch.target;
            child.velocity=child.program.direction*child.programSpeed+up*child.program.verticalSpeed;
            child.remaining=child.program.distanceLeft;
            children.push_back(std::move(child));
        };
        auto split=[&](size_t i){projectile::split(s,p.programSpeed,f,i,up,spawn);};
        Fixed time=fxMin(sim.dt(),s.lifetimeLeft);
        while(alive&&time>Fixed{}) {
            Fixed dt=projectile::sliceTime(s,p.programSpeed,time,s.ballistic);
            for(size_t i=0;i<s.cursor;++i){const auto& m=f.modifiers[i];
                if(m.kind!=projectile::ProjectileModKind::Split||!m.timed||(s.consumed&(1u<<i)))continue;
                if(s.age>=m.after)split(i);else dt=fxMin(dt,m.after-s.age);
            }
            auto targets=candidates();
            if(f.homing)for(const auto& t:targets)if(t.id==p.programTarget){
                auto heading=fxNormalize(t.centre-p.pos);if(heading.lengthSq()>Fixed{})s.direction=heading;
            }
            Vec3 delta=projectile::displacement(s,p.programSpeed,up,dt);
            Fixed length=projectile::magnitude(delta);
            if(length>s.distanceLeft){auto ratio=s.distanceLeft/length;delta=delta*ratio;dt*=ratio;length=s.distanceLeft;}
            Fixed fraction=1_fx;std::optional<Candidate> victim;
            const Vec3 extent{s.radius,s.radius,s.radius};
            // The same sweep query now serves FPS physical/projectile-program
            // flight and any other native genre. Endpoint hits and stable
            // target-ID ties preserve the original resolver semantics.
            const auto swept=lib::genre::selectSweptProjectileContact(
                targets,p.pos,delta,extent,
                [](const Candidate& t) {return t.bounds;},
                [](const Candidate& a,const Candidate& b) {
                    return a.id.index<b.id.index;
                });
            if(swept) {fraction=swept.fraction;victim=*swept.target;}
            uint32_t wall=0;
            if(f.blockTerrain){
                const auto end=p.pos+delta;
                world::Bounds bounds{{fxMin(p.pos.x,end.x),fxMin(p.pos.y,end.y),fxMin(p.pos.z,end.z)},
                                     {fxMax(p.pos.x,end.x),fxMax(p.pos.y,end.y),fxMax(p.pos.z,end.z)}};
                bounds.min=bounds.min-extent;bounds.max+=extent;
                for(auto id:collision_.query(bounds)){
                    bool passed=false;for(uint8_t i=0;i<p.piercedCount;++i)passed|=p.pierced[i]==id;
                    if(passed)continue;
                    auto hit=collision_.castCollider(id,p.pos,delta,extent);
                    if(hit.hit&&hit.fraction<=fraction&&(!wall||hit.fraction<fraction||id<wall)){
                        fraction=hit.fraction;wall=id;victim.reset();
                    }
                }
            }
            const Fixed elapsed=dt*fraction;
            p.pos+=delta*fraction;s.verticalSpeed-=s.gravity*elapsed;
            s.distanceLeft=fxMax(Fixed{},s.distanceLeft-length*fraction);
            s.lifetimeLeft=fxMax(Fixed{},s.lifetimeLeft-elapsed);s.age+=elapsed;time-=elapsed;
            if(wall){
                auto material=penetrable_.find(wall);
                if(material!=penetrable_.end()&&p.penetration>=material->second&&p.piercedCount<8){
                    p.penetration-=material->second;p.damage=p.damage*3/4;p.pierced[p.piercedCount++]=wall;
                }else alive=false;
            }else if(victim){
                if(s.hit.size()>=256){alive=false;break;}
                s.hit.push_back(victim->id);
                const Fixed scale=s.damageScale;
                auto branch=projectile::contact(s,p.programSpeed,f,split,[&](Fixed radius)->std::optional<projectile::Target>{
                    std::optional<projectile::Target> result;Fixed best=radius;
                    for(const auto& t:candidates()){
                        auto offset=t.centre-p.pos;auto distance=offset.length();
                        if(distance>best)continue;
                        if(f.blockTerrain&&collision_.cast(p.pos,offset,extent).hit)continue;
                        if(!result||distance<best||(distance==best&&t.id.index<result->id.index)){
                            best=distance;result=projectile::Target{t.id,fxNormalize(offset)};
                        }
                    }
                    return result;
                });
                if(branch)spawn(std::move(*branch));
                int32_t amount=int32_t((Fixed::fromInt(p.damage)*scale).floorInt());
                if(victim->id.gen){
                    auto& actor=state_.actors[victim->id.index-1];bool head=false;
                    if(config_.systems.enabled){
                        auto relative=p.pos.y-actor.pos.y;auto height=stanceHeight(actor,config_.motor);
                        head=relative>height*Fixed::fromRatio(4,5);
                        if(head)amount=int32_t((Fixed::fromInt(amount)*weapon.headMultiplier).floorInt());
                        else if(relative<height*Fixed::fromRatio(2,5))amount=int32_t((Fixed::fromInt(amount)*weapon.limbMultiplier).floorInt());
                    }
                    damage(sim,p.owner,actor,amount,head);
                }else for(auto& vehicle:state_.vehicles)if(vehicle.id+201u==victim->id.index){
                    vehicle.health=std::max(0,vehicle.health-amount);
                    if(!vehicle.health&&vehicle.driver){auto& driver=state_.actors[vehicle.driver-1];vehicle.driver=0;driver.vehicle=0;damage(sim,p.owner,driver,1000);}
                }
                sim.emit({1,p.owner,{}, {},int32_t(victim->id.index),p.pos});
                alive=!branch&&projectile::pass(s,f);
            }
            alive=alive&&s.distanceLeft>Fixed{}&&s.lifetimeLeft>Fixed{}&&
                fxAbs(p.pos.x)<=config_.worldLimit&&fxAbs(p.pos.y)<=config_.worldLimit&&fxAbs(p.pos.z)<=config_.worldLimit;
            if(alive)for(size_t i=0;i<s.cursor;++i){const auto& m=f.modifiers[i];
                if(m.kind==projectile::ProjectileModKind::Split&&m.timed&&!(s.consumed&(1u<<i))&&s.age>=m.after)split(i);
            }
        }
        p.velocity=s.direction*p.programSpeed+up*s.verticalSpeed;p.remaining=s.distanceLeft;
    return alive;
}

void Module::tickProjectilePrograms(Simulation& sim) {
    auto& state_=state(sim);
    std::vector<Projectile> children;

    if(!config_.ecsAuthority) {
        // Legacy default keeps the original source vector and immediate
        // erase/append ordering; no changes to save v9 or game mode.
        for(auto it=state_.projectiles.begin();it!=state_.projectiles.end();) {
            if(!it->program.enabled){++it;continue;}
            const bool alive=stepProgrammedProjectile(
                sim,*it,children,state_.projectiles.size());
            if(!alive)it=state_.projectiles.erase(it);else ++it;
        }
        for(auto& child:children)state_.projectiles.push_back(std::move(child));
        return;
    }

    // ECS-authoritative path: iteration and writable projectile state are
    // sourced from ProjectileRuntime components, never from State vectors.
    // Source order is explicit and preserved across pool swap-remove/COW.
    World& world=sim.world();
    auto* index=world.resource<DynamicEntityIndex>();
    if(!index)throw std::logic_error("FPS DynamicEntityIndex missing in ECS projectile stage");

    struct OrderedProjectile {Entity entity;uint32_t order,id;};
    std::vector<OrderedProjectile> ordered;
    world.eachExistingConst<ProjectileRuntime>(
        [&](Entity e,const ProjectileRuntime& runtime) {
            if(world.alive(e))
                ordered.push_back({e,runtime.order,runtime.value.id});
        });
    auto less=[](const OrderedProjectile& a,const OrderedProjectile& b) {
        return a.order!=b.order ? a.order<b.order : a.id<b.id;
    };
    std::sort(ordered.begin(),ordered.end(),less);

    std::size_t liveCount=ordered.size();
    std::vector<std::pair<Entity,uint32_t>> expired;
    for(const auto& item:ordered) {
        auto* runtime=world.tryGet<ProjectileRuntime>(item.entity);
        if(!runtime)throw std::logic_error("FPS projectile runtime vanished during update");
        if(!runtime->value.program.enabled)continue;
        if(!stepProgrammedProjectile(sim,runtime->value,children,liveCount)) {
            expired.emplace_back(item.entity,item.id);
            --liveCount; // legacy erase happens before processing next shot
        }
    }

    // Structural mutations take place only after the stable source traversal.
    // Remove old IDs before creating children; free slots may be recycled.
    for(const auto& [entity,id]:expired) {
        world.destroy(entity);
        index->byKey.erase((uint64_t(1)<<32)|id);
    }

    // State::projectiles is a read/compatibility projection after this point:
    // grenade/round-mode logic is not yet migrated and can still add/remove
    // objects during the main FPS tick. Its writes are reconciled at mirror.
    state_.projectiles.clear();
    state_.projectiles.reserve(liveCount+children.size());
    uint32_t position=0;
    for(const auto& item:ordered) {
        auto* runtime=world.tryGet<ProjectileRuntime>(item.entity);
        if(!runtime)continue; // removed by the preceding structural barrier
        runtime->order=position++;
        state_.projectiles.push_back(runtime->value);
    }
    for(auto& child:children) {
        const uint32_t id=child.id;
        const uint64_t key=(uint64_t(1)<<32)|id;
        if(!id || (index->byKey.contains(key) &&
                    world.alive(index->byKey.at(key))))
            throw std::logic_error("FPS duplicate ECS spawned projectile ID");
        const Entity e=world.create();
        world.add<ProjectileRuntime>(e,ProjectileRuntime{std::move(child),position++});
        index->byKey[key]=e;
        state_.projectiles.push_back(world.get<ProjectileRuntime>(e).value);
    }
}
}
