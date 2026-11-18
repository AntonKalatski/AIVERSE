#include "engine/physis/PhysisAdminEndpoint.h"
#include "engine/physis/PhysisRemoteClient.h"
#include "engine/net/Conn.h"
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

using namespace aiverse;
using namespace aiverse::physis;
using namespace aiverse::physis::remote;
using namespace aiverse::physis::remote::admin;
namespace {
int failed=0;
#define CHECK(x) do{if(!(x)){std::printf("PHYSIS ADMIN FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failed;}}while(0)
constexpr std::uint16_t kEdit=53012;
net::Key key() {
    net::Key result{};
    for(std::size_t i=0;i<result.size();++i)
        result[i]=std::uint8_t(i*7+3);
    return result;
}
Json map() {
    return Json::parse(R"({"physis":{
      "enabled":true,
      "grid":{"originX":0,"originZ":0,"width":2,"height":2,"cellSizeMeters":100,"seed":8},
      "climate":{"baseTemperature":16,"baseHumidity":0.5},
      "enableWindZones":true,"enableStorms":true,"enableGravityFields":true,
      "windZones":[{"id":"wind-one","center":[0,0,0],"radiusMeters":70,"velocity":[1,0,0]}]
    }})");
}
class Host final:public GameModule {
public:
    std::string name()const override{return "physis-admin-journal-test";}
    void setup(Simulation& sim,const Json&,const Json& m)override{
        if(!installEditableFromMap(sim,m))throw std::runtime_error("PHYSIS install");
    }
    void applyCommand(Simulation& sim,const Command& cmd)override{
        if(cmd.type==kEdit)(void)applyFieldEditCommand(sim,cmd,kEdit);
    }
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
std::unique_ptr<Simulation> make() {
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<Host>());
    sim->setup(Json::object(),map());
    return sim;
}
FieldEditBatch desired(int speed=15,std::uint64_t revision=0) {
    const auto j=map();
    auto assets=parseFieldAssets(j["physis"],*optionsFromMap(j));
    auto wind=std::get<WindZone>(assets.front().data);
    wind.velocity.x=Fixed::fromInt(speed);
    assets.front().data=wind;
    return {revision,std::move(assets)};
}
Packet packet(int speed=15,std::uint64_t instance=144,
              std::uint64_t request=1) {
    Packet p;
    p.kind=Message::Submit;
    p.clientInstance=instance;p.requestId=request;
    if(!encodeFieldEditCommand(desired(speed),kEdit,p.command))
        throw std::runtime_error("cannot encode test PHYSIS command");
    p.command.player=4;
    return p;
}
bool reader(const std::string& user){return user=="admin";}
bool writer(const std::string& user,const Command& cmd){
    return user=="admin" && cmd.player==4 && cmd.type==kEdit;
}
std::filesystem::path tempPath() {
    net::Key random=net::randomKey();
    const auto label=net::toHexString(random.data(),8);
    return std::filesystem::temp_directory_path()/("aiverse-physis26-"+label);
}

void codecAndCorruption() {
    ServerGateway g;
    auto sim=make();
    auto [client,server]=net::makeLoopbackPair();
    const auto p=packet();
    net::Bytes b;
    CHECK(encode(p,b));
    client->send(b);
    g.poll(*sim,*server,"admin",reader,writer,1);
    CHECK(g.entries().size()==1);
    std::map<ServerGateway::Key,ServerGateway::Entry> restored;
    net::Bytes snapshot;
    std::string error;
    CHECK(FileJournal::encodeEntries(g.entries(),snapshot,&error));
    CHECK(FileJournal::decodeEntries(snapshot,restored,&error));
    CHECK(restored.size()==1);
    auto bad=snapshot;
    bad.back()^=1;
    CHECK(!FileJournal::decodeEntries(bad,restored,&error));
    CHECK(restored.size()==1); // unchanged on error
    bad=snapshot;
    bad.pop_back();
    CHECK(!FileJournal::decodeEntries(bad,restored,&error));
}

