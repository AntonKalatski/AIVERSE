// NEREID sphere water: radial sampling, currents, priority, buoyancy and save/restore in a host module.
#include <aiverselib/water/NereidSphereWater.h>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace aiverse;
namespace nw = aiverse::lib::nereid;
static void check(bool v, const char* why) { if (!v) throw std::runtime_error(why); }

class Host final : public GameModule {
public:
    Entity boat = kNullEntity;
    std::string name() const override { return "nereid-sphere-host"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        nw::registerNereidWaterPersistence(sim);
        nw::registerNereidBuoyancyPersistence(sim);
        nw::registerNereidSphereWaterPersistence(sim);
        auto& w = sim.world();
        const Entity sea = w.create();
        nw::WaterSphereComponent s;
        s.center = {1000_fx, Fixed{}, Fixed{}};
        s.surfaceRadius = 80_fx; s.floorRadius = 70_fx; s.currentSpeed = 2_fx;
        w.add<nw::WaterSphereComponent>(sea, s);
        boat = w.create();
        Transform t; t.pos = {1000_fx, Fixed{}, 79_fx};   // 1 m under the surface, on the +Z side
        w.add<Transform>(boat, t);
        w.add<nw::WaterProbeComponent>(boat);
        w.add<nw::WaterSphereContactComponent>(boat);
        nw::WaterBuoyancyComponent b; b.buoyancyAcceleration = 10_fx; b.immersionHeight = 2_fx; b.linearDrag = Fixed{};
        w.add<nw::WaterBuoyancyComponent>(boat, b);
        w.add<nw::WaterVelocityInputComponent>(boat);
        w.add<nw::WaterForceComponent>(boat);
        nw::addWaterProbeFromTransformSystem(sim, "nereid.probe");
        nw::addSphereWaterSamplingSystem(sim, "nereid.sphere.sample");
        nw::addSphereWaterBuoyancySystem(sim, "nereid.sphere.buoyancy");
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&, Archive& a) override { return a.ok(); }
};

int main() {
    try {
        nw::WaterSphereComponent s;
        s.center = {}; s.surfaceRadius = 10_fx; s.floorRadius = 4_fx;
        const Entity e = Entity{};
        // below the surface along any axis: submerged, radial up, radial depth
        auto in = nw::sampleWaterSphere(e, s, {Fixed{}, -9_fx, Fixed{}});
        check(in.found && in.submerged && in.depth == 1_fx && in.up.y == -1_fx, "south pole sample");
        auto above = nw::sampleWaterSphere(e, s, {12_fx, Fixed{}, Fixed{}});
        check(above.found && !above.submerged && above.depth == Fixed{}, "above the surface");
        auto deep = nw::sampleWaterSphere(e, s, {3_fx, Fixed{}, Fixed{}});
        check(!deep.submerged, "below the floor");
        check(!nw::sampleWaterSphere(e, s, {}).found, "centre has no direction");
        // current: eastward about +Y; at +Z the east direction is +X
        s.currentSpeed = 3_fx;
        auto cur = nw::sampleWaterSphere(e, s, {Fixed{}, Fixed{}, 9_fx});
        check(cur.flow.x == 3_fx && cur.flow.y == Fixed{} && cur.flow.z == Fixed{}, "eastward current");
        // buoyancy points along up at the south pole
        nw::WaterBuoyancyComponent b; b.buoyancyAcceleration = 10_fx; b.immersionHeight = 2_fx; b.linearDrag = Fixed{};
        auto f = nw::computeSphereWaterForces(in, b, Vec3{});
        check(f.acceleration.y < Fixed{} && f.immersedFraction == Fixed::fromRatio(1, 2), "radial buoyancy");

        // host: systems, determinism, save/restore
        auto run = [](int ticks, std::string* save) {
            auto mod = std::make_unique<Host>();
            Host* h = mod.get();
            Simulation sim(SimConfig{7, 20, 0, true}, std::move(mod));
            sim.setup(Json::object(), Json::object());
            for (int i = 0; i < ticks; ++i) sim.step();
            const auto* c = sim.world().tryGetConst<nw::WaterSphereContactComponent>(h->boat);
            const auto* fc = sim.world().tryGetConst<nw::WaterForceComponent>(h->boat);
            check(c && c->sample.submerged && c->sample.depth == 1_fx, "host contact");
            check(fc && fc->acceleration.z > Fixed{}, "host buoyancy along +Z");
            if (save) *save = sim.serialize();
            return sim.checksum();
        };
        std::string a, b2;
        const uint64_t c1 = run(10, &a), c2 = run(10, &b2);
        check(c1 == c2 && a == b2, "deterministic");
        std::cout << "nereid sphere water: ok\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << "\n";
        return 1;
    }
}
