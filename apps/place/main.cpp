// aiverse_place: multiplayer physics sandbox on net::replica + physics::Scene (docs/replica.md, First milestone).
//   serve  --scene FILE [--port N] [--ws-port N] [--seconds S]     authoritative server (UDP, optional WebSocket)
//   client --connect ADDR [--lang en|ru]                           graphical client (desktop builds)
//   bots   --connect ADDR [--count N] [--seconds S]                headless bot clients over the network
//   load   [--scene FILE] [--clients N] [--seconds S] [--profile 4g] [--late S]   everything in one process over SimNet
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include "apps/place/PlaceSim.h"
#include "engine/net/Udp.h"
#include "engine/net/WebSocket.h"

using namespace aiverse;
using namespace aiverse::place;

#ifdef AIVERSE_PLACE_CLIENT
int runPlaceClient(const std::string& address, const std::string& language, const char* argv0);   // PlaceView.cpp
#endif

namespace {
struct Args {
    std::vector<std::string> v;
    std::string get(const std::string& key, const std::string& def) const {
        for (size_t i = 1; i + 1 < v.size(); ++i) if (v[i] == key) return v[i + 1];
        return def;
    }
    double num(const std::string& key, double def) const {
        std::string s = get(key, "");
        if (s.empty()) return def;
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (!end || *end) throw std::runtime_error("invalid number for " + key + ": " + s);
        return d;
    }
};

const char* kUsage =
    "usage: aiverse_place serve --scene FILE [--port 27300] [--ws-port N] [--seconds S]\n"
    "       aiverse_place client --connect ADDR [--lang en|ru]\n"
    "       aiverse_place bots --connect ADDR [--count 8] [--seconds 60]\n"
    "       aiverse_place load [--scene FILE] [--clients 16] [--seconds 30] [--profile 4g] [--late 20]\n";

Json readScene(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read scene " + path);
    std::string text((std::istreambuf_iterator<char>(f)), {});
    return Json::parse(text);
}

int serve(const Args& a) {
    std::string file = a.get("--scene", "");
    if (file.empty()) throw std::runtime_error(kUsage);
    uint16_t port = uint16_t(a.num("--port", 27300));
    int wsPort = int(a.num("--ws-port", 0));
    double seconds = a.num("--seconds", 0);
    PlaceServer server(readScene(file));
    net::UdpListener udp;
    net::WsListener ws;
    if (!udp.open(port)) throw std::runtime_error("cannot bind UDP port " + std::to_string(port));
    if (wsPort && !ws.open(uint16_t(wsPort))) throw std::runtime_error("cannot bind WebSocket port " + std::to_string(wsPort));
    std::printf("place server: UDP %u%s%s, %d Hz, %zu bodies\n", unsigned(port), wsPort ? ", WebSocket " : "",
                wsPort ? std::to_string(wsPort).c_str() : "", server.scene().tickRate(), server.bodies().size());
    std::fflush(stdout);
    const double step = 1000.0 / server.scene().tickRate();
    uint64_t start = net::nowMs(), previous = start, lastReport = start;
    double acc = 0;
    uint64_t lastBytes = 0;
    while (!seconds || double(net::nowMs() - start) < seconds * 1000) {
        uint64_t now = net::nowMs();
        acc += double(now - previous);
        previous = now;
        for (auto& c : udp.poll(now)) if (!server.attach(std::move(c))) std::printf("server full\n");
        if (wsPort) for (auto& c : ws.poll(now)) server.attach(std::move(c));
        int budget = 8;
        while (acc >= step && budget--) { server.update(now); acc -= step; }
        if (budget < 0) acc = 0;   // fell behind: drop time rather than spiral
        if (now - lastReport >= 5000) {
            const auto& s = server.stats();
            const auto& rs = server.replica().stats();
            uint64_t bytes = rs.snapshotBytes + rs.reliableBytes;
            size_t players = server.players();
            std::printf("players %zu, bodies %zu, tick avg %.3f ms worst %.3f ms, %.0f B/s per client, spawned %llu refused %llu\n", players,
                        server.bodies().size(), s.tickMsAvg(), s.tickMsWorst,
                        players ? double(bytes - lastBytes) / 5.0 / double(players) : 0.0, (unsigned long long)s.spawned,
                        (unsigned long long)s.spawnDenied);
            std::fflush(stdout);
            server.resetTickTiming();
            lastBytes = bytes;
            lastReport = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}

int bots(const Args& a) {
    std::string address = a.get("--connect", "");
    if (address.empty()) throw std::runtime_error(kUsage);
    int count = int(a.num("--count", 8));
    double seconds = a.num("--seconds", 60);
    std::vector<SimEntry> list;
    for (int i = 0; i < count; ++i) {
        auto conn = net::connectTo(address);
        if (!conn) throw std::runtime_error("invalid address " + address);
        list.push_back({std::make_unique<PlaceClient>(std::move(conn)), std::make_unique<Bot>(uint32_t(1000 + i)), 0});
    }
    const double step = 1000.0 / 60;
    uint64_t start = net::nowMs(), previous = start, lastReport = start;
    double acc = 0;
    auto report = [&](const char* what) {
        int ready = 0;
        double rtt = 0, snaps = 0, in = 0, mean = 0;
        uint64_t corr = 0, rec = 0;
        float worst = 0;
        for (auto& e : list) {
            const auto& s = e.client->stats();
            ready += e.client->ready();
            rtt += s.rttMs; snaps += s.snapshotsPerSecond; in += s.bytesInPerSecond;
            corr += s.corrections; rec += s.reconciles; mean += s.sumCorrection; worst = std::max(worst, s.maxCorrection);
        }
        double n = std::max<double>(1, double(list.size()));
        std::printf("%s: %d/%zu ready, rtt %.0f ms, %.1f snapshots/s, %.0f B/s in per client, %llu corrections > 1 cm of %llu, mean %.4f m, max %.3f m\n",
                    what, ready, list.size(), rtt / n, snaps / n, in / n, (unsigned long long)corr, (unsigned long long)rec, rec ? mean / double(rec) : 0.0,
                    worst);
        std::fflush(stdout);
    };
    while (double(net::nowMs() - start) < seconds * 1000) {
        uint64_t now = net::nowMs();
        acc += double(now - previous);
        previous = now;
        int budget = 8;
        while (acc >= step && budget--) {
            for (auto& e : list) {
                e.client->update(now, e.bot->think(*e.client, true));
                e.client->advanceSmoothing(float(step));
            }
            acc -= step;
        }
        if (budget < 0) acc = 0;
        if (now - lastReport >= 5000) { report("bots"); lastReport = now; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    report("final");
    return 0;
}

int load(const Args& a) {
    LoadOptions o;
    o.clients = int(a.num("--clients", o.clients));
    o.seconds = a.num("--seconds", o.seconds);
    o.profile = a.get("--profile", o.profile);
    o.lateJoinAt = a.num("--late", o.seconds > 20 ? 20 : o.seconds * 2 / 3);
    bool found = false;
    net::linkPreset(o.profile, &found);
    if (!found) throw std::runtime_error("unknown link profile " + o.profile);
    std::string file = a.get("--scene", "examples/place/scene.json");
    Json scene = readScene(file);
    std::printf("load: %d clients over SimNet \"%s\", %.0f s, late join at %.0f s, scene %s\n", o.clients, o.profile.c_str(), o.seconds, o.lateJoinAt,
                file.c_str());
    std::fflush(stdout);
    auto r = runLoad(scene, o);
    std::printf("%s", r.text().c_str());
    return r.allReady && r.lateJoinSame ? 0 : 1;
}

int command(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) a.v.push_back(argv[i]);
    std::string cmd = a.v.empty() ? "" : a.v[0];
    if (cmd == "serve") return serve(a);
    if (cmd == "bots") return bots(a);
    if (cmd == "load") return load(a);
    if (cmd == "client") {
#ifdef AIVERSE_PLACE_CLIENT
        std::string address = a.get("--connect", "");
        if (address.empty()) throw std::runtime_error(kUsage);
        return runPlaceClient(address, a.get("--lang", ""), argv[0]);
#else
        throw std::runtime_error("this build has no graphical client (desktop builds only)");
#endif
    }
    throw std::runtime_error(kUsage);
}
}

#ifdef AIVERSE_PLACE_CLIENT
int aiverseMain(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        return command(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
}
