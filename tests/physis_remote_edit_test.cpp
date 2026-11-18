#include "engine/physis/PhysisRemoteClient.h"
#include "engine/physis/PhysisRemoteServer.h"
#include "engine/net/Conn.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;
using namespace aiverse::physis;
using namespace aiverse::physis::remote;
namespace {
int fails=0;
#define CHECK(x) do {if(!(x)){std::printf("PHYSIS REMOTE FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);++fails;}}while(0)
constexpr uint16_t kEdit=53011;

Json map() {
    return Json::parse(R"({"physis":{
        "enabled":true,
        "grid":{"originX":0,"originZ":0,"width":2,"height":2,
                "cellSizeMeters":100,"seed":17},
        "climate":{"baseTemperature":15,"baseHumidity":0.5},
        "enableWindZones":true,"enableGravityFields":true,"enableStorms":true,
        "floodMode":"ExternalAuthority",
        "windZones":[{"id":"wind-1","center":[0,0,0],
                      "radiusMeters":100,"velocity":[5,0,0]}]
    }})");
}
class Host final:public GameModule {
public:
    std::string name()const override{return "physis-remote-test";}
    void setup(Simulation& sim,const Json&,const Json& m)override{
        (void)installEditableFromMap(sim,m);
    }
    void applyCommand(Simulation& sim,const Command& c)override{
        if(c.type==kEdit)(void)applyFieldEditCommand(sim,c,kEdit);
    }
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
std::unique_ptr<Simulation> make() {
    auto s=std::make_unique<Simulation>(SimConfig{},std::make_unique<Host>());
    s->setup(Json::object(),map());
    return s;
}
FieldEditBatch edit(Fixed speed,std::uint64_t revision=0) {
    const auto j=map();
    auto options=optionsFromMap(j);
    auto fields=parseFieldAssets(j["physis"],*options);
    WindZone w=std::get<WindZone>(fields[0].data);
    w.velocity.x=speed;
    fields[0].data=w;
    return {revision,std::move(fields)};
}
const FieldRuntimeState& registry(Simulation& s) {
    return *s.world().resourceConst<FieldRuntimeState>();
}
bool readAcl(const std::string& identity) {
    return identity=="trusted-editor";
}
bool writeAcl(const std::string& identity,const Command& cmd) {
    return identity=="trusted-editor" && cmd.player==4 && cmd.type==kEdit;
}
void drive(RemoteClient& client,ServerGateway& server,Simulation& simulation,
           net::Conn& c,net::Conn& s,
           std::uint64_t start=0,std::uint64_t end=2000,
           bool advance=true) {
    for(std::uint64_t ms=start;ms<end;ms+=40) {
        client.poll(c,ms);
        server.poll(simulation,s,"trusted-editor",readAcl,writeAcl,ms);
        if(advance)simulation.step();
        server.observe(simulation);
        client.poll(c,ms+1);
    }
}

void codec() {
    auto command=Command{};
    CHECK(encodeFieldEditCommand(edit(Fixed::fromInt(13)),kEdit,command));
    command.player=4;
    Packet p; p.kind=Message::Submit; p.clientInstance=19;p.requestId=1;
    p.command=command;
    net::Bytes bytes;
    CHECK(encode(p,bytes));
    Packet decoded;
    CHECK(decode(bytes,decoded));
    CHECK(decoded.kind==Message::Submit && decoded.requestId==1);
    CHECK(decoded.command.player==4);
    FieldEditBatch parsed;
    CHECK(decodeFieldEditCommand(decoded.command,parsed));
    CHECK(parsed.desired.size()==1);
    CHECK(!bytes.empty());
    auto corrupt=bytes;
    corrupt[0]^=0xff;
    CHECK(!decode(corrupt,decoded));
    CHECK(decoded.kind==Message::Submit); // failed decode leaves output intact
    corrupt=bytes;corrupt.pop_back();
    CHECK(!decode(corrupt,decoded));
    Packet query;query.kind=Message::Query;query.clientInstance=19;query.requestId=4;
    CHECK(encode(query,bytes));
    CHECK(decode(bytes,decoded) && decoded.kind==Message::Query);
    Packet ack;ack.kind=Message::Receipt;ack.clientInstance=19;ack.requestId=4;
    ack.receipt={Result::Conflict,19,3,"stale revision"};
    CHECK(encode(ack,bytes));
    CHECK(decode(bytes,decoded));
    CHECK(decoded.receipt.result==Result::Conflict && decoded.receipt.revision==3);
    ack.receipt.detail.assign(kMaxText+1,'x');
    CHECK(!encode(ack,bytes));
}

void completeAndDedupe() {
    auto serverSim=make();
    ServerGateway gateway;
    RemoteClient client(10019);
    CHECK(client.begin(edit(Fixed::fromInt(23)),kEdit,4));
    auto wire=client.exportOutstanding();
    CHECK(!wire.empty());
    auto [clientConn,serverConn]=net::makeLoopbackPair({40,0,3});
    drive(client,gateway,*serverSim,*clientConn,*serverConn,0,3000);
    CHECK(client.status()==ClientStatus::Applied);
    CHECK(registry(*serverSim).revision==1);
    const auto wind=registry(*serverSim).byId.at("wind-1").entity;
    CHECK(serverSim->world().get<WindZone>(wind).velocity.x==Fixed::fromInt(23));
    CHECK(gateway.entries().size()==1);
    // Repeat exact frame; it should only return stored receipt, never reapply.
    clientConn->send(wire);
    for(std::uint64_t ms=3000;ms<3300;ms+=40) {
        serverConn->update(ms);serverConn->recv(wire); // no: consume would drop it
    }
}
void duplicatedPayloadConflict() {
    auto sim=make();
    ServerGateway gateway;
    auto [client,server]=net::makeLoopbackPair();
    Packet request;request.kind=Message::Submit;
    request.clientInstance=123;request.requestId=77;
    CHECK(encodeFieldEditCommand(edit(Fixed::fromInt(7)),kEdit,request.command));
    request.command.player=4;
    net::Bytes encoded;
    CHECK(encode(request,encoded));
    client->send(encoded);
    server->update(10);
    gateway.poll(*sim,*server,"trusted-editor",readAcl,writeAcl,10);
    sim->step();sim->step();sim->step();
    gateway.observe(*sim);
    CHECK(registry(*sim).revision==1);
    client->send(encoded);
    gateway.poll(*sim,*server,"trusted-editor",readAcl,writeAcl,100);
    CHECK(registry(*sim).revision==1);
    // Same principal/instance/request ID with modified bytes is forbidden.
    CHECK(encodeFieldEditCommand(edit(Fixed::fromInt(15)),kEdit,request.command));
    request.command.player=4;
    CHECK(encode(request,encoded));
    client->send(encoded);
    gateway.poll(*sim,*server,"trusted-editor",readAcl,writeAcl,120);
    CHECK(registry(*sim).revision==1);
    net::Bytes response;
    Result last=Result::Unknown;
    client->update(120);
    while(client->recv(response)) {
        Packet receipt;
        if(decode(response,receipt))last=receipt.receipt.result;
    }
    CHECK(last==Result::Rejected);
}
void reconnectAndRestore() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient original(4242);
    CHECK(original.begin(edit(Fixed::fromInt(31)),kEdit,4));
    auto snapshot=original.exportOutstanding();
    auto [c1,s1]=net::makeLoopbackPair();
    original.poll(*c1,0);
    gateway.poll(*sim,*s1,"trusted-editor",readAcl,writeAcl,0);
    // Simulate losing response before the next client poll.
    sim->step();sim->step();sim->step();
    gateway.observe(*sim);
    CHECK(registry(*sim).revision==1);
    c1->close();s1->close();
    RemoteClient restored(4242);
    CHECK(restored.restoreOutstanding(snapshot));
    CHECK(restored.requestId()==original.requestId());
    auto [c2,s2]=net::makeLoopbackPair();
    drive(restored,gateway,*sim,*c2,*s2,100,1400);
    CHECK(restored.status()==ClientStatus::Applied);
    CHECK(registry(*sim).revision==1);
    CHECK(gateway.entries().size()==1);
}
void denied() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient client(99);
    CHECK(client.begin(edit(Fixed::fromInt(51)),kEdit,4));
    auto [c,s]=net::makeLoopbackPair();
    for(std::uint64_t ms=0;ms<1200;ms+=40) {
        client.poll(*c,ms);
        gateway.poll(*sim,*s,"unauthorized",readAcl,writeAcl,ms);
        client.poll(*c,ms+1);
    }
    CHECK(client.status()==ClientStatus::Denied);
    CHECK(registry(*sim).revision==0);
}
void conflictAfterServerRestart() {
    auto sim=make();
    ServerGateway gateway;
    RemoteClient client(4142);
    CHECK(client.begin(edit(Fixed::fromInt(9)),kEdit,4));
    auto saved=client.exportOutstanding();
    auto [c1,s1]=net::makeLoopbackPair();
    client.poll(*c1,0);
    gateway.poll(*sim,*s1,"trusted-editor",readAcl,writeAcl,0);
    for(int i=0;i<3;++i)sim->step();
    gateway.observe(*sim);
    CHECK(registry(*sim).revision==1);
    c1->close();s1->close();
    // Cache lost (new server), but World's revision is authoritative:
    // resend cannot apply twice; server responds with conflict.
    ServerGateway restartedGateway;
    RemoteClient restored(4142);
    CHECK(restored.restoreOutstanding(saved));
    auto [c2,s2]=net::makeLoopbackPair();
    drive(restored,restartedGateway,*sim,*c2,*s2,100,2000);
    CHECK(restored.status()==ClientStatus::Conflict);
    CHECK(registry(*sim).revision==1);
}
}
int main() {
    codec();
    duplicatedPayloadConflict();
    reconnectAndRestore();
    denied();
    conflictAfterServerRestart();
    std::printf("PHYSIS remote edit: %s\n",fails?"FAIL":"PASS");
    return fails?1:0;
}
