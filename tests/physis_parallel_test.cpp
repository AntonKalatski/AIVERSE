// PHYSIS parallel grid update: every lane count must give a World that is
// bit-identical to the serial update (atmosphere, surface, fire, flood, quake),
// for storms + fronts + climate zones + flood + fire, with COW snapshots on.
// With an argument "bench [size] [ticks]" it also measures ms/tick per lane
// count (used for the measured speedup in docs/physis.md).
#include "engine/physis/PhysisIntegration.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS PARALLEL FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

class Host final : public GameModule {
public:
    Host(int size, unsigned lanes, bool cow) : size_(size), lanes_(lanes), cow_(cow) {}
    std::string name() const override { return "physis-parallel-host"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        if (cow_) sim.world().enableCowSnapshots(true);
        InstallOptions o;
        o.climate.width = size_; o.climate.height = size_ * 3 / 4;
        o.climate.originX = -size_ / 2; o.climate.originZ = -7;
        o.climate.cellSizeMeters = 64; o.climate.seed = 99;
        o.climate.baseHumidity = Fixed::fromRatio(7, 10);
        o.enableDisasters = true; o.enableStorms = true;
        o.workerThreads = lanes_;
        install(sim, o);
        World& w = sim.world();
        const auto* st = w.resourceConst<State>();
        for (std::size_t i = 0; i < st->cells.size(); ++i) {
            const Entity e = st->cells[i];
            auto* c = w.tryGet<Cell>(e);
            c->elevation = Fixed::fromRaw(std::int64_t(mix64(i) & 0xfffff) * 9);
            if (i % 7 == 0) w.tryGet<HazardCell>(e)->waterDepth = Fixed::fromRatio(std::int64_t(i % 13), 4);
            if (i % 3 == 0) {
                ClimateZone z; z.prevailingWind = {Fixed::fromInt(6), Fixed::fromInt(-3)};
                z.temperatureOffset = Fixed::fromInt(int(i % 11) - 5);
                w.add<ClimateZone>(e, z);
            }
        }
        for (int k = 0; k < 6; ++k) {
            Entity f = w.create();
            WeatherFront front;
            front.center = {Fixed::fromInt(-size_ / 2 + k * 9), Fixed::fromInt(k * 5)};
            front.velocity = {Fixed::fromRatio(1, 4), Fixed::fromRatio(-1, 8)};
            front.humidityDelta = Fixed::fromRatio(1, 5);
            front.precipitationBoost = Fixed::fromRatio(1, 3);
            front.windDelta = {Fixed::fromInt(4), Fixed::fromInt(2)};
            w.add<WeatherFront>(f, front);
        }
        Entity s = w.create();
        StormEvent storm; storm.center = {Fixed::fromInt(500), Fixed{}, Fixed::fromInt(900)};
        storm.driftPerTick = {Fixed::fromInt(30), Fixed{}, Fixed::fromInt(-10)};
        w.add<StormEvent>(s, storm);
        Entity q = w.create();
        QuakeEvent quake; quake.centerX = 3; quake.centerZ = 4; quake.radiusCells = Fixed::fromInt(20);
        quake.magnitude = Fixed::fromRatio(3, 4); quake.startTick = 5; quake.durationTicks = 10;
        w.add<QuakeEvent>(q, quake);
        for (int k = 0; k < 40; ++k)
            (void)ignite(w, -size_ / 2 + int(mix64(k) % std::uint64_t(size_)),
                         -7 + int(mix64(k + 99) % std::uint64_t(size_ * 3 / 4)), Fixed::fromInt(1));
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher&) override {}
    int result(Simulation&) override { return 0; }
private:
    int size_; unsigned lanes_; bool cow_;
};

std::unique_ptr<Simulation> make(int size, unsigned lanes, bool cow = false) {
    auto sim = std::make_unique<Simulation>(SimConfig{}, std::make_unique<Host>(size, lanes, cow));
    sim->setup(Json::object(), Json::object());
    return sim;
}

