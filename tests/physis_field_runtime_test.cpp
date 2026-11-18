#include "engine/physis/PhysisFieldRuntime.h"
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures=0;
#define CHECK(e) do { if(!(e)){std::printf("PHYSIS RUNTIME FAIL %s:%d: %s\n",__FILE__,__LINE__,#e);++failures;} } while(0)
constexpr uint16_t kFieldEditCmd=53123;

Json setupMap() {
    return Json::parse(R"({
      "physis":{
        "enabled":true,
        "grid":{"originX":0,"originZ":0,"width":2,"height":2,
                "cellSizeMeters":128,"seed":9},
        "climate":{"baseTemperature":17,"baseHumidity":0.5},
        "enableWindZones":true,"enableGravityFields":true,
        "enableStorms":true,"floodMode":"ExternalAuthority",
        "windZones":[
          {"id":"wind-a","center":[0,0,0],
           "radiusMeters":90,"velocity":[5,0,1]},
          {"id":"wind-b","center":[100,0,50],
           "radiusMeters":80,"velocity":[0,0,3]}
        ],
        "storms":[
          {"id":"storm-c","kind":"Tornado","center":[20,0,10],
           "eyeRadiusMeters":15,"outerRadiusMeters":180,
           "heightMeters":1000,"driftPerTick":[1,0,0]}
        ],
        "gravityFields":[
          {"id":"gravity-d","mode":"Attract","center":[0,80,0],
           "radiusMeters":200,"radialMagnitude":12}
        ]
      }
    })");
}
class EditableHost final:public GameModule {
public:
    std::string name() const override {return "physis-authoritative-test";}
    void setup(Simulation& sim,const Json&,const Json& map) override {
        if(!installEditableFromMap(sim,map))
            throw std::runtime_error("PHYSIS editable setup failed");
    }
    void applyCommand(Simulation& sim,const Command& command) override {
        if(command.type==kFieldEditCmd) {
            // Production host must authenticate system commands at network edge.
            (void)applyFieldEditCommand(sim,command,kFieldEditCmd);
        }
    }
    void hashState(Simulation&,Hasher&) override {}
    int result(Simulation&) override{return 0;}
    bool serializeState(Simulation&,Archive& a) override{return a.ok();}
};
std::unique_ptr<Simulation> make() {
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<EditableHost>());
    sim->setup(Json::object(),setupMap());
    return sim;
}
std::vector<AuthoredField> initial() {
    const auto map=setupMap();
    return parseFieldAssets(map["physis"],*optionsFromMap(map));
}
const FieldRuntimeState& registry(const Simulation& s) {
    return *s.world().resourceConst<FieldRuntimeState>();
}
Entity bound(const Simulation& s,const char* name) {
    const auto it=registry(s).byId.find(name);
    return it==registry(s).byId.end() ? kNullEntity:it->second.entity;
}
void formatAndValidation() {
    FieldEditBatch req{0,initial()};
    Command cmd;std::string error;
    CHECK(encodeFieldEditCommand(req,kFieldEditCmd,cmd,&error));
    CHECK(!cmd.args.empty() && cmd.args[0]>0 && cmd.type==kFieldEditCmd);
    FieldEditBatch decoded;
    CHECK(decodeFieldEditCommand(cmd,decoded,&error));
    CHECK(decoded.desired.size()==req.desired.size());
    CHECK(decoded.expectedRevision==0);
    if(decoded.desired.size()==req.desired.size())
        for(size_t i=0;i<req.desired.size();++i)
            CHECK(authoredFingerprint(decoded.desired[i])==
                  authoredFingerprint(req.desired[i]));
    const auto initialState=decoded;
    cmd.args[0]=-1;
    CHECK(!decodeFieldEditCommand(cmd,decoded,&error));
    CHECK(decoded.desired.size()==initialState.desired.size());
    CHECK(encodeFieldEditCommand(req,kFieldEditCmd,cmd,&error));
    cmd.args.pop_back();
    CHECK(!decodeFieldEditCommand(cmd,decoded,&error));
    auto invalid=req;
    invalid.desired.front().id="bad id";
    CHECK(!encodeFieldEditCommand(invalid,kFieldEditCmd,cmd,&error));
    invalid=req;
    invalid.desired.front().id=invalid.desired[1].id;
    CHECK(!encodeFieldEditCommand(invalid,kFieldEditCmd,cmd,&error));
    invalid=req;
    invalid.desired.front().kind=AuthoredFieldKind::Storm;
    CHECK(!encodeFieldEditCommand(invalid,kFieldEditCmd,cmd,&error));
}
void replayIdentityAndRollback() {
    auto a=make(), b=make();
    CHECK(registry(*a).revision==0);
    CHECK(registry(*a).byId.size()==4);
    CHECK(a->checksum()==b->checksum());
    const auto windA=bound(*a,"wind-a");
    const auto windB=bound(*a,"wind-b");
    const auto storm=bound(*a,"storm-c");
    CHECK(a->world().alive(windA) && a->world().alive(storm));
    SimSnapshot before;
    a->saveState(before);
    FieldEditBatch edit{0,initial()};
    edit.desired.erase(std::remove_if(edit.desired.begin(),edit.desired.end(),
        [](const AuthoredField& x){return x.id=="wind-b";}),edit.desired.end());
    for(auto& source:edit.desired)
        if(source.id=="wind-a") {
            auto wind=std::get<WindZone>(source.data);
            wind.velocity.x=Fixed::fromInt(19);
            source.data=wind;
        }
    AuthoredField extra;
    extra.id="wind-new";extra.kind=AuthoredFieldKind::Wind;
    WindZone newZone;
    newZone.center={Fixed::fromInt(10),Fixed{},Fixed::fromInt(10)};
    newZone.velocity={Fixed::fromInt(2),Fixed{},Fixed{}};
    extra.data=newZone;
    edit.desired.push_back(extra);
    std::sort(edit.desired.begin(),edit.desired.end(),
              [](const auto& x,const auto& y){return x.id<y.id;});
    Command cmd;
    CHECK(encodeFieldEditCommand(edit,kFieldEditCmd,cmd));
    cmd.tick=0;cmd.seq=1;
    a->inject(cmd);b->inject(cmd);
    a->step();b->step();
    CHECK(a->checksum()==b->checksum());
    CHECK(registry(*a).revision==1);
    CHECK(bound(*a,"wind-a")==windA);
    CHECK(!a->world().alive(windB));
    CHECK(bound(*a,"wind-b")==kNullEntity);
    CHECK(a->world().get<WindZone>(windA).velocity.x==Fixed::fromInt(19));
    CHECK(bound(*a,"storm-c")==storm);
    CHECK(a->world().alive(bound(*a,"wind-new")));
    const uint64_t after=a->checksum();
    a->loadState(before);
    CHECK(registry(*a).revision==0);
    CHECK(bound(*a,"wind-a")==windA);
    CHECK(bound(*a,"wind-b")==windB);
    CHECK(a->world().alive(windB));
    a->inject(cmd);
    a->step();
    CHECK(a->checksum()==after);
    auto stale=cmd;
    stale.tick=a->tick();
    a->inject(stale);a->step();
    CHECK(registry(*a).revision==1);
    CHECK(a->checksum()==b->checksum() || a->tick()!=b->tick());
}
void kindChangeAndSave() {
    auto one=make();
    FieldEditBatch req{0,initial()};
    const Entity oldWind=bound(*one,"wind-a");
    for(auto& source:req.desired)if(source.id=="wind-a") {
        source.kind=AuthoredFieldKind::Gravity;
        GravityField g;
        g.mode=GravityMode::Uniform;
        g.acceleration={Fixed{},Fixed::fromInt(-4),Fixed{}};
        g.radiusMeters=Fixed::fromInt(30);
        source.data=g;
    }
    Command change;CHECK(encodeFieldEditCommand(req,kFieldEditCmd,change));
    change.tick=0;change.seq=1;
    one->inject(change);one->step();
    CHECK(registry(*one).revision==1);
    const Entity updated=bound(*one,"wind-a");
    CHECK(updated!=oldWind);
    CHECK(!one->world().alive(oldWind));
    CHECK(one->world().has<GravityField>(updated));
    CHECK(!one->world().has<WindZone>(updated));
    auto saved=one->serialize();
    auto restored=make();
    std::string error;
    CHECK(restored->deserialize(saved,&error));
    CHECK(restored->checksum()==one->checksum());
    CHECK(bound(*restored,"wind-a")==updated);
    CHECK(registry(*restored).revision==1);
    one->step();restored->step();
    CHECK(one->checksum()==restored->checksum());
}
void authorityAndStale() {
    auto sim=make();
    Command cmd;
    FieldEditBatch edit{0,initial()};
    CHECK(encodeFieldEditCommand(edit,kFieldEditCmd,cmd));
    cmd.tick=sim->tick();
    const auto baseline=sim->checksum();
    cmd.player=1;
    std::string error;
    CHECK(!applyFieldEditCommand(*sim,cmd,kFieldEditCmd,&error));
    CHECK(sim->checksum()==baseline);
    cmd.player=0;cmd.tick=sim->tick()+1;
    CHECK(!applyFieldEditCommand(*sim,cmd,kFieldEditCmd,&error));
    CHECK(sim->checksum()==baseline);
    cmd.tick=sim->tick();
    CHECK(applyFieldEditCommand(*sim,cmd,kFieldEditCmd,&error)); // noop consumes revision
    CHECK(registry(*sim).revision==1);
    CHECK(!applyFieldEditCommand(*sim,cmd,kFieldEditCmd,&error));
    CHECK(registry(*sim).revision==1);
}
} // namespace
int main() {
    formatAndValidation();
    replayIdentityAndRollback();
    kindChangeAndSave();
    authorityAndStale();
    std::printf("PHYSIS field runtime: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
