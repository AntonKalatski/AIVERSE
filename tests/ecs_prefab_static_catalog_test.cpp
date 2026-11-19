// Native, genre-independent prefab immutable configuration integration test.
// No dependency on the unverified SceneRuntime branch: the same spawn API
// is used by a future SceneLoadCoordinator::SpawnPrefab callback.
#include "engine/sim/PrefabStaticCatalog.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace sample {
struct UnitConfig {
    int32_t maxHealth = 0;
    int32_t range = 0;
    std::array<int32_t,30> modifiers{};
    bool operator==(const UnitConfig&) const = default;
};
inline void io(aiverse::Archive& archive,UnitConfig& value) {
    archive(value.maxHealth,value.range);
    for(auto& item:value.modifiers)archive(item);
}
inline UnitConfig make(int32_t hp) {
    UnitConfig config;
    config.maxHealth=hp;
    config.range=hp/10;
    for(size_t i=0;i<config.modifiers.size();++i)
        config.modifiers[i]=hp+int32_t(i);
    return config;
}
} // namespace sample

using namespace aiverse;
namespace st=aiverse::storage;
using Catalog=st::PrefabStaticCatalog<sample::UnitConfig>;

static std::shared_ptr<const Catalog> makeCatalog(bool reverse=false,
                                                  bool changedAlias=false) {
    st::PrefabStaticCatalogBuilder<sample::UnitConfig> builder;
    // Authoring order MUST NOT determine IDs or content hashes.
    if(reverse) {
        AIVERSE_TEST_CHECK(builder.add("units/mage",
            sample::make(changedAlias?200:100)));
        AIVERSE_TEST_CHECK(builder.add("units/knight",sample::make(200)));
        AIVERSE_TEST_CHECK(builder.add("units/guard",sample::make(200)));
        AIVERSE_TEST_CHECK(builder.add("units/archer",sample::make(100)));
    } else {
        AIVERSE_TEST_CHECK(builder.add("units/archer",sample::make(100)));
        AIVERSE_TEST_CHECK(builder.add("units/guard",sample::make(200)));
        AIVERSE_TEST_CHECK(builder.add("units/knight",sample::make(200)));
        AIVERSE_TEST_CHECK(builder.add("units/mage",
            sample::make(changedAlias?200:100)));
    }
    AIVERSE_TEST_CHECK(!builder.add("units/archer",sample::make(999)));
    AIVERSE_TEST_CHECK(!builder.add("../escape",sample::make(999)));
    AIVERSE_TEST_CHECK(!builder.add("units//bad",sample::make(999)));
    auto result=builder.seal();
    bool rejected=false;
    try {builder.add("units/extra",sample::make(1));}
    catch(const std::logic_error&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    return result;
}

static void stableCatalogAndScenes() {
    const auto defs=makeCatalog();
    const auto defsReverse=makeCatalog(true);
    AIVERSE_TEST_CHECK(defs->fingerprint()==defsReverse->fingerprint());
    AIVERSE_TEST_CHECK(defs->table().fingerprint()==defsReverse->table().fingerprint());
    AIVERSE_TEST_CHECK(defs->uniqueConfigCount()==2 && defs->prefabCount()==4);
    AIVERSE_TEST_CHECK(defs->find("units/archer").slot==1);
    AIVERSE_TEST_CHECK(defs->find("units/guard").slot==2);
    AIVERSE_TEST_CHECK(defs->find("units/knight")==defs->find("units/guard"));
    AIVERSE_TEST_CHECK(defs->find("units/mage")==defs->find("units/archer"));
    AIVERSE_TEST_CHECK(!defs->find("units/missing").valid());

    World game, preview;
    AIVERSE_TEST_CHECK(defs->bindWorld(game));
    AIVERSE_TEST_CHECK(defs->bindWorld(preview));
    AIVERSE_TEST_CHECK(defs->bound(game)&&defs->bound(preview));
    // Emulate two additive scenes, each with many instances of the same
    // prefab, within one authoritative World.
    for(int scene=0;scene<2;++scene) {
        for(int i=0;i<5000;++i) {
            auto e=game.create();
            AIVERSE_TEST_CHECK(defs->attach(game,e,
                (i%3==0)?"units/archer":"units/guard"));
            AIVERSE_TEST_CHECK(defs->read(game,e)->maxHealth ==
                ((i%3==0)?100:200));
        }
    }
    const Entity ePreview=preview.create();
    AIVERSE_TEST_CHECK(defs->attach(preview,ePreview,"units/guard"));
    const Entity eGame=game.create();
    AIVERSE_TEST_CHECK(defs->attach(game,eGame,"units/guard"));
    AIVERSE_TEST_CHECK(defs->read(game,eGame)==defs->read(preview,ePreview));
    AIVERSE_TEST_CHECK(defs->read(game,eGame)==&defs->table().get({2}));
    AIVERSE_TEST_CHECK(!defs->attach(game,eGame,"units/archer"));
    AIVERSE_TEST_CHECK(!defs->attach(game,game.create(),"units/missing"));
    AIVERSE_TEST_CHECK(defs->validateWorld(game));
    AIVERSE_TEST_CHECK(defs->validateWorld(preview));
    AIVERSE_TEST_CHECK(game.countExisting<st::StaticRef<sample::UnitConfig>>()==10001);
    AIVERSE_TEST_CHECK(defs->table().logicalPayloadBytes()==
                      sizeof(sample::UnitConfig)*2);

    World snapshot=game;
    auto extra=game.create();
    AIVERSE_TEST_CHECK(defs->attach(game,extra,"units/archer"));
    AIVERSE_TEST_CHECK(game.countExisting<st::StaticRef<sample::UnitConfig>>()==10002);
    game.copyFrom(snapshot);
    AIVERSE_TEST_CHECK(!game.alive(extra));
    AIVERSE_TEST_CHECK(defs->validateWorld(game));

    // Same unique table values, but changed alias. A save must detect
    // that prefab->config associations no longer match.
    const auto changed=makeCatalog(false,true);
    AIVERSE_TEST_CHECK(changed->table().fingerprint()==defs->table().fingerprint());
    AIVERSE_TEST_CHECK(changed->fingerprint()!=defs->fingerprint());
    AIVERSE_TEST_CHECK(!changed->bindWorld(game));
    AIVERSE_TEST_CHECK(!changed->validateWorld(game));
}

class PrefabModule final : public GameModule {
public:
    explicit PrefabModule(std::shared_ptr<const Catalog> catalog)
      : catalog_(std::move(catalog)) {}
    std::string name()const override {return "prefab-static-native";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        st::registerPrefabStaticPersistence<sample::UnitConfig>(
            sim,"test.prefab.config",1);
        AIVERSE_TEST_CHECK(catalog_->bindWorld(sim.world()));
        for(int i=0;i<40;++i) {
            Entity entity=sim.world().create();
            AIVERSE_TEST_CHECK(catalog_->attach(sim.world(),entity,
                (i%2)?"units/guard":"units/archer"));
        }
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    bool serializeState(Simulation&,Archive&)override{return true;}
    int result(Simulation&)override{return 0;}
private:
    std::shared_ptr<const Catalog> catalog_;
};
static std::unique_ptr<Simulation> makeSimulation(
    const std::shared_ptr<const Catalog>& catalog) {
    auto sim=std::make_unique<Simulation>(SimConfig{77,20,0,true},
        std::make_unique<PrefabModule>(catalog));
    sim->setup(Json::object(),Json::object());
    return sim;
}
static void independentHostsPersistenceAndRollback() {
    auto catalog=makeCatalog();
    auto game=makeSimulation(catalog);
    auto separate=makeSimulation(catalog);
    AIVERSE_TEST_CHECK(catalog->validateWorld(game->world()));
    AIVERSE_TEST_CHECK(catalog->validateWorld(separate->world()));
    const uint64_t original=game->checksum();
    AIVERSE_TEST_CHECK(original==separate->checksum());
    const std::string serialized=game->serialize();
    AIVERSE_TEST_CHECK(!serialized.empty());

    SimSnapshot checkpoint;
    game->saveState(checkpoint);
    Entity additional=game->world().create();
    AIVERSE_TEST_CHECK(catalog->attach(game->world(),additional,"units/mage"));
    AIVERSE_TEST_CHECK(game->checksum()!=original);
    game->loadState(checkpoint);
    AIVERSE_TEST_CHECK(game->checksum()==original);
    AIVERSE_TEST_CHECK(catalog->validateWorld(game->world()));
    AIVERSE_TEST_CHECK(separate->checksum()==original);

    auto restored=makeSimulation(catalog);
    std::string error;
    AIVERSE_TEST_CHECK(restored->deserialize(serialized,&error));
    AIVERSE_TEST_CHECK(restored->checksum()==original);
    AIVERSE_TEST_CHECK(catalog->validateWorld(restored->world()));

    auto changed=makeCatalog(false,true);
    // Loading an old save into a world with the new prefab alias binding
    // must be explicitly rejected by the host before simulation resumes.
    AIVERSE_TEST_CHECK(!changed->validateWorld(restored->world()));
    AIVERSE_TEST_CHECK(!changed->bindWorld(restored->world()));
}

int main() {
    stableCatalogAndScenes();
    independentHostsPersistenceAndRollback();
    std::cout<<"PASS prefab static configs across scenes/worlds, deterministic mapping, "
                "native save, rollback and changed-alias rejection\n";
}
