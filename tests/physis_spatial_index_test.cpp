#include "engine/physis/PhysisSpatialIndex.h"
#include <cstdio>
#include <vector>
using namespace aiverse;
using namespace aiverse::physis;

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("PHYSIS SPATIAL FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)

void assertParity(const World& world, const SpatialFieldIndex& index) {
    const std::vector<Vec3> samples = {
        {-900_fx, 2_fx, -900_fx}, {-256_fx, 0_fx, -256_fx},
        {-255_fx, 0_fx, -250_fx}, {-1_fx, 0_fx, -1_fx},
        {0_fx, 0_fx, 0_fx}, {10_fx, 100_fx, 10_fx},
        {200_fx, 10_fx, 200_fx}, {257_fx, 5_fx, 259_fx},
        {1000_fx, 0_fx, 1000_fx}, {5000_fx, 600_fx, 5000_fx},
        {25000_fx, 0_fx, -25000_fx}
    };
    const auto* state = world.resourceConst<State>();
    for (const Vec3 p : samples) {
        CHECK(indexedWindAt(world, index, p) == windAt(world, p));
        CHECK(indexedGravityAt(world, index, p) == gravityAt(world, p));
        CHECK(indexedWindWithStormsAt(world, index, p) ==
              windWithStormsAt(world, p));
        const StormInfluence reference = stormsAt(world, p, state->tick);
        const StormInfluence indexed = indexedStormsAt(world, index, p, state->tick);
        CHECK(indexed.wind == reference.wind);
        CHECK(indexed.precipitationBoost == reference.precipitationBoost);
        CHECK(indexed.pressureOffset == reference.pressureOffset);
        NatureFieldSample oldSample, indexedSample;
        CHECK(sampleNatureField(world, p, oldSample));
        CHECK(sampleNatureFieldIndexed(world, index, p, indexedSample));
        CHECK(oldSample.tick == indexedSample.tick);
        CHECK(oldSample.hasAtmosphere == indexedSample.hasAtmosphere);
        CHECK(oldSample.hasHazards == indexedSample.hasHazards);
        CHECK(oldSample.windVelocity == indexedSample.windVelocity);
        CHECK(oldSample.gravityAcceleration == indexedSample.gravityAcceleration);
        CHECK(oldSample.storms.wind == indexedSample.storms.wind);
        CHECK(oldSample.storms.pressureOffset == indexedSample.storms.pressureOffset);
    }
}

void testMixedGlobalAndLocal() {
    World world;
    Settings settings;
    settings.originX = -10;
    settings.originZ = -10;
    settings.width = 20;
    settings.height = 20;
    settings.cellSizeMeters = 128;
    initialize(world, settings);
    initializeDisasters(world);
    for (int i = 0; i < 20; ++i) {
        const Entity entity = world.create();
        WindZone zone;
        zone.center = {Fixed::fromInt(i * 200 - 1900), Fixed{}, Fixed::fromInt(i * 100 - 900)};
        zone.radiusMeters = 125_fx;
        zone.velocity = {Fixed::fromInt(i), Fixed::fromInt(i % 3), Fixed::fromInt(-i)};
        zone.gustAmplitude = 5_fx;
        zone.seed = static_cast<std::uint64_t>(i + 1);
        world.add<WindZone>(entity, zone);
    }
    for (int i = 0; i < 6; ++i) {
        WindShelter shelter;
        shelter.center = {Fixed::fromInt(i * 250 - 650), Fixed{}, Fixed{}};
        shelter.radiusMeters = 260_fx;
        shelter.attenuation = Fixed::fromRatio(1, i + 2);
        world.add<WindShelter>(world.create(), shelter);
    }
    GravityField uniform;
    uniform.mode = GravityMode::Uniform;
    uniform.radiusMeters = Fixed{}; // global
    uniform.acceleration = {0_fx, 1_fx, 0_fx};
    world.add<GravityField>(world.create(), uniform);
    GravityField attract;
    attract.mode = GravityMode::Attract;
    attract.center = {-250_fx, 200_fx, -250_fx};
    attract.radiusMeters = 500_fx;
    attract.radialMagnitude = 30_fx;
    world.add<GravityField>(world.create(), attract);
    GravityField moon;
    moon.overrideBaseline = true;
    moon.priority = 10;
    moon.radiusMeters = 2000_fx;
    moon.acceleration = {0_fx, Fixed::fromRatio(-162, 100), 0_fx};
    world.add<GravityField>(world.create(), moon);

    StormEvent hurricane;
    hurricane.center = {0_fx, 0_fx, 0_fx};
    hurricane.outerRadiusMeters = 8000_fx; // intentionally global fallback
    hurricane.eyeRadiusMeters = 100_fx;
    hurricane.heightMeters = 2000_fx;
    hurricane.driftPerTick = {10_fx, 0_fx, 0_fx};
    world.add<StormEvent>(world.create(), hurricane);
    StormEvent tornado;
    tornado.kind = StormKind::Tornado;
    tornado.center = {-250_fx, 0_fx, -250_fx};
    tornado.eyeRadiusMeters = 20_fx;
    tornado.outerRadiusMeters = 300_fx;
    tornado.heightMeters = 200_fx;
    tornado.updraft = 100_fx;
    world.add<StormEvent>(world.create(), tornado);

    SpatialFieldIndex index;
    CHECK(index.rebuild(world));
    CHECK(index.sameTick(world));
    CHECK(index.wind.volumeCount() == 20);
    CHECK(index.gravity.globalCount() >= 1);
    CHECK(index.storms.globalCount() >= 1);
    CHECK(index.wind.bucketCount() > 1);
    assertParity(world, index);

    // Typical isolated region checks fewer candidates than full traversal.
    std::size_t visited = 0;
    index.wind.visit({0_fx, 0_fx, 0_fx}, [&](Entity, const WindZone&) { ++visited; });
    CHECK(visited < index.wind.volumeCount());

    // Index-backed diagnostic raster is identical to the reference for all
    // scalar channels; expensive 3D fields use the bounded candidate visits.
    const DiagnosticLayer layers[] = {
        DiagnosticLayer::TemperatureCelsius, DiagnosticLayer::RelativeHumidity,
        DiagnosticLayer::PressureHpa, DiagnosticLayer::Rain, DiagnosticLayer::Snow,
        DiagnosticLayer::SurfaceWetness, DiagnosticLayer::SnowpackMeters,
        DiagnosticLayer::SurfaceIce, DiagnosticLayer::WindSpeedMetersPerSecond,
        DiagnosticLayer::VerticalWindMetersPerSecond,
        DiagnosticLayer::GravityMagnitude, DiagnosticLayer::Fire,
        DiagnosticLayer::Quake, DiagnosticLayer::LocalFloodMeters
    };
    for (const auto layer : layers) {
        DiagnosticRequest req;
        req.originX = -3; req.originZ = -3;
        req.width = 6; req.height = 6;
        req.stride = 2;
        req.layer = layer;
        DiagnosticRaster reference, indexedRaster;
        CHECK(sampleDiagnosticRaster(world, req, reference));
        CHECK(sampleDiagnosticRasterIndexed(world, index, req, indexedRaster));
        CHECK(reference.rawValues == indexedRaster.rawValues);
        CHECK(reference.valid == indexedRaster.valid);
        CHECK(reference.minRaw == indexedRaster.minRaw);
        CHECK(reference.maxRaw == indexedRaster.maxRaw);
        CHECK(reference.sampleCount == indexedRaster.sampleCount);
        CHECK(reference.tick == indexedRaster.tick);
    }

    const World checkpoint = world;
    advanceEnvironmentWithStormsAndDisasters(world, FloodMode::ExternalAuthority);
    CHECK(!index.sameTick(world));
    // Stale index falls back to authoritative reference samplers.
    assertParity(world, index);
    CHECK(index.rebuild(world));
    assertParity(world, index);
    world = checkpoint; // rollback must rebuild, even when tick happens to match.
    CHECK(index.rebuild(world));
    assertParity(world, index);
    CHECK(world.resourceConst<State>()->tick == 0);
}

void testBudgetAndEmpty() {
    World world;
    SpatialFieldIndex index;
    CHECK(!index.rebuild(world));
    NatureFieldSample result;
    result.tick = 999;
    CHECK(!sampleNatureFieldIndexed(world, index, {}, result));
    CHECK(result.tick == 999);

    Settings config;
    config.width = 1; config.height = 1;
    initialize(world, config);
    GravityField huge;
    huge.mode = GravityMode::Uniform;
    huge.radiusMeters = 10000000_fx;
    huge.acceleration = {0_fx, 0_fx, 1_fx};
    world.add<GravityField>(world.create(), huge);
    CHECK(index.rebuild(world));
    CHECK(index.gravity.globalCount() == 1);
    CHECK(index.gravity.assignmentCount() == 0);
    assertParity(world, index);

    WindZone outside;
    outside.center = {-10000_fx, 0_fx, 0_fx};
    outside.radiusMeters = 100_fx;
    outside.velocity = {20_fx, 0_fx, 0_fx};
    world.add<WindZone>(world.create(), outside);
    // A volume inserted on the same tick needs a new snapshot: no revision token.
    CHECK(index.rebuild(world));
    CHECK(indexedWindAt(world, index, {-10000_fx, 0_fx, 0_fx}) ==
          windAt(world, {-10000_fx, 0_fx, 0_fx}));
}
}
int main() {
    testMixedGlobalAndLocal();
    testBudgetAndEmpty();
    std::printf("PHYSIS spatial: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
