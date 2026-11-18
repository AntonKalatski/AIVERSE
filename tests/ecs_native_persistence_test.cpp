// Native ECS registration is independent of genre module and C++ type discovery order.
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
void io(Archive& a,Ammo& v){a(v.current,v.reserve);}
void io(Archive& a,Funds& v){a(v.value);}
}
using namespace custom;
static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
struct Host final:GameModule {
    bool native=true,reverse=false;uint32_t version=1;Entity entity;
    Host(bool enabled=true,bool reversed=false,uint32_t v=1):native(enabled),reverse(reversed),version(v){}
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
        }
        entity=s.world().create();
        if(native){s.world().add<Ammo>(entity,Ammo{30,90});s.world().setResource<Funds>(Funds{100});}
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
static std::unique_ptr<Simulation> make(bool native=true,bool reverse=false,uint32_t v=1){
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<Host>(native,reverse,v));
    sim->setup(Json::object(),Json::object());
    return sim;
}
int main(){
    auto s=make(),reverse=make(true,true);
    check(s->serialize()==reverse->serialize(),"registration ordering changed bytes");
    check(s->checksum()==reverse->checksum(),"registration ordering changed hash");
    auto legacy=make(false);
    auto legacyBytes=legacy->serialize();
    check(uint8_t(legacyBytes[4])==9,"legacy format changed");
    check(uint8_t(s->serialize()[4])==10,"native format not v10");
    Entity entity=static_cast<Host&>(s->module()).entity;
    s->world().get<Ammo>(entity).current=7;
    s->world().resource<Funds>()->value=555;
    auto save=s->serialize();const uint64_t checksum=s->checksum();
    auto loaded=make(true,true);std::string err;
    check(loaded->deserialize(save,&err),"native load: "+err);
    check(loaded->checksum()==checksum,"native checksum mismatch");
    check(loaded->world().get<Ammo>(entity).current==7,"component value lost");
    check(loaded->world().resource<Funds>()->value==555,"resource value lost");
    s->world().enableCowSnapshots();SimSnapshot snapshot;s->saveState(snapshot);
    s->world().get<Ammo>(entity).current=1;
    s->world().resource<Funds>()->value=2;
    s->loadState(snapshot);
    check(s->checksum()==checksum,"COW snapshot lost custom ECS values");
    auto wrong=make(true,false,2);
    check(!wrong->deserialize(save,&err),"accepted wrong schema version");
    check(err.find("schema")!=std::string::npos,"schema mismatch lacks diagnostics");
    check(!make()->deserialize(legacyBytes,&err),"accepted legacy bytes with missing native pools");
    auto broken=make();auto truncated=save.substr(0,save.size()-5);
    check(!broken->deserialize(truncated,&err),"accepted truncated save");
    s->world().destroy(entity);Entity reused=s->world().create();
    check(reused.index==entity.index&&reused.gen!=entity.gen,"entity generation not incremented");
    std::cout<<"PASS registered ECS save/hash/schema, legacy v9, COW and generations\n";
}
