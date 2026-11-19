// Real SceneLoadCoordinator -> existing async hashed CollisionStream, using
// disjoint namespaces in the same SpatialWorld. Host gates simulation until
// collision regions are resident. Read and write assertions remain enabled.
#include "engine/scene/SceneCollisionBridge.h"
#include "engine/pack/Sha256.h"
#include "tests/AlwaysCheck.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <utility>

using namespace aiverse;
namespace sc=aiverse::scene;
namespace wd=aiverse::world;

static Json makeCollisionManifest(std::string path,std::string contents) {
    auto hash=pack::sha256(contents.data(),contents.size());
    auto doc=Json::object();
    doc.set("async",Json::fromBool(true));
    doc.set("version",Json::number(int64_t(1)));
    doc.set("cellSize",Json::number(int64_t(16)));
    doc.set("maxResidentBytes",Json::number(int64_t(1024*1024)));
    doc.set("maxStagingBytes",Json::number(int64_t(1024*1024)));
    doc.set("maxResidentChunks",Json::number(int64_t(4)));
    Json assets=Json::object();
    assets.set(path,Json::string(pack::toHex(hash.data(),hash.size())));
    doc.set("assets",std::move(assets));
    Json chunk=Json::object();
    Json cell=Json::array();
    for(int i=0;i<3;++i)cell.push(Json::number(int64_t(0)));
    chunk.set("cell",std::move(cell));
    chunk.set("file",Json::string(path));
    chunk.set("reservedBytes",Json::number(int64_t(8192)));
    Json chunks=Json::array();
    chunks.push(std::move(chunk));
    doc.set("chunks",std::move(chunks));
    return doc;
}
static wd::Bounds gameplayRegion() {
    return {{Fixed::fromInt(-1),Fixed::fromInt(-1),Fixed::fromInt(-1)},
            {Fixed::fromInt(8),Fixed::fromInt(8),Fixed::fromInt(8)}};
}
static void testRealSceneBridge() {
    const std::string box=R"({"boxes":[
      {"min":[0,0,0],"max":[3,3,3]}
    ]})";
    auto chunkReader=[box](const std::string&,std::string& out) {
        out=box;return true;
    };
    wd::SpatialWorld target(32);
    sc::SceneCollisionNamespaces ids;
    AIVERSE_TEST_CHECK(ids.define("instances/cave",1));
    AIVERSE_TEST_CHECK(ids.define("instances/global",1));
    AIVERSE_TEST_CHECK(ids.define("instances/town",1));
    AIVERSE_TEST_CHECK(ids.seal());

    sc::SceneCollisionBridge collision(target,ids);
    AIVERSE_TEST_CHECK(collision.define("collision/global","instances/global",
        makeCollisionManifest("chunks/global.json",box),chunkReader));
    AIVERSE_TEST_CHECK(collision.define("collision/town","instances/town",
        makeCollisionManifest("chunks/town.json",box),chunkReader));
    AIVERSE_TEST_CHECK(collision.define("collision/cave","instances/cave",
        makeCollisionManifest("chunks/cave.json",box),chunkReader));
    AIVERSE_TEST_CHECK(!collision.define("collision/alias","instances/town",
        makeCollisionManifest("chunks/alias.json",box),chunkReader));

    auto sceneReader=[](const std::string& id,std::string& out,
                        std::string& error) {
        static const std::map<std::string,std::string> docs={
          {"scenes/global",R"({"version":1,"id":"scenes/global",
             "dependencies":["collision/global"],"objects":[]})"},
          {"scenes/town",R"({"version":1,"id":"scenes/town",
             "dependencies":["collision/town"],"objects":[]})"},
          {"scenes/cave",R"({"version":1,"id":"scenes/cave",
             "dependencies":["collision/cave"],"objects":[]})"}
        };
        auto it=docs.find(id);
        if(it==docs.end()) {error="missing scene";return false;}
        out=it->second;return true;
    };
    World world;
    sc::SceneLoadCoordinator loader(world,sceneReader,collision.adapter(),{});
    sc::SceneRuntime scenes(world);

    auto awaitScene=[&](sc::SceneTicket ticket) {
        AIVERSE_TEST_CHECK(ticket.valid());
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(5);
        do {
            loader.poll();
            if(loader.ready(ticket))return;
            if(loader.status(ticket)==sc::SceneLoadStatus::Failed)
                throw std::runtime_error(loader.error(ticket));
            std::this_thread::yield();
        }while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("scene manifest prepare timeout");
    };
    auto awaitPhysics=[&] {
        const auto bounds=gameplayRegion();
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(5);
        do {
            if(collision.prepareRegions({&bounds,1},2,2))return;
            std::this_thread::yield();
        }while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("physics collision residency gate timeout");
    };

    auto global=loader.request("scenes/global",sc::LoadMode::Additive,true);
    auto town=loader.request("scenes/town");
    awaitScene(global);
    awaitScene(town);
    AIVERSE_TEST_CHECK(loader.commit(global));
    AIVERSE_TEST_CHECK(loader.commit(town));
    AIVERSE_TEST_CHECK(scenes.count()==2);
    AIVERSE_TEST_CHECK(collision.residentSceneResources()==2);
    awaitPhysics();
    AIVERSE_TEST_CHECK(collision.covers(gameplayRegion()));
    AIVERSE_TEST_CHECK(target.colliders().size()==2);
    AIVERSE_TEST_CHECK(ids.activeCount()==2);

    // Unloading town must never remove a persistent/global collider.
    AIVERSE_TEST_CHECK(loader.unload(town.scene));
    AIVERSE_TEST_CHECK(target.colliders().size()==1);
    AIVERSE_TEST_CHECK(collision.residentSceneResources()==1);
    AIVERSE_TEST_CHECK(ids.activeCount()==1);

    // Replace only nonpersistent scenes. The collision source acquires
    // a distinct namespace even though geometry occupies the same region.
    auto cave=loader.request("scenes/cave",sc::LoadMode::Replace);
    awaitScene(cave);
    AIVERSE_TEST_CHECK(loader.commit(cave));
    awaitPhysics();
    AIVERSE_TEST_CHECK(scenes.count()==2);
    AIVERSE_TEST_CHECK(collision.residentSceneResources()==2);
    AIVERSE_TEST_CHECK(target.colliders().size()==2);
    AIVERSE_TEST_CHECK(ids.activeCount()==2);

    AIVERSE_TEST_CHECK(loader.unload(global.scene));
    AIVERSE_TEST_CHECK(loader.unload(cave.scene));
    AIVERSE_TEST_CHECK(target.colliders().empty());
    AIVERSE_TEST_CHECK(collision.residentSceneResources()==0);
    AIVERSE_TEST_CHECK(ids.activeCount()==0);
}
int main() {
    testRealSceneBridge();
    std::cout<<"PASS additive collision ownership, real scene loads, physics gate "
               "and independent unload/replace\n";
}
