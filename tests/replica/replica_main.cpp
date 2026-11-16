// Test runner + benchmark (`aiverse_replica_tests --bench`): 64 clients, 2000 moving objects.
#include <chrono>
#include <cstring>
#include <random>
#include "tests/replica/ReplicaTest.h"

namespace rt {
int g_failed = 0, g_checks = 0;
std::vector<std::pair<const char*, std::function<void()>>>& registry() {
    static std::vector<std::pair<const char*, std::function<void()>>> r;
    return r;
}

// `moving`: fraction of the objects that move (the rest rest after spawning, like sleeping physics bodies).
// `latencyMs`: one-way loopback latency (0: acks arrive before the next snapshot).
static void bench(float radius, const char* label, float moving = 1.0f, uint32_t latencyMs = 0) {
    using clk = std::chrono::steady_clock;
    ServerOptions so;
    so.defaultRadius = radius;
    so.cellSize = std::max(32.0f, radius / 2);
    LoopbackOptions lo; lo.latencyMs = latencyMs;
    LoopWorld w(so, lo, {}, 0);
    const int kClients = 64, kObjects = 2000;
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-1000, 1000);
    std::vector<ObjectId> ids;
    std::vector<std::array<float, 4>> motion;   // x z heading speed
    for (int i = 0; i < kObjects; ++i) {
        ids.push_back(w.server.create(w.game.prop));
        motion.push_back({u(rng), u(rng), u(rng), 2 + float(i % 7)});
    }
    for (int i = 0; i < kClients; ++i) w.add();
    double stepSec = 0, bcastSec = 0, clientSec = 0, worstTick = 0;
    uint64_t measuredTicks = 0, bytes0 = 0, snaps0 = 0;
    const int warm = 120, total = 720;
    const size_t movers = size_t(float(kObjects) * moving);
    for (int f = 0; f < total; ++f) {
        ++w.ticks;
        w.now = 1000 + w.ticks * 1000 / 60;
        auto t0 = clk::now();
        w.server.poll(w.now);
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i >= movers && i % 31 != 0 && f > 0) continue;   // at rest (viewer objects always move)
            auto& m = motion[i];
            m[2] += 0.01f * float(int(i % 5) - 2);
            m[0] = std::clamp(m[0] + std::cos(m[2]) * m[3] / 60, -1000.0f, 1000.0f);
            m[1] = std::clamp(m[1] + std::sin(m[2]) * m[3] / 60, -1000.0f, 1000.0f);
            w.server.set(ids[i], w.game.bPos, vec(m[0], 0, m[1]));
            w.server.set(ids[i], w.game.bRot, quatYaw(m[2]));
        }
        for (int c = 0; c < kClients; ++c) {
            auto& m = motion[size_t(c * 31)];   // each client looks from one of the movers
            w.server.setViewers(uint8_t(c + 1), {{m[0], 0, m[1]}});
        }
        w.server.tick();
        auto t1 = clk::now();
        w.server.broadcast(w.now);
        auto t2 = clk::now();
        for (auto& c : w.clients) c->poll(w.now);
        auto t3 = clk::now();
        if (f == warm) { bytes0 = w.server.stats().snapshotBytes; snaps0 = w.server.stats().snapshots; }
        if (f >= warm) {
            ++measuredTicks;
            double b = std::chrono::duration<double>(t2 - t1).count();
            stepSec += std::chrono::duration<double>(t1 - t0).count();
            bcastSec += b;
            clientSec += std::chrono::duration<double>(t3 - t2).count();
            worstTick = std::max(worstTick, b);
        }
    }
    const double secs = double(measuredTicks) / 60.0;
    size_t seen = 0;
    for (auto& c : w.clients) seen += c->objects().size();
    const auto& st = w.server.stats();
    std::printf("bench %s: %d clients, %d objects (%.0f %% moving), radius %.0f, 60 Hz ticks, %d snapshots/s\n", label, kClients,
                kObjects, double(moving) * 100, radius, so.snapshotRate);
    std::printf("  server broadcast: %.3f ms/tick avg, %.3f ms worst; poll + game set(): %.3f ms/tick\n", bcastSec / double(measuredTicks) * 1000,
                worstTick * 1000, stepSec / double(measuredTicks) * 1000);
    std::printf("  per client: %.0f bytes/s (budget %d), %.0f objects in view, %.0f bytes/snapshot\n",
                double(st.snapshotBytes - bytes0) / secs / kClients, so.bytesPerSecond, double(seen) / kClients,
                double(st.snapshotBytes - bytes0) / double(std::max<uint64_t>(1, st.snapshots - snaps0)));
    std::printf("  all 64 clients decoding: %.3f ms/tick; starved candidates: %llu\n", clientSec / double(measuredTicks) * 1000,
                (unsigned long long)st.starved);
    std::printf("  object updates per client per second: %.0f, %.1f bytes each; delta-coded fields %.0f %%; relevant objects looked at %.1f %%\n",
                double(st.entries) / (double(total) / 60.0) / kClients, double(st.snapshotBytes) / double(std::max<uint64_t>(1, st.entries)),
                100.0 * double(st.deltaFields) / double(std::max<uint64_t>(1, st.entries)),
                100.0 * double(st.evaluated) / double(std::max<uint64_t>(1, st.relevant)));
}
} // namespace rt

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) {
        const char* only = argc > 2 ? argv[2] : "";
        auto want = [&](const char* k) { return !*only || std::strstr(k, only); };
        if (want("near")) rt::bench(128, "near (radius 128)");
        if (want("crowded")) rt::bench(400, "crowded (radius 400)");
        if (want("latency")) rt::bench(128, "near, 50 ms latency (RTT > snapshot interval)", 1.0f, 50);
        if (want("all")) rt::bench(4000, "everything relevant");
        if (want("rest")) rt::bench(4000, "everything relevant, 10 % + viewers moving (rest)", 0.1f);
        if (want("still")) rt::bench(4000, "everything relevant, 1 % + viewers moving (still)", 0.01f);
        return 0;
    }
    const char* only = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto& [name, fn] : rt::registry()) {
        if (only && !std::strstr(name, only)) continue;
        int before = rt::g_failed;
        auto t0 = std::chrono::steady_clock::now();
        fn();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%s %s (%.0f ms)\n", rt::g_failed == before ? "ok  " : "FAIL", name, ms);
        ++run;
    }
    std::printf("%d tests, %d checks, %d failed\n", run, rt::g_checks, rt::g_failed);
    return rt::g_failed ? 1 : 0;
}
