#include "apps/place/PlaceSim.h"
#include <cmath>
#include <cstdio>
#include <set>
#include "engine/net/MatchTicket.h"

namespace aiverse::place {
namespace {
constexpr uint64_t kUnix = 1'800'000'000'000ull;
constexpr uint64_t kMatch = 7001;
float angleTo(b3Vec3 from, b3Vec3 to) { return std::atan2(-(to.x - from.x), -(to.z - from.z)); } // yaw whose forward points at `to`
float wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
}

Bot::Bot(uint32_t seed, std::vector<b3Vec3> waypoints) : rng_(seed), waypoints_(std::move(waypoints)) {
    yaw_ = std::uniform_real_distribution<float>(-3.14f, 3.14f)(rng_);
    spawnAt_ = 300 + rng_() % 600;
}

Input Bot::think(PlaceClient& c, bool active) {
    ++ticks_;
    yaw_ += c.takeGroundTurn();   // a rotating platform turns the bot like it turns a player's view
    Input in;
    in.move.yaw = yaw_;
    if (!active || !c.ready()) return in;
    const auto& st = c.predicted();
    b3Vec3 pos = st.position;
    auto& r = c.replica();
    float want = yaw_;
    bool close = false;
    if (!waypoints_.empty()) {
        b3Vec3 w = waypoints_[next_ % waypoints_.size()];
        float dx = w.x - pos.x, dz = w.z - pos.z;
        if (dx * dx + dz * dz < 0.5f * 0.5f) ++next_;
        want = angleTo(pos, w);
        in.move.moveZ = 1;
    } else {
        if (ticks_ >= retargetAt_ || !r.exists(target_)) {
            auto ids = r.objects();
            std::vector<replica::ObjectId> bodies;
            for (auto id : ids) if (r.classOf(id) == c.schema().body) bodies.push_back(id);
            target_ = bodies.empty() ? 0 : bodies[rng_() % bodies.size()];
            retargetAt_ = ticks_ + 240 + rng_() % 360;
            run_ = rng_() % 3 == 0;
        }
        if (target_) {
            auto t = r.sample(target_, Schema::kBodyPos).f;
            float dx = t[0] - pos.x, dz = t[2] - pos.z;
            close = dx * dx + dz * dz < 2.5f * 2.5f;
            want = angleTo(pos, {t[0], t[1], t[2]});
        } else {
            wander_ += std::uniform_real_distribution<float>(-0.05f, 0.05f)(rng_);
            want = yaw_ + wander_ * 0.1f;
        }
        in.move.moveZ = 1;
        in.move.run = run_;
        if (pushEnabled && close && ticks_ % 20 == 0) in.actions |= kActionPush;
        if (jumpEnabled && (ticks_ % 150 == 0 || (close && ticks_ % 90 == 45))) in.move.jump = true;
        if (spawnEnabled && ticks_ >= spawnAt_) { c.spawnBox(); spawnAt_ = ticks_ + 600 + rng_() % 600; }
    }
    // Turn at a bounded rate like a mouse would.
    float d = wrap(want - yaw_);
    yaw_ = wrap(yaw_ + std::clamp(d, -0.08f, 0.08f));
    in.move.yaw = yaw_;
    return in;
}

SimPlace::SimPlace(const Json& scene, uint64_t seed, replica::ServerOptions options)
    : server(scene, options), net_(seed), key_(net::randomKey()), address_(net::SimNetwork::address("10.0.0.1", 27200)) {
    net::SecureServerConfig sc;
    sc.serverKey = key_;
    sc.admit = [](const net::TicketClaims& c) { return c.matchId == kMatch; };
    sc.clock = [this] { return kUnix + now_; };
    listener_ = std::make_unique<net::SecureListener>(sc);
    listener_->attach(net_.open("10.0.0.1", 27200));
}

PlaceClient& SimPlace::add(const std::string& profile, std::unique_ptr<Bot> bot, replica::ClientOptions options) {
    size_t n = clients.size() + 1;
    net::TicketClaims c;
    c.matchId = kMatch;
    c.playerId = "bot" + std::to_string(n);
    c.slot = uint8_t(n);
    c.expiresAtMs = kUnix + 24ull * 3'600'000;
    auto port = net_.open("10.1." + std::to_string(n / 250) + "." + std::to_string(n % 250 + 1), uint16_t(40000 + n), net::linkPreset(profile));
    SimEntry e;
    e.client = std::make_unique<PlaceClient>(net::connectSecure(std::move(port), address_, net::issueTicket(key_, c, "10.0.0.1:27200")), options);
    e.bot = std::move(bot);
    e.joinedAtMs = now_;
    clients.push_back(std::move(e));
    return *clients.back().client;
}

void SimPlace::frame() {
    ++ticks_;
    now_ = 1000 + ticks_ * 1000 / 60;
    net_.setTime(now_);
    for (auto& a : listener_->poll(now_)) server.attach(std::move(a.conn));
    server.update(now_);
    if (afterServer) afterServer();
    for (auto& e : clients) {
        Input in = e.script ? e.script(*e.client) : e.bot ? e.bot->think(*e.client, botsActive) : Input{};
        e.client->update(now_, in);
        e.client->advanceSmoothing(1000.0f / 60);
    }
}

void SimPlace::run(double seconds) {
    int n = int(seconds * 60 + 0.5);
    for (int i = 0; i < n; ++i) frame();
}

SimPlace::Convergence SimPlace::convergence(const PlaceClient& client) const {
    Convergence out;
    const auto& r = client.replica();
    std::set<replica::ObjectId> mine;
    for (auto id : r.objects()) if (r.classOf(id) == client.schema().body) mine.insert(id);
    double sumAwake = 0;
    for (auto id : server.bodies()) {
        ++out.bodies;
        if (!mine.erase(id)) { ++out.missing; continue; }
        auto a = r.latest(id, Schema::kBodyPos).f;
        auto b = server.replica().get(id, Schema::kBodyPos).f;
        float d = std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
        if (server.replica().get(id, Schema::kBodySleeping).i) out.worst = std::max(out.worst, d);
        else { ++out.awake; sumAwake += d; }
    }
    out.extra = mine.size();
    out.meanAwake = out.awake ? float(sumAwake / double(out.awake)) : 0;
    return out;
}

LoadReport runLoad(const Json& scene, const LoadOptions& o) {
    SimPlace sim(scene, o.seed);
    for (int i = 0; i < o.clients; ++i) sim.add(o.profile, std::make_unique<Bot>(uint32_t(o.seed * 1000 + uint64_t(i))));
    LoadReport rep;
    bool late = o.lateJoinAt >= 0 && o.lateJoinAt < o.seconds;
    int lateIndex = -1;
    sim.run(1.0);                                    // connect
    sim.server.resetTickTiming();
    while (sim.seconds() < o.seconds) {
        if (late && lateIndex < 0 && sim.seconds() >= o.lateJoinAt) {
            lateIndex = int(sim.clients.size());
            sim.add(o.profile, std::make_unique<Bot>(uint32_t(o.seed * 1000 + 999)));
        }
        sim.frame();
    }
    rep.tickAvgMs = sim.server.stats().tickMsAvg();
    rep.tickWorstMs = sim.server.stats().tickMsWorst;
    sim.botsActive = false;
    sim.run(o.quietSeconds);
    rep.allReady = true;
    double bytes = 0;
    for (size_t i = 0; i < sim.clients.size(); ++i) {
        auto& c = *sim.clients[i].client;
        rep.allReady &= c.ready();
        double secs = (sim.seconds() * 1000 + 1000 - double(sim.clients[i].joinedAtMs)) / 1000.0;
        bytes += double(c.replica().stats().bytesIn) / std::max(1.0, secs);
        rep.reconciles += c.stats().reconciles;
        rep.corrections += c.stats().corrections;
        rep.maxCorrection = std::max(rep.maxCorrection, c.stats().maxCorrection);
        rep.meanCorrection += float(c.stats().sumCorrection);
        auto cv = sim.convergence(c);
        rep.bodies = cv.bodies;
        rep.missing += cv.missing;
        rep.worstConvergence = std::max(rep.worstConvergence, cv.worst);
        if (int(i) == lateIndex) { rep.lateJoinWorst = cv.worst; rep.lateJoinMissing = cv.missing; rep.lateJoinSame = cv.missing == 0 && cv.extra == 0; }
    }
    rep.meanCorrection = rep.reconciles ? rep.meanCorrection / float(rep.reconciles) : 0;
    rep.bytesPerClientPerSecond = sim.clients.empty() ? 0 : bytes / double(sim.clients.size());
    const auto& ss = sim.server.replica().stats();
    rep.serverBytesPerClientPerSecond = sim.clients.empty() ? 0 : double(ss.snapshotBytes + ss.reliableBytes) / sim.seconds() / double(sim.clients.size());
    rep.server = sim.server.stats();
    if (!late) rep.lateJoinSame = true;
    return rep;
}

std::string LoadReport::text() const {
    char b[1024];
    std::snprintf(b, sizeof b,
                  "server tick: avg %.3f ms, worst %.3f ms (%zu bodies)\n"
                  "bytes per client per second: %.0f received (server sent %.0f)\n"
                  "prediction: %llu reconciles, %llu corrections > 1 cm (%.1f %%), mean %.4f m, max %.3f m\n"
                  "convergence: worst body error %.4f m, missing %zu\n"
                  "late join: %s (worst %.4f m, missing %zu)\n"
                  "server: %llu inputs, %llu idle steps, %llu pushes moved %llu bodies, %llu boxes spawned, %llu refused, %llu bad messages\n",
                  tickAvgMs, tickWorstMs, bodies, bytesPerClientPerSecond, serverBytesPerClientPerSecond, (unsigned long long)reconciles,
                  (unsigned long long)corrections, reconciles ? 100.0 * double(corrections) / double(reconciles) : 0.0, meanCorrection, maxCorrection, worstConvergence, missing, lateJoinSame ? "same objects" : "DIFFERENT",
                  lateJoinWorst, lateJoinMissing, (unsigned long long)server.inputs, (unsigned long long)server.idleSteps,
                  (unsigned long long)server.pushes, (unsigned long long)server.pushedBodies, (unsigned long long)server.spawned,
                  (unsigned long long)server.spawnDenied, (unsigned long long)server.badMessages);
    return b;
}
}
