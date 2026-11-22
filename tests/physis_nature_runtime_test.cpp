// PHYSIS data-driven integration: automatic install from the map "physis"
// block for every GameModule, the "physis.presentation" config and the pure
// PHYSIS -> TerraWeave vegetation wind conversions used by PhysisNatureRuntime.
#include "engine/physis/PhysisAutoInstall.h"
#include "engine/physis/PhysisProjectConfig.h"
#include "engine/world/PhysisNatureRuntimeConfig.h"
#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace aiverse;
namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS RUNTIME FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

// Any genre: this module knows nothing about PHYSIS unless asked to.
class PlainModule final : public GameModule {
public:
    explicit PlainModule(bool installItself = false) : self_(installItself) {}
    std::string name() const override { return "plain"; }
    void setup(Simulation& sim, const Json&, const Json& map) override {
        if (self_) (void)physis::installFromMap(sim, map);
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&, Archive& archive) override { return archive.ok(); }
private:
    bool self_;
};

Json mapWith(const char* extra = "") {
    std::string text = std::string(R"({"physis":{"grid":{"originX":0,"originZ":0,"width":4,"height":3,
        "cellSizeMeters":32,"seed":7},"climate":{"baseTemperature":18,"baseHumidity":0.6},
        "enableDisasters":true)") + extra + "}}";
    return Json::parse(text);
}

std::unique_ptr<Simulation> make(const Json& map, bool self = false) {
    SimConfig cfg; cfg.seed = 11;
    auto sim = std::make_unique<Simulation>(cfg, std::make_unique<PlainModule>(self));
    sim->setup(Json::object(), map);
    return sim;
}

