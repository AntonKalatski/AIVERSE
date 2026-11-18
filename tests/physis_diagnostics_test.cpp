#include "engine/physis/PhysisDiagnostics.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::physis;

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS DIAGNOSTICS FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

void testRasterAndInvalidInputs() {
    World world;
    DiagnosticRequest req;
    DiagnosticRaster raster;
    raster.tick = 999;
    CHECK(!sampleDiagnosticRaster(world, req, raster));
    CHECK(raster.tick == 999);
    Settings settings;
    settings.originX = -2;
    settings.originZ = -1;
    settings.width = 4;
    settings.height = 3;
    settings.cellSizeMeters = 100;
    initialize(world, settings);
    req.originX = -3;
    req.originZ = -2;
    req.width = 6;
    req.height = 5;
    req.stride = 1;
    req.layer = DiagnosticLayer::TemperatureCelsius;
    CHECK(sampleDiagnosticRaster(world, req, raster));
    CHECK(raster.width == 6 && raster.height == 5);
    CHECK(raster.sampleCount == 12);
    CHECK(!raster.valid[0]);
    CHECK(raster.valid[1 * 6 + 1]);
    CHECK(raster.tick == 0);
    CHECK(raster.maxRaw >= raster.minRaw);

    const auto before = raster;
    req.width = -1;
    CHECK(!sampleDiagnosticRaster(world, req, raster));
    CHECK(raster.sampleCount == before.sampleCount);
    req.width = 6;
    req.stride = 0;
    CHECK(!sampleDiagnosticRaster(world, req, raster));
    req.stride = 1;
    req.width = 100000;
    req.height = 100000;
    CHECK(!sampleDiagnosticRaster(world, req, raster));
    req.width = 6;
    req.height = 5;
    req.originX = 2147483647;
    CHECK(!sampleDiagnosticRaster(world, req, raster));
}

void testLayersAndDeterminism() {
    World a, b;
    Settings settings;
    settings.originX = -1; settings.originZ = -1;
    settings.width = 2; settings.height = 2;
    initialize(a, settings);
    initialize(b, settings);
    initializeDisasters(a);
    initializeDisasters(b);
    CHECK(ignite(a, -1, -1));
    CHECK(ignite(b, -1, -1));
    DiagnosticRequest req;
    req.originX = -1; req.originZ = -1;
    req.width = 2; req.height = 2;
    req.layer = DiagnosticLayer::Fire;
    DiagnosticRaster ar, br;
    CHECK(sampleDiagnosticRaster(a, req, ar));
    CHECK(sampleDiagnosticRaster(b, req, br));
    CHECK(ar.sampleCount == 4);
    CHECK(ar.rawValues == br.rawValues);
    CHECK(ar.minRaw == 0 && ar.maxRaw == Fixed::kOne);
    CHECK(diagnosticNormalized(ar, 0) == 65535);
    CHECK(diagnosticNormalized(ar, 1) == 0);
    CHECK(diagnosticNormalized(ar, 999) == 0);

    req.layer = DiagnosticLayer::GravityMagnitude;
    CHECK(sampleDiagnosticRaster(a, req, ar));
    CHECK(ar.sampleCount == 4);
    CHECK(ar.minRaw == ar.maxRaw);
    CHECK(diagnosticNormalized(ar, 0) == 0);

    req.layer = DiagnosticLayer::VerticalWindMetersPerSecond;
    WindZone zone;
    zone.center = {-64_fx, 0_fx, -64_fx};
    zone.radiusMeters = 200_fx;
    zone.velocity = {0_fx, 10_fx, 0_fx};
    a.add<WindZone>(a.create(), zone);
    CHECK(sampleDiagnosticRaster(a, req, ar));
    CHECK(ar.maxRaw > 0);
    World checkpoint = a;
    advance(a);
    CHECK(sampleDiagnosticRaster(a, req, ar));
    a = checkpoint;
    CHECK(sampleDiagnosticRaster(a, req, br));
    CHECK(br.tick == 0);
    CHECK(br.rawValues != ar.rawValues || br.tick != ar.tick);
}
void testStrideAndReadOnly() {
    World world;
    Settings s;
    s.width = 5; s.height = 5;
    initialize(world, s);
    DiagnosticRequest req;
    req.width = 5; req.height = 5; req.stride = 2;
    req.layer = DiagnosticLayer::SurfaceWetness;
    DiagnosticRaster raster;
    CHECK(sampleDiagnosticRaster(world, req, raster));
    CHECK(raster.width == 3 && raster.height == 3);
    CHECK(raster.sampleCount == 9);
    CHECK(world.resourceConst<State>()->tick == 0);
    CHECK(!raster.valid.empty());
}
}

int main() {
    testRasterAndInvalidInputs();
    testLayersAndDeterminism();
    testStrideAndReadOnly();
    std::printf("PHYSIS Diagnostics: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
