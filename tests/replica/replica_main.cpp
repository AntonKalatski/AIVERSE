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

static void bench(float radius, const char* label) {
    using clk = std::chrono::steady_clock;
    ServerOptions so;
    so.defaultRadius = radius;
    so.cellSize = std::max(32.0f, radius / 2);
    LoopWorld w(so, {}, {}, 0);
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
    for (int f = 0; f < total; ++f) {
        ++w.ticks;
        w.now = 1000 + w.ticks * 1000 / 60;
        auto t0 = clk::now();
        w.server.poll(w.now);
        for (size_t i = 0; i < ids.size(); ++i) {
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
    std::printf("bench %s: %d clients, %d objects, radius %.0f, 60 Hz ticks, %d snapshots/s\n", label, kClients, kObjects, radius,
                so.snapshotRate);
    std::printf("  server broadcast: %.3f ms/tick avg, %.3f ms worst; poll + game set(): %.3f ms/tick\n", bcastSec / double(measuredTicks) * 1000,
                worstTick * 1000, stepSec / double(measuredTicks) * 1000);
    std::printf("  per client: %.0f bytes/s (budget %d), %.0f objects in view, %.0f bytes/snapshot\n",
                double(st.snapshotBytes - bytes0) / secs / kClients, so.bytesPerSecond, double(seen) / kClients,
                double(st.snapshotBytes - bytes0) / double(std::max<uint64_t>(1, st.snapshots - snaps0)));
    std::printf("  all 64 clients decoding: %.3f ms/tick; starved candidates: %llu\n", clientSec / double(measuredTicks) * 1000,
                (unsigned long long)st.starved);
}
} // namespace rt

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) {
        rt::bench(128, "near (radius 128)");
        rt::bench(400, "crowded (radius 400)");
        rt::bench(4000, "everything relevant");
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
