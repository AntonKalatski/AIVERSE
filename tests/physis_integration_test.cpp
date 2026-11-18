#include "engine/physis/PhysisIntegration.h"
#include <memory>
#include <cstdio>
#include <stdexcept>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures=0;
#define CHECK(c) do { if(!(c)){std::printf("PHYSIS SIM FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;} } while(0)
class GenericHost final : public GameModule {
    FloodMode floodMode_ = FloodMode::LocalShallow;
public:
    explicit GenericHost(FloodMode floodMode = FloodMode::LocalShallow) : floodMode_(floodMode) {}
    std::string name() const override { return "physis-generic-host"; }
    void setup(Simulation& simulation, const Json&, const Json&) override {
        InstallOptions options;
        options.climate.width=2;
        options.climate.height=1;
        options.climate.seed=123;
        options.enableDisasters=true;
        options.floodMode=floodMode_;
        install(simulation, options);
        (void)ignite(simulation.world(),0,0,Fixed::fromInt(1));
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&,Archive& archive) override { return archive.ok(); }
};
std::unique_ptr<Simulation> make(FloodMode floodMode = FloodMode::LocalShallow) {
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<GenericHost>(floodMode));
    sim->setup(Json::object(),Json::object());
    return sim;
}
void serializationAndHost() {
    auto one=make(),two=make();
    for(int i=0;i<10;++i){one->step();two->step();}
    CHECK(one->world().resourceConst<State>()->tick==10);
    CHECK(one->checksum()==two->checksum());
    const auto save=one->serialize();
    const auto checksum=one->checksum();
    const auto before=one->world().resourceConst<DisasterState>()->lastProcessedTick;
    CHECK(before==10);
    auto loaded=make();
    std::string error;
    CHECK(loaded->deserialize(save,&error));
    CHECK(loaded->checksum()==checksum);
    CHECK(loaded->world().resourceConst<State>()->tick==10);
    CHECK(loaded->world().resourceConst<DisasterState>()->lastProcessedTick==10);
    HazardCell restored{},original{};
    CHECK(hazardAt(one->world(),0,0,original));
    CHECK(hazardAt(loaded->world(),0,0,restored));
    CHECK(restored.fire==original.fire);
    int samples=0;
    forEachPresentationCell(static_cast<const World&>(loaded->world()),
        [&](const PresentationCell& p){CHECK(p.hasHazards);++samples;});
    CHECK(samples==2);
    // Both peers continue the same deterministic world after file persistence.
    one->step();loaded->step();
    CHECK(one->checksum()==loaded->checksum());
}
void externalAuthorityHostAndPersistence() {
    auto one=make(FloodMode::ExternalAuthority);
    auto two=make(FloodMode::ExternalAuthority);
    one->world().get<HazardCell>(cellAt(one->world(),0,0)).waterDepth=Fixed::fromInt(3);
    two->world().get<HazardCell>(cellAt(two->world(),0,0)).waterDepth=Fixed::fromInt(3);
    for (int i=0; i<4; ++i) { one->step(); two->step(); }
    CHECK(one->checksum()==two->checksum());
    HazardCell h{};
    CHECK(hazardAt(one->world(),0,0,h));
    CHECK(h.waterDepth==Fixed{});
    CHECK(h.fire>Fixed{});
    auto saved=one->serialize();
    auto restored=make(FloodMode::ExternalAuthority);
    std::string error;
    CHECK(restored->deserialize(saved,&error));
    CHECK(restored->checksum()==one->checksum());
    one->step(); restored->step();
    CHECK(restored->checksum()==one->checksum());
}

void snapshot() {
    auto s=make();
    s->step();
    SimSnapshot snapshot;
    s->saveState(snapshot);
    const auto expected=s->checksum();
    s->step();s->step();
    s->loadState(snapshot);
    CHECK(s->checksum()==expected);
    CHECK(s->world().resourceConst<State>()->tick==1);
}
}
int main(){
    serializationAndHost();
    snapshot();
    externalAuthorityHostAndPersistence();
    std::printf("PHYSIS integration: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
