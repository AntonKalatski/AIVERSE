// Headless bots and an in-process load harness: the place server and N bot clients over SimNet (secure UDP
// with link profiles such as "4g"), on a virtual 60 Hz clock. Used by `aiverse_place load` and tests/place.
#pragma once
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include "apps/place/PlaceClient.h"
#include "apps/place/PlaceServer.h"
#include "engine/net/Crypto.h"
#include "engine/net/SecureUdp.h"
#include "engine/net/SimNet.h"

namespace aiverse::place {

// A headless player: walks to a random body and pushes it, jumps, runs, now and then spawns a box.
// With waypoints it patrols them instead (a steady walker on open ground: prediction checks).
class Bot {
public:
    explicit Bot(uint32_t seed, std::vector<b3Vec3> waypoints = {});
    Input think(PlaceClient& client, bool active);
    bool spawnEnabled = true;
    bool pushEnabled = true;
    bool jumpEnabled = true;
private:
    std::mt19937 rng_;
    std::vector<b3Vec3> waypoints_;
    size_t next_ = 0;
    replica::ObjectId target_ = 0;
    uint64_t ticks_ = 0, retargetAt_ = 0, spawnAt_ = 0;
    float yaw_ = 0, wander_ = 0;
    bool run_ = false;
};

struct SimEntry {
    std::unique_ptr<PlaceClient> client;
    std::unique_ptr<Bot> bot;
    uint64_t joinedAtMs = 0;
};

class SimPlace {
public:
    SimPlace(const Json& scene, uint64_t seed = 1, replica::ServerOptions options = {});
    ~SimPlace() { clients.clear(); }   // client connections close through the simulated network
    PlaceClient& add(const std::string& profile, std::unique_ptr<Bot> bot, replica::ClientOptions options = defaultClientOptions());
    void frame();                      // one tick: network time, admission, server tick, every client tick
    void run(double seconds);
    double seconds() const { return double(ticks_) / 60.0; }

    PlaceServer server;
    std::vector<SimEntry> clients;
    bool botsActive = true;
    std::function<void()> afterServer; // test hook between the server tick and the clients

    // Distance between a client's newest body positions and the server's: worst over the bodies asleep on the
    // server (at rest: must match up to quantization), mean over the awake ones (still moving: latency shows);
    // `missing` counts server bodies the client does not know, `extra` client bodies the server does not.
    struct Convergence { float worst = 0, meanAwake = 0; size_t missing = 0, extra = 0, bodies = 0, awake = 0; };
    Convergence convergence(const PlaceClient& client) const;

private:
    net::SimNetwork net_;
    net::Key key_;
    std::unique_ptr<net::SecureListener> listener_;
    net::Address address_;
    uint64_t now_ = 1000, ticks_ = 0;
};

struct LoadOptions {
    int clients = 16;
    double seconds = 30;
    std::string profile = "4g";
    double lateJoinAt = 20;            // < 0: none
    double quietSeconds = 3;           // bots stand still at the end, then convergence is measured
    uint64_t seed = 7;
};
struct LoadReport {
    double tickAvgMs = 0, tickWorstMs = 0, bytesPerClientPerSecond = 0, serverBytesPerClientPerSecond = 0;
    uint64_t reconciles = 0, corrections = 0;
    float meanCorrection = 0, maxCorrection = 0, worstConvergence = 0, lateJoinWorst = 0;
    size_t bodies = 0, lateJoinMissing = 0, missing = 0;
    bool allReady = false, lateJoinSame = false;
    PlaceServerStats server;
    std::string text() const;
};
LoadReport runLoad(const Json& scene, const LoadOptions& options);
}