void autoInstall() {
    // No block: nothing changes, no PHYSIS state, no system.
    auto none = make(Json::object());
    CHECK(!none->world().resourceConst<physis::State>());
    CHECK(none->systemCount() == 0);

    auto a = make(mapWith()), b = make(mapWith());
    const auto* state = a->world().resourceConst<physis::State>();
    CHECK(state && state->settings.width == 4 && state->settings.height == 3);
    CHECK(a->systemCount() == 1);
    for (int i = 0; i < 25; ++i) { a->step(); b->step(); }
    CHECK(a->world().resourceConst<physis::State>()->tick == 25);
    CHECK(a->world().resourceConst<physis::DisasterState>() != nullptr);
    CHECK(a->checksum() == b->checksum());   // same data -> same nature, every host
    // Saves round-trip through the auto-registered persistence schema.
    const auto bytes = a->serialize();
    auto c = make(mapWith());
    std::string error;
    CHECK(c->deserialize(bytes, &error));
    if (!error.empty()) std::printf("deserialize: %s\n", error.c_str());
    CHECK(c->checksum() == a->checksum());

    // Opt-outs and the module-owned path.
    auto off = make(mapWith(R"(,"autoInstall":false)"));
    CHECK(!off->world().resourceConst<physis::State>());
    auto disabled = make(mapWith(R"(,"enabled":false)"));
    CHECK(!disabled->world().resourceConst<physis::State>());
    auto self = make(mapWith(), true);           // module installed it: not twice
    CHECK(self->world().resourceConst<physis::State>());
    CHECK(self->systemCount() == 1);
    auto tracked = make(mapWith(R"(,"trackedFields":true)"));
    CHECK(tracked->world().resourceConst<physis::RuntimeFieldRegistry>() != nullptr);

    bool threw = false;
    try { (void)make(mapWith(R"(,"autoInstall":1)")); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { (void)make(Json::parse(R"({"physis":{"grid":{}}})")); } catch (const std::exception&) { threw = true; }
    CHECK(threw);   // malformed block is a setup error, as for a module install
}

void presentationConfig() {
    using world::natureRuntimeConfigFromMap;
    CHECK(!natureRuntimeConfigFromMap(Json::object()));
    CHECK(!natureRuntimeConfigFromMap(mapWith(R"(,"enabled":false)")));
    CHECK(!natureRuntimeConfigFromMap(mapWith(R"(,"presentation":false)")));
    const auto d = natureRuntimeConfigFromMap(mapWith());
    CHECK(d && d->fx && d->audio && d->vegetationWind && d->waterWind && d->sky);
    CHECK(d && d->radiusMeters == 160 && d->windFieldNodes == 16);
    const auto c = natureRuntimeConfigFromMap(mapWith(
        R"(,"presentation":{"audio":false,"sky":false,"radiusMeters":300,"windFieldNodes":0,"fxWindScale":0.5})"));
    CHECK(c && c->fx && !c->audio && !c->sky && c->radiusMeters == 300 && c->windFieldNodes == 0);
    CHECK(c && c->fxWindScale == 0.5f);
    for (const char* bad : {R"(,"presentation":{"fx":1})", R"(,"presentation":{"radiusMeters":1})",
                            R"(,"presentation":{"windFieldNodes":1})", R"(,"presentation":"yes")"}) {
        bool threw = false;
        try { (void)natureRuntimeConfigFromMap(mapWith(bad)); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
}

world::NaturePresentationFrame windFrame(float wx, float wz) {
    world::NaturePresentationFrame f;
    f.valid = true; f.cellSize = 32; f.originX = -2; f.originZ = 1; f.width = 4; f.height = 3;
    const std::size_t n = 12;
    for (auto* v : {&f.fire, &f.charred, &f.wetness, &f.snow, &f.elevation}) v->assign(n, 0.f);
    f.windX.assign(n, wx); f.windZ.assign(n, wz); f.present.assign(n, 1);
    f.windX[11] = wx * 2;   // one gusty corner cell
    f.meanWindX = wx; f.meanWindZ = wz;
    return f;
}

void vegetationWind() {
    terraweave::ForestWindState base; base.cycleTicks = 77;
    const auto w = world::forestWindFromNature(windFrame(5, -2), base);
    CHECK(w.xCmPerSec == 500 && w.zCmPerSec == -200);
    CHECK(w.cycleTicks == 77);                       // host sway settings kept
    CHECK(w.gustPermille > 250 && w.gustPermille <= 900);
    const auto still = world::forestWindFromNature(world::NaturePresentationFrame{}, base);
    CHECK(still == base);
    const auto huge = world::forestWindFromNature(windFrame(900, 0));
    CHECK(huge.xCmPerSec == 5000);                   // clamped to TerraWeave's limit

    const auto field = world::forestWindFieldFromNature(windFrame(5, -2), 8);
    // 4x3 cells of 32 m: 8 nodes across X, fewer along the shorter Z.
    CHECK(field && field->columns == 8 && field->rows == 7 && field->samples.size() == 56);
    CHECK(field && field->originXcm == -2 * 3200 && field->originZcm == 3200);
    CHECK(field && field->samples.front().xCmPerSec == 500);
    CHECK(field && field->samples.back().xCmPerSec > 500);   // the gusty corner shows
    const auto again = world::forestWindFieldFromNature(windFrame(5, -2), 8);
    CHECK(world::sameForestWindField(field.get(), again.get()));
    CHECK(!world::forestWindFieldFromNature(windFrame(5, -2), 0));
    CHECK(!world::forestWindFieldFromNature(world::NaturePresentationFrame{}, 8));
}

// End-to-end: auto-installed PHYSIS World -> presentation frame -> wind.
void liveFrame() {
    auto sim = make(mapWith(R"(,"enableWindZones":true)"));
    for (int i = 0; i < 40; ++i) sim->step();
    world::NaturePresentationPolicy p; p.centerX = 64; p.centerZ = 48; p.radiusMeters = 200;
    const auto frame = world::buildNaturePresentation(sim->world(), p);
    CHECK(frame.valid && frame.width == 4 && frame.height == 3);
    const auto field = world::forestWindFieldFromNature(frame, 4);
    CHECK(field && field->columns == 4 && field->rows == 4);   // 96 m / 42.7 m -> 4 nodes
}
} // namespace

int main() {
    autoInstall();
    presentationConfig();
    vegetationWind();
    liveFrame();
    if (failures) { std::printf("PHYSIS nature runtime: %d FAIL\n", failures); return 1; }
    std::printf("PHYSIS nature runtime: PASS\n");
    return 0;
}
