// PHYSIS chunk streaming: dormant chunks are frozen, active chunks match the
// unstreamed simulation's rules, flood never leaks into dormant cells, the
// mask follows moving anchors, saves round-trip, lanes stay bit-identical,
// and map data configures it. "bench [size] [ticks]" measures the saving.
#include "engine/physis/PhysisProjectConfig.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS STREAMING FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

struct Setup { int size = 128; std::int32_t chunk = 0; unsigned lanes = 1; bool anchor = true; Fixed radius = Fixed::fromInt(1000); };

class Host final : public GameModule {
public:
    explicit Host(Setup s) : s_(s) {}
    std::string name() const override { return "physis-streaming-host"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        InstallOptions o;
        o.climate.width = s_.size; o.climate.height = s_.size;
        o.climate.cellSizeMeters = 64; o.climate.seed = 5;
        o.climate.baseHumidity = Fixed::fromRatio(4, 5);
        o.enableDisasters = true;
        o.workerThreads = s_.lanes;
        o.streamingChunkCells = s_.chunk;
        if (s_.chunk && s_.anchor) o.anchors.push_back({{Fixed::fromInt(600), Fixed{}, Fixed::fromInt(600)}, s_.radius});
        install(sim, o);
        World& w = sim.world();
        const auto* st = w.resourceConst<State>();
        for (std::size_t i = 0; i < st->cells.size(); ++i) {
            const Entity e = st->cells[i];
            w.tryGet<Cell>(e)->elevation = Fixed::fromRaw(std::int64_t(mix64(i) & 0xfffff) * 7);
            if (i % 5 == 0) w.tryGet<HazardCell>(e)->waterDepth = Fixed::fromRatio(std::int64_t(i % 9), 4);
        }
        for (int k = 0; k < 30; ++k)
            (void)ignite(w, int(mix64(k) % std::uint64_t(s_.size)), int(mix64(k + 7) % std::uint64_t(s_.size)));
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&, Archive& a) override { return a.ok(); }
private:
    Setup s_;
};

std::unique_ptr<Simulation> make(Setup s) {
    auto sim = std::make_unique<Simulation>(SimConfig{}, std::make_unique<Host>(s));
    sim->setup(Json::object(), Json::object());
    return sim;
}

bool sameCell(const World& a, const World& b, std::size_t i) {
    const Entity ea = a.resourceConst<State>()->cells[i], eb = b.resourceConst<State>()->cells[i];
    const auto* aa = a.tryGetConst<Atmosphere>(ea); const auto* ab = b.tryGetConst<Atmosphere>(eb);
    const auto* sa = a.tryGetConst<Surface>(ea); const auto* sb = b.tryGetConst<Surface>(eb);
    const auto* ha = a.tryGetConst<HazardCell>(ea); const auto* hb = b.tryGetConst<HazardCell>(eb);
    return aa->temperature == ab->temperature && aa->humidity == ab->humidity &&
           aa->wind.x == ab->wind.x && aa->rain == ab->rain && sa->wetness == sb->wetness &&
           sa->snowpack == sb->snowpack && ha->fire == hb->fire && ha->fuel == hb->fuel &&
           ha->waterDepth == hb->waterDepth;
}
bool sameAll(const World& a, const World& b) {
    const auto n = a.resourceConst<State>()->cells.size();
    for (std::size_t i = 0; i < n; ++i) if (!sameCell(a, b, i)) return false;
    return true;
}

void noAnchorsMeansEverything() {
    Setup plain; plain.chunk = 0;
    Setup streamed; streamed.chunk = 16; streamed.anchor = false;
    auto a = make(plain), b = make(streamed);
    for (int t = 0; t < 20; ++t) { a->step(); b->step(); }
    CHECK(b->world().resourceConst<ActiveChunks>()->all);
    CHECK(sameAll(a->world(), b->world()));
}

