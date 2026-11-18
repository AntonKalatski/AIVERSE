// Portable native ECS save/hash schema upgrade, rollback and simulated peer parity.
#include "engine/sim/Simulation.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace custom {
struct Ammo {int32_t current=30,reserve=90;};
struct Funds {int64_t value=100;};
void io(Archive& a,Ammo& v){a(v.current);if(a.formatVersion>=2)a(v.reserve);}
void io(Archive& a,Funds& v){a(v.value);}
}
using namespace custom;
static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
struct Host final:GameModule {
    bool native=true,reverse=false,migrate=false;uint32_t version=1;Entity entity;
    Host(bool enabled=true,bool reversed=false,uint32_t v=1,bool m=false):
        native(enabled),reverse(reversed),migrate(m),version(v){}
    std::string name()const override{return "native-persistence";}
    void setup(Simulation& s,const Json&,const Json&)override{
        if(native){
            if(reverse){
                s.registerPersistentResource<Funds>("project.funds",1);
                s.registerPersistentComponent<Ammo>("project.ammo",version);
            }else{
                s.registerPersistentComponent<Ammo>("project.ammo",version);
                s.registerPersistentResource<Funds>("project.funds",1);
            }
            if(migrate) {
                s.registerPersistentComponentMigration<Ammo>("project.ammo",1,
                    [](Archive& a,Ammo& dst){a(dst.current);dst.reserve=44;});
            }
        }
        entity=s.world().create();
        if(native){s.world().add<Ammo>(entity,Ammo{30,90});s.world().setResource<Funds>(Funds{100});}
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
static std::unique_ptr<Simulation> make(bool native=true,bool reverse=false,
                                         uint32_t v=1,bool migrate=false){
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<Host>(native,reverse,v,migrate));
    sim->setup(Json::object(),Json::object());return sim;
}
int main(){
    auto s=make(),reverse=make(true,true);
    check(s->serialize()==reverse->serialize(),"registration order changed bytes");
    check(s->checksum()==reverse->checksum(),"registration order changed hash");
    auto legacy=make(false),legacyBytes=legacy->serialize();
    check(uint8_t(legacyBytes[4])==9,"legacy format changed");
    check(uint8_t(s->serialize()[4])==10,"native save not version 10");
    Entity e=static_cast<Host&>(s->module()).entity;
    s->world().get<Ammo>(e).current=7;
    s->world().resource<Funds>()->value=555;
    std::string save=s->serialize();const uint64_t digest=s->checksum();
    std::string error;
    auto loaded=make(true,true);
    check(loaded->deserialize(save,&error),"native load: "+error);
    check(loaded->checksum()==digest,"native checksum mismatch");
    check(loaded->world().get<Ammo>(e).current==7,"component value lost");
    check(loaded->world().resource<Funds>()->value==555,"resource value lost");
    // Verified v1->v2 upgrade. The stored source checksum must match the old
    // schema/bytes BEFORE accepting the migrated component with new defaults.
    auto upgrade=make(true,false,2,true);
    check(upgrade->deserialize(save,&error),"explicit schema migration failed: "+error);
    check(upgrade->world().get<Ammo>(e).current==7 &&
          upgrade->world().get<Ammo>(e).reserve==44,"migration value wrong");
    auto modern=make(true,true,2);
    check(modern->deserialize(upgrade->serialize(),&error),
          "re-saved migrated state cannot load: "+error);
    check(modern->checksum()==upgrade->checksum(),"migrated re-save checksum mismatch");
    auto unsupported=make(true,false,2);
    check(!unsupported->deserialize(save,&error),"accepted version without migration");
    check(error.find("schema")!=std::string::npos,"schema error absent");

    s->world().enableCowSnapshots();SimSnapshot snap;s->saveState(snap);
    s->world().get<Ammo>(e).current=1;
    s->world().resource<Funds>()->value=2;
    s->loadState(snap);
    check(s->checksum()==digest,"COW rollback lost custom ECS");
    // Two independent simulated peers: identical inputs/ticks must produce the
    // same hashes, and rollback restores parity after an intentional desync.
    auto peerA=make(true,false,2),peerB=make(true,true,2);
    Entity p=static_cast<Host&>(peerA->module()).entity;
    for(int n=0;n<8;++n) {
        peerA->world().get<Ammo>(p).current-=1;
        peerB->world().get<Ammo>(p).current-=1;
        peerA->step();peerB->step();
        check(peerA->checksum()==peerB->checksum(),"peer checksum diverged");
    }
    SimSnapshot healthy;peerB->saveState(healthy);
    peerB->world().get<Ammo>(p).current+=3;
    check(peerA->checksum()!=peerB->checksum(),"desync not detected");
    peerB->loadState(healthy);
    check(peerA->checksum()==peerB->checksum(),"rollback peer checksum mismatch");

    // Failed loads must not mutate Simulation-managed state.
    auto victim=make();
    auto before=victim->serialize(),originalHash=victim->checksum();
    auto broken=save.substr(0,save.size()-5);
    check(!victim->deserialize(broken,&error),"accepted truncated state");
    check(victim->checksum()==originalHash && victim->serialize()==before,
          "truncated load damaged live state");
    check(!victim->deserialize(legacyBytes,&error),"accepted v9 without native data");
    check(victim->checksum()==originalHash,"bad v9 load mutated state");

    s->world().destroy(e);Entity reused=s->world().create();
    check(reused.index==e.index && reused.gen!=e.gen,"entity generation not incremented");
    std::cout<<"PASS native ECS v9/v10, migration with original hash, COW, simulated peer parity, failed-load rollback\n";
}