void durableIntentAndReload() {
    auto sim=make();
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto journalPath=dir/"physis.receipts";
    {
        ServerGateway gateway;
        FileJournal file(journalPath);
        std::string error;
        CHECK(file.attach(gateway,&error));
        auto [client,server]=net::makeLoopbackPair();
        net::Bytes bytes;CHECK(encode(packet(),bytes));
        client->send(bytes);
        gateway.poll(*sim,*server,"admin",reader,writer,1);
        CHECK(gateway.entries().size()==1);
        CHECK(std::filesystem::exists(journalPath));
        CHECK(std::filesystem::file_size(journalPath)>18);
        const auto status=gateway.entries().begin()->second.receipt.result;
        CHECK(status==Result::Queued);
        for(int t=0;t<4;++t)sim->step();
        gateway.observe(*sim);
        CHECK(gateway.entries().begin()->second.receipt.result==Result::Applied);
    }
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
    const auto checkpoint=sim->serialize();
    auto recovered=make();
    std::string error;
    CHECK(recovered->deserialize(checkpoint,&error));
    {
        ServerGateway gateway;
        FileJournal file(journalPath);
        CHECK(file.attach(gateway,&error));
        CHECK(gateway.entries().size()==1);
        gateway.observe(*recovered);
        auto [client,server]=net::makeLoopbackPair();
        net::Bytes bytes;CHECK(encode(packet(),bytes));
        client->send(bytes);
        gateway.poll(*recovered,*server,"admin",reader,writer,100);
        CHECK(gateway.entries().size()==1);
        CHECK(recovered->world().resourceConst<FieldRuntimeState>()->revision==1);
        net::Bytes reply;
        Packet response;
        client->update(100);
        CHECK(client->recv(reply));
        CHECK(decode(reply,response));
        CHECK(response.receipt.result==Result::Applied);
    }
    std::filesystem::remove_all(dir);
}
void missingJournalFailsClosed() {
    auto sim=make();
    const auto dir=tempPath();
    const auto p=dir/"missing"/"record";
    ServerGateway gateway;
    FileJournal journal(p);
    std::string error;
    CHECK(journal.attach(gateway,&error));
    auto [client,server]=net::makeLoopbackPair();
    net::Bytes bytes;CHECK(encode(packet(),bytes));
    client->send(bytes);
    gateway.poll(*sim,*server,"admin",reader,writer,1);
    CHECK(!gateway.journalHealthy());
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==0);
    for(int i=0;i<4;++i)sim->step();
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==0);
}
void recoveredIntent() {
    auto sim=make();
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    const auto p=dir/"journal";
    ServerGateway gateway;
    ServerGateway::Key key{"admin",123,6};
    ServerGateway::Entry entry;
    CHECK(encode(packet(19,123,6),entry.originalFrame));
    entry.expectedRevision=0;
    entry.observedTick=0;
    entry.receipt={Result::Unknown,0,0,"PHYSIS durable submission intent"};
    const auto assets=desired(19).desired;
    for(const auto& field:assets)
        entry.desired.emplace(field.id,
            std::make_pair(field.kind,authoredFingerprint(field)));
    std::map<ServerGateway::Key,ServerGateway::Entry> records;
    records.emplace(key,std::move(entry));
    FileJournal journal(p);
    std::string error;
    CHECK(journal.save(records,&error));
    CHECK(journal.attach(gateway,&error));
    auto [client,server]=net::makeLoopbackPair();
    net::Bytes b;CHECK(encode(packet(19,123,6),b));
    client->send(b);
    gateway.poll(*sim,*server,"admin",reader,writer,5);
    CHECK(gateway.entries().size()==1);
    CHECK(gateway.entries().begin()->second.receipt.result==Result::Queued);
    for(int i=0;i<4;++i)sim->step();
    gateway.observe(*sim);
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
    std::filesystem::remove_all(dir);
}
void authProof() {
    auto [client,server]=net::makeLoopbackPair();
    const auto k=key();
    ServerHandshake serverAuth([&](const std::string& user)->std::optional<net::Key>{
        return user=="admin"?std::optional<net::Key>(k):std::nullopt;
    });
    ClientHandshake clientAuth("admin",k);
    CHECK(clientAuth.begin(*client));
    net::Bytes b;
    CHECK(server->recv(b));
    CHECK(serverAuth.consume(*server,b));
    CHECK(client->recv(b));
    CHECK(clientAuth.consume(*client,b));
    CHECK(server->recv(b));
    CHECK(serverAuth.consume(*server,b));
    CHECK(client->recv(b));
    CHECK(clientAuth.consume(*client,b));
    CHECK(clientAuth.authenticated() && serverAuth.authenticated());
    CHECK(serverAuth.principal()=="admin");
    // A wrong client secret cannot establish a trusted principal.
    auto [badC,badS]=net::makeLoopbackPair();
    ServerHandshake badServer([&](const std::string& user)->std::optional<net::Key>{
        return user=="admin"?std::optional<net::Key>(k):std::nullopt;
    });
    net::Key wrong=k;wrong[0]^=12;
    ClientHandshake badClient("admin",wrong);
    CHECK(badClient.begin(*badC));
    CHECK(badS->recv(b));
    CHECK(badServer.consume(*badS,b));
    CHECK(badC->recv(b));
    CHECK(badClient.consume(*badC,b));
    CHECK(badS->recv(b));
    CHECK(!badServer.consume(*badS,b));
    CHECK(!badServer.authenticated());
}
#ifndef __EMSCRIPTEN__
void realLoopbackWebSocket() {
    const auto dir=tempPath();
    std::filesystem::create_directories(dir);
    auto sim=make();
    LoopbackAdminEndpoint service(dir/"journal");
    std::string error;
    CHECK(service.open(0,
        [](const std::string& name)->std::optional<net::Key>{
            return name=="admin"?std::optional<net::Key>(key()):std::nullopt;
        },reader,writer,&error));
    CHECK(service.port()!=0);
    LoopbackAdminConnector connector;
    const auto url="ws://127.0.0.1:"+std::to_string(service.port());
    CHECK(connector.connect(url,"admin",key(),&error));
    const auto start=net::nowMs();
    bool established=false;
    for(int i=0;i<200;++i) {
        const auto now=start+std::uint64_t(i*20);
        connector.poll(now);
        service.poll(*sim,now);
        connector.poll(now+1);
        if(connector.ready()){established=true;break;}
    }
    CHECK(established);
    if(established) {
        RemoteClient remote(334455);
        CHECK(remote.begin(desired(21),kEdit,4));
        auto c=connector.takeAuthenticated();
        const auto next=net::nowMs();
        for(int i=0;i<150;++i) {
            const auto now=next+std::uint64_t(i*30);
            remote.poll(*c,now);
            service.poll(*sim,now);
            sim->step();
            service.afterTick(*sim);
            remote.poll(*c,now+1);
            if(remote.status()==ClientStatus::Applied)break;
        }
        CHECK(remote.status()==ClientStatus::Applied);
        CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
    }
    std::filesystem::remove_all(dir);
}
#endif
}
int main(){
    codecAndCorruption();
    durableIntentAndReload();
    missingJournalFailsClosed();
    recoveredIntent();
    authProof();
#ifndef __EMSCRIPTEN__
    realLoopbackWebSocket();
#endif
    std::printf("PHYSIS admin/journal: %s\n",failed?"FAIL":"PASS");
    return failed?1:0;
}