void dormantChunksFreeze() {
    Setup s; s.chunk = 16;   // 8x8 chunks of 1024 m; anchor (600,600) r=1000 -> 4 chunks
    auto sim = make(s);
    auto initial = make(s);    // untouched copy to compare frozen cells with
    for (int t = 0; t < 25; ++t) sim->step();
    const auto* mask = sim->world().resourceConst<ActiveChunks>();
    CHECK(mask && !mask->all);
    std::size_t activeChunks = 0;
    for (auto v : mask->active) activeChunks += v;
    CHECK(activeChunks == 4);
    CHECK(activeCellCount(sim->world()) == 4u * 16u * 16u);
    const auto n = sim->world().resourceConst<State>()->cells.size();
    std::size_t frozenOk = 0, frozen = 0, changed = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (cellSimulated(mask, s.size, i)) { if (!sameCell(sim->world(), initial->world(), i)) ++changed; }
        else { ++frozen; if (sameCell(sim->world(), initial->world(), i)) ++frozenOk; }
    }
    CHECK(frozen == n - 4u * 256u && frozenOk == frozen);   // no water/fire/weather leaked in
    CHECK(changed > 900);

    // Move the anchor across the map: the mask follows deterministically.
    const auto before = mask->revision;
    sim->world().eachExisting<ActivityAnchor>([](Entity, ActivityAnchor& a) {
        a.position = {Fixed::fromInt(7000), Fixed{}, Fixed::fromInt(7000)};
    });
    sim->step();
    mask = sim->world().resourceConst<ActiveChunks>();
    CHECK(mask->revision == before + 1);
    CHECK(!cellSimulated(mask, s.size, 0));
    CHECK(cellSimulated(mask, s.size, std::size_t(127) * 128 + 127));

    // Save / restore carries anchors and mask; checksum equal.
    const auto bytes = sim->serialize();
    auto restored = make(s);
    std::string error;
    CHECK(restored->deserialize(bytes, &error));
    CHECK(restored->checksum() == sim->checksum());
    restored->step(); sim->step();
    CHECK(restored->checksum() == sim->checksum());
}

void lanesStayIdentical() {
    Setup serial; serial.size = 192; serial.chunk = 32; serial.radius = Fixed::fromInt(4000);
    Setup lanes = serial; lanes.lanes = 4;
    auto a = make(serial), b = make(lanes);
    for (int t = 0; t < 20; ++t) { a->step(); b->step(); }
    CHECK(sameAll(a->world(), b->world()));
    CHECK(a->checksum() == b->checksum());
}

void mapConfig() {
    const auto map = Json::parse(R"({"physis":{"grid":{"originX":0,"originZ":0,"width":64,"height":64,
        "cellSizeMeters":32,"seed":1},"climate":{"baseTemperature":10,"baseHumidity":0.5},
        "workerThreads":2,"streaming":{"chunkCells":16},
        "anchors":[{"x":100,"z":200,"radiusMeters":300}]}})");
    const auto o = optionsFromMap(map);
    CHECK(o && o->streamingChunkCells == 16 && o->anchors.size() == 1 && o->workerThreads == 2);
    CHECK(o && o->anchors[0].radiusMeters == Fixed::fromInt(300));
    bool threw = false;
    try { (void)optionsFromMap(Json::parse(R"({"physis":{"grid":{"originX":0,"originZ":0,"width":4,"height":4,
        "cellSizeMeters":32,"seed":1},"climate":{"baseTemperature":10,"baseHumidity":0.5},
        "anchors":[{"x":1,"z":2,"radiusMeters":3}]}})")); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

double msPerTick(Setup s, int ticks) {
    auto sim = make(s);
    sim->step();
    const auto t0 = std::chrono::steady_clock::now();
    for (int t = 0; t < ticks; ++t) sim->step();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / ticks;
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
        const int size = argc > 2 ? std::atoi(argv[2]) : 1024, ticks = argc > 3 ? std::atoi(argv[3]) : 10;
        Setup full; full.size = size;
        Setup streamed = full; streamed.chunk = 32; streamed.radius = Fixed::fromInt(8000);
        Setup both = streamed; both.lanes = 0;
        const double f = msPerTick(full, ticks), s = msPerTick(streamed, ticks), p = msPerTick(both, ticks);
        auto probe = make(streamed); probe->step();
        std::printf("PHYSIS streaming bench %dx%d cells, active %zu cells\n", size, size,
                    activeCellCount(probe->world()));
        std::printf("  full grid serial  %.2f ms/tick\n  streamed serial   %.2f ms/tick (x%.2f)\n"
                    "  streamed + lanes  %.2f ms/tick (x%.2f)\n", f, s, f / s, p, f / p);
        return 0;
    }
    noAnchorsMeansEverything();
    dormantChunksFreeze();
    lanesStayIdentical();
    mapConfig();
    if (failures) { std::printf("PHYSIS streaming: %d FAIL\n", failures); return 1; }
    std::printf("PHYSIS streaming: PASS\n");
    return 0;
}
