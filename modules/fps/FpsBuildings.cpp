// Map `buildings` (building-gen package): enterable boxes with floors, doors, windows and switchback stairs
// generated at setup into static collision (ids from kBuildingColliderBase) plus loot on the generated spots.
// {"buildings":[{"pos":[x,y,z],"yaw":0,"seed":1,"building":{...BuildingDef JSON...},"loot":[1,2,101]}]}
#include "FpsModule.h"
namespace aiverse::fps {
std::vector<Loot> Module::buildMapBuildings(const Json& map) {
    std::vector<Loot> loot;const auto& list=map["buildings"];
    if(list.isNull())return loot;
    if(!list.isArray()||list.size()>512)throw JsonError("fps.buildings: expected at most 512 buildings");
    uint32_t next=kBuildingColliderBase;
    for(size_t i=0;i<list.size();++i){
        auto path="fps.buildings["+std::to_string(i)+"]";const auto& b=list[i];
        if(!b.isObject())throw JsonError(path+": expected object");
        for(const auto& [k,v]:b.members()){(void)v;if(k!="pos"&&k!="yaw"&&k!="seed"&&k!="building"&&k!="loot")throw JsonError(path+": unknown key '"+k+"'");}
        if(!b["pos"].isArray()||b["pos"].size()!=3)throw JsonError(path+".pos: expected [x,y,z]");
        Vec3 origin{b["pos"][size_t(0)].fx(),b["pos"][size_t(1)].fx(),b["pos"][size_t(2)].fx()};
        auto yaw=b.getInt("yaw",0),seed=b.getInt("seed",int64_t(i+1));
        if(yaw<0||yaw>3)throw JsonError(path+".yaw: expected quarter turns 0..3");
        auto def=b.has("building")?lib::systems::readBuildingDef(b["building"],path+".building"):lib::systems::BuildingDef{};
        def.agentRadius=config_.motor.radius;def.agentHeight=config_.motor.height;def.maxStepRise=fxMin(def.maxStepRise,config_.motor.stepHeight);
        auto layout=lib::systems::generateBuilding(def,uint64_t(seed),origin,uint32_t(yaw));
        if(!layout)throw JsonError(path+": building does not fit the motor (step height, radius, height) or its footprint");
        for(const auto& box:layout->boxes){
            for(auto c:{box.min.x,box.min.z,box.max.x,box.max.z})if(fxAbs(c)>config_.worldLimit)throw JsonError(path+": outside world limit");
            if(!collision_.add({next++,{box.min,box.max}}))throw JsonError(path+": invalid generated box");
        }
        if(b.has("loot")){
            const auto& items=b["loot"];if(!items.isArray()||items.size()>64)throw JsonError(path+".loot: expected at most 64 item types");
            Random g(uint64_t(seed),0x100f);auto spots=layout->lootSpots;
            for(size_t k=0;k<items.size();++k){
                auto type=items[k].integer(0);
                if(!((type>=1&&type<=9)||(type>=100&&type<100+int64_t(config_.weapons.size()))||(type>=200&&config_.gear.contains(type-200))))throw JsonError(path+".loot["+std::to_string(k)+"]: invalid item type");
                auto spot=spots[g.below(uint32_t(spots.size()))]+Vec3{(g.unit()-Fixed::fromRatio(1,2)),Fixed::fromRatio(1,100),(g.unit()-Fixed::fromRatio(1,2))};
                Loot l{0,spot,int32_t(type),1};
                if(type>=200)l.ammo=int32_t(config_.gear.at(type-200).durability);
                else if(type>=100){l.ammo=config_.weapons[size_t(type-100)].magazine;l.reserve=config_.weapons[size_t(type-100)].reserve;}
                loot.push_back(l);
            }
        }
    }
    return loot;
}
}