bool sameWorld(const World& a, const World& b) {
    const auto* sa = a.resourceConst<State>();
    const auto* sb = b.resourceConst<State>();
    if (!sa || !sb || sa->tick != sb->tick || sa->cells.size() != sb->cells.size()) return false;
    for (std::size_t i = 0; i < sa->cells.size(); ++i) {
        const auto* aa = a.tryGetConst<Atmosphere>(sa->cells[i]);
        const auto* ab = b.tryGetConst<Atmosphere>(sb->cells[i]);
        const auto* xa = a.tryGetConst<Surface>(sa->cells[i]);
        const auto* xb = b.tryGetConst<Surface>(sb->cells[i]);
        const auto* ha = a.tryGetConst<HazardCell>(sa->cells[i]);
        const auto* hb = b.tryGetConst<HazardCell>(sb->cells[i]);
        if (!aa || !ab || !xa || !xb || !ha || !hb) return false;
        if (aa->temperature != ab->temperature || aa->humidity != ab->humidity ||
            aa->pressure != ab->pressure || aa->wind.x != ab->wind.x || aa->wind.y != ab->wind.y ||
            aa->rain != ab->rain || aa->snow != ab->snow) return false;
        if (xa->wetness != xb->wetness || xa->snowpack != xb->snowpack || xa->ice != xb->ice) return false;
        if (ha->fuel != hb->fuel || ha->fire != hb->fire || ha->waterDepth != hb->waterDepth ||
            ha->quake != hb->quake) return false;
    }
    return true;
}

void identical() {
    for (const bool cow : {false, true}) {
        auto serial = make(160, 1, cow);
        std::vector<std::unique_ptr<Simulation>> lanes;
        for (unsigned n : {2u, 3u, 4u, 8u, 0u}) lanes.push_back(make(160, n, cow));
        SimSnapshot keep;   // a live snapshot shares COW pages during the run
        for (int t = 0; t < 30; ++t) {
            if (t == 10) serial->saveState(keep);
            serial->step();
            for (auto& s : lanes) s->step();
        }
        for (auto& s : lanes) {
            CHECK(sameWorld(serial->world(), s->world()));
            CHECK(serial->checksum() == s->checksum());
        }
        // Something actually burned and flowed.
        bool fire = false;
        const auto* st = serial->world().resourceConst<State>();
        for (auto e : st->cells) if (serial->world().tryGetConst<HazardCell>(e)->fuel < Fixed::fromInt(1)) fire = true;
        CHECK(fire);
        if (cow) CHECK(keep.tick == 10);
    }
}

void executorContract() {
    ThreadPoolCellExecutor pool(4);
    CHECK(pool.lanes() >= 1);
    std::vector<int> hits(100000, 0);
    pool.run(hits.size(), 1000, [&](std::size_t b, std::size_t e) { for (auto i = b; i < e; ++i) ++hits[i]; });
    bool once = true;
    for (int h : hits) once = once && h == 1;
    CHECK(once);
    bool threw = false;
    try {
        pool.run(50000, 100, [](std::size_t b, std::size_t) { if (b >= 20000) throw std::runtime_error("lane"); });
    } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    // Usable again after an exception.
    int total = 0; std::mutex m;
    pool.run(10000, 500, [&](std::size_t b, std::size_t e) { std::lock_guard<std::mutex> l(m); total += int(e - b); });
    CHECK(total == 10000);
    // Small passes stay serial, no executor = serial.
    {
        ScopedCellExecutor scope(&pool);
        CHECK(!cellPassIsParallel(100));
        CHECK(cellPassIsParallel(1 << 20) == (pool.lanes() > 1));
    }
    CHECK(!cellPassIsParallel(1 << 20));
}

double msPerTick(int size, unsigned lanes, int ticks) {
    auto sim = make(size, lanes);
    sim->step();   // warm-up
    const auto t0 = std::chrono::steady_clock::now();
    for (int t = 0; t < ticks; ++t) sim->step();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / ticks;
}

void bench(int size, int ticks) {
    std::printf("PHYSIS bench grid %dx%d (%d cells), storms+disasters, %d ticks\n",
                size, size * 3 / 4, size * size * 3 / 4, ticks);
    const double base = msPerTick(size, 1, ticks);
    std::printf("  lanes 1: %.2f ms/tick\n", base);
    for (unsigned n : {2u, 4u, 6u, 8u}) {
        const double ms = msPerTick(size, n, ticks);
        std::printf("  lanes %u: %.2f ms/tick  speedup x%.2f\n", n, ms, base / ms);
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
        bench(argc > 2 ? std::atoi(argv[2]) : 1024, argc > 3 ? std::atoi(argv[3]) : 10);
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "checksum") == 0) {   // A/B against older sources
        auto sim = make(160, 1);
        for (int t = 0; t < 30; ++t) sim->step();
        std::printf("%llu\n", static_cast<unsigned long long>(sim->checksum()));
        return 0;
    }
    executorContract();
    identical();
    if (failures) { std::printf("PHYSIS parallel: %d FAIL\n", failures); return 1; }
    std::printf("PHYSIS parallel: PASS\n");
    return 0;
}
