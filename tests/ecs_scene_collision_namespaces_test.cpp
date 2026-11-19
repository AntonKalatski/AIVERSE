// Real collision streamer integration with deterministic, additive scene
// namespaces in one SpatialWorld. All assertions execute in Release.
#include "engine/scene/SceneCollisionNamespaces.h"
#include "engine/pack/Sha256.h"
#include "tests/AlwaysCheck.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace aiverse;
namespace sc=aiverse::scene;
namespace wd=aiverse::world;

static Json manifest(const std::string& key,const std::string& bytes,
                     int32_t cellX=0) {
    auto digest=pack::sha256(bytes.data(),bytes.size());
    const std::string sha=pack::toHex(digest.data(),digest.size());

    Json document=Json::object();
    document.set("async",Json::fromBool(true));
    document.set("version",Json::number(int64_t(1)));
    document.set("cellSize",Json::number(int64_t(16)));
    document.set("maxResidentBytes",Json::number(int64_t(1024*1024)));
    document.set("maxStagingBytes",Json::number(int64_t(1024*1024)));
    document.set("maxResidentChunks",Json::number(int64_t(4)));
    document.set("maxJobs",Json::number(int64_t(1)));
    Json hashes=Json::object();
    hashes.set(key,Json::string(sha));
    document.set("assets",std::move(hashes));

    Json chunk=Json::object(),cell=Json::array();
    cell.push(Json::number(int64_t(cellX)));
    cell.push(Json::number(int64_t(0)));
    cell.push(Json::number(int64_t(0)));
    chunk.set("cell",std::move(cell));
    chunk.set("file",Json::string(key));
    chunk.set("reservedBytes",Json::number(int64_t(8192)));
    Json chunks=Json::array();
    chunks.push(std::move(chunk));
    document.set("chunks",std::move(chunks));
    return document;
}
static wd::Bounds region() {
    return {{Fixed::fromInt(-1),Fixed::fromInt(-1),Fixed::fromInt(-1)},
            {Fixed::fromInt(8),Fixed::fromInt(8),Fixed::fromInt(8)}};
}
static void untilReady(wd::CollisionStream& stream) {
    const auto bounds=region();
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<limit) {
        if(stream.prepareRegions({&bounds,1},1,1)) return;
        std::this_thread::yield();
    }
    throw std::runtime_error("collision stream preparation timeout");
}
static void verifyNamespaces() {
    // Two reservation builders differing only in authoring order must have
    // IDENTICAL slot layout and fingerprints before async stream creation.
    sc::SceneCollisionNamespaces catalog;
    AIVERSE_TEST_CHECK(catalog.define("instances/b",1));
    AIVERSE_TEST_CHECK(catalog.define("instances/a",1));
    AIVERSE_TEST_CHECK(!catalog.define("instances/a",1));
    AIVERSE_TEST_CHECK(!catalog.define("../bad",1));
    AIVERSE_TEST_CHECK(catalog.seal());
    AIVERSE_TEST_CHECK(!catalog.seal());
    AIVERSE_TEST_CHECK(!catalog.define("instances/late",1));
    const auto aRange=catalog.find("instances/a");
    const auto bRange=catalog.find("instances/b");
    AIVERSE_TEST_CHECK(aRange && bRange);
    AIVERSE_TEST_CHECK(aRange->valid() && bRange->valid());
    AIVERSE_TEST_CHECK(aRange->firstSlot==0 && bRange->firstSlot==1);
    AIVERSE_TEST_CHECK(catalog.reservedChunks()==2);
    sc::SceneCollisionNamespaces reversed;
    AIVERSE_TEST_CHECK(reversed.define("instances/a",1));
    AIVERSE_TEST_CHECK(reversed.define("instances/b",1));
    AIVERSE_TEST_CHECK(reversed.seal());
    AIVERSE_TEST_CHECK(reversed.fingerprint()==catalog.fingerprint());
    AIVERSE_TEST_CHECK(reversed.find("instances/a")->firstSlot==
                      aRange->firstSlot);

    const std::string boxJson=R"({"boxes":[
        {"min":[0,0,0],"max":[3,3,3]}
    ]})";
    Json dataA=manifest("chunk/a.json",boxJson);
    Json dataB=manifest("chunk/b.json",boxJson);
    auto reader=[boxJson](const std::string&,std::string& out){
        out=boxJson;
        return true;
    };
    wd::SpatialWorld space(32);
    auto aLease=catalog.acquire("instances/a");
    auto bLease=catalog.acquire("instances/b");
    AIVERSE_TEST_CHECK(aLease && bLease);
    AIVERSE_TEST_CHECK(catalog.activeCount()==2);
    AIVERSE_TEST_CHECK(!catalog.acquire("instances/a"));
    // Streams may be constructed even though no collider has yet been
    // uploaded; the namespace LEASE prevents hidden future aliasing.
    auto a=std::make_unique<wd::CollisionStream>(
        dataA,reader,space,aLease->range());
    auto b=std::make_unique<wd::CollisionStream>(
        dataB,reader,space,bLease->range());
    untilReady(*a);
    untilReady(*b);
    AIVERSE_TEST_CHECK(space.colliders().size()==2);
    AIVERSE_TEST_CHECK(space.colliders().contains(0x80000001u));
    AIVERSE_TEST_CHECK(space.colliders().contains(0x80010001u));
    AIVERSE_TEST_CHECK(space.blocked(region()));
    AIVERSE_TEST_CHECK(a->covers(region()) && b->covers(region()));

    // An explicit collision stream cannot steal a resident ID range even
    // by bypassing the namespace allocator.
    bool duplicateRejected=false;
    try {
        wd::CollisionStream illegal(dataA,reader,space,*aRange);
    } catch(const JsonError&) { duplicateRejected=true; }
    AIVERSE_TEST_CHECK(duplicateRejected);

    // Legacy single-world streams keep their existing high-ID guard.
    bool legacyRejected=false;
    try {
        wd::CollisionStream legacy(dataA,reader,space);
    } catch(const JsonError&) { legacyRejected=true; }
    AIVERSE_TEST_CHECK(legacyRejected);

    // Removing one scene drops ONLY its reserved collider IDs; other
    // active scene remains resident. Released slots may be reacquired
    // by the SAME stable scene instance after stream shutdown.
    a.reset();
    AIVERSE_TEST_CHECK(space.colliders().size()==1);
    AIVERSE_TEST_CHECK(space.colliders().contains(0x80010001u));
    aLease.reset();
    AIVERSE_TEST_CHECK(catalog.activeCount()==1);
    auto again=catalog.acquire("instances/a");
    AIVERSE_TEST_CHECK(again.has_value());
    auto reloaded=std::make_unique<wd::CollisionStream>(
        dataA,reader,space,again->range());
    untilReady(*reloaded);
    AIVERSE_TEST_CHECK(space.colliders().size()==2);
    AIVERSE_TEST_CHECK(space.colliders().contains(0x80000001u));
    reloaded.reset();
    again.reset();
    b.reset();
    bLease.reset();
    AIVERSE_TEST_CHECK(space.colliders().empty());
    AIVERSE_TEST_CHECK(catalog.activeCount()==0);
}

int main() {
    verifyNamespaces();
    std::cout<<"PASS two concurrent additive hashed CollisionStreams, "
               "collision namespace leases, independent unload/reload, legacy guard\n";
}
