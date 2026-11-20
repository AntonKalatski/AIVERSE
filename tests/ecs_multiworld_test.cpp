#include "engine/scene/SimulationHost.h"
#include "tests/AlwaysCheck.h"
#include <iostream>
#include <memory>

using namespace aiverse;
namespace sc = aiverse::scene;

struct Counter { uint32_t value = 0; };
class CounterModule final : public GameModule {
public:
    std::string name() const override { return "multiworld-host-test"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        Entity e = sim.world().create();
        sim.world().add<Counter>(e);
        sim.addSystem("counter", [e](Simulation& s) {
            ++s.world().get<Counter>(e).value;
        });
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation& sim, Hasher& h) override {
        sim.world().eachExistingConst<Counter>([&](Entity e, const Counter& counter) {
            h.u64(e.packed());
            h.u32(counter.value);
        });
    }
    int result(Simulation&) override { return 0; }
};
static std::unique_ptr<Simulation> make(uint64_t seed) {
    auto sim = std::make_unique<Simulation>(SimConfig{seed,20,0,true},
                                            std::make_unique<CounterModule>());
    sim->setup(Json::object(), Json::object());
    return sim;
}
static uint32_t value(const Simulation& s) {
    uint32_t result = 0;
    s.world().eachExistingConst<Counter>([&](Entity, const Counter& c) { result = c.value; });
    return result;
}
int main() {
    sc::SimulationHost host;
    const sc::WorldHandle a = host.insert(make(11));
    const sc::WorldHandle b = host.insert(make(22));
    AIVERSE_TEST_CHECK(a.valid() && b.valid() && a != b);
    AIVERSE_TEST_CHECK(host.size() == 2);
    // Same raw Entity may exist in different worlds: never exchange one without WorldHandle.
    const sc::WorldEntityRef refA{a, Entity{0,0}};
    const sc::WorldEntityRef refB{b, Entity{0,0}};
    AIVERSE_TEST_CHECK(refA.entity == refB.entity);
    AIVERSE_TEST_CHECK(refA != refB && host.alive(refA) && host.alive(refB));
    host.stepAll();
    AIVERSE_TEST_CHECK(value(*host.get(a)) == 1 && value(*host.get(b)) == 1);
    AIVERSE_TEST_CHECK(host.setPaused(a, true));
    host.stepAll();
    AIVERSE_TEST_CHECK(value(*host.get(a)) == 1 && value(*host.get(b)) == 2);
    AIVERSE_TEST_CHECK(host.setPaused(a, false));
    const auto* stablePointer = host.get(b);
    host.advanceAll(0.2);
    AIVERSE_TEST_CHECK(host.get(b) == stablePointer);
    AIVERSE_TEST_CHECK(value(*host.get(a)) > 1 && value(*host.get(b)) > 2);
    const uint64_t unaffected = host.get(b)->checksum();
    AIVERSE_TEST_CHECK(host.erase(a));
    AIVERSE_TEST_CHECK(!host.contains(a) && !host.alive(refA));
    AIVERSE_TEST_CHECK(host.get(b)->checksum() == unaffected);
    const sc::WorldHandle reused = host.insert(make(33));
    AIVERSE_TEST_CHECK(reused.index == a.index && reused.generation != a.generation);
    AIVERSE_TEST_CHECK(!host.alive(refA) && !host.contains(a));
    AIVERSE_TEST_CHECK(host.alive(sc::WorldEntityRef{reused, Entity{0,0}}));
    AIVERSE_TEST_CHECK(!host.erase(a));
    AIVERSE_TEST_CHECK(host.erase(reused) && host.erase(b));
    AIVERSE_TEST_CHECK(host.size() == 0);
    std::cout << "PASS multiple independent Simulations, pause, WorldEntityRef and stale handles\n";
}
