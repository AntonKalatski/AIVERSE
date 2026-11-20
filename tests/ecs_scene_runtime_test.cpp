#include "engine/scene/SceneRuntime.h"
#include "tests/AlwaysCheck.h"
#include <iostream>
#include <memory>
#include <string>

using namespace aiverse;
namespace sc = aiverse::scene;

static void additiveAndReplacement() {
    World world;
    sc::SceneRuntime scenes(world);
    AIVERSE_TEST_CHECK(scenes.count() == 0);
    const sc::SceneId global = scenes.beginLoad("scenes/persistent", sc::LoadMode::Additive, true);
    const sc::SceneId town = scenes.beginLoad("scenes/town");
    AIVERSE_TEST_CHECK(global.valid() && town.valid() && global != town);
    AIVERSE_TEST_CHECK(scenes.activate(global));
    AIVERSE_TEST_CHECK(scenes.activate(town));

    Entity oldParent = scenes.createEntity(town);
    Entity foreignChild = scenes.createEntity(global);
    Entity noScene = world.create();
    AIVERSE_TEST_CHECK(scenes.ownerOf(noScene) == sc::SceneId{});
    SceneHierarchy graph;
    AIVERSE_TEST_CHECK(graph.attach(world, oldParent));
    AIVERSE_TEST_CHECK(graph.attach(world, foreignChild));
    AIVERSE_TEST_CHECK(graph.setParent(world, foreignChild, oldParent));
    graph.update(world);

    const sc::SceneId replacement = scenes.beginLoad("scenes/desert", sc::LoadMode::Replace);
    AIVERSE_TEST_CHECK(replacement.valid());
    AIVERSE_TEST_CHECK(!scenes.beginLoad("scenes/concurrent", sc::LoadMode::Replace).valid());
    Entity desert = scenes.createEntity(replacement);
    AIVERSE_TEST_CHECK(world.alive(oldParent)); // Do not drop the old scene while streaming.
    AIVERSE_TEST_CHECK(scenes.activate(replacement, &graph));
    AIVERSE_TEST_CHECK(!world.alive(oldParent) && world.alive(foreignChild));
    AIVERSE_TEST_CHECK(world.alive(noScene) && world.alive(desert));
    AIVERSE_TEST_CHECK(!world.has<Parent>(foreignChild)); // Cross-scene child promoted to root.
    AIVERSE_TEST_CHECK(!graph.contains(oldParent) && graph.contains(foreignChild));
    AIVERSE_TEST_CHECK(scenes.count() == 2);
    AIVERSE_TEST_CHECK(scenes.record(global) && scenes.record(replacement));
    AIVERSE_TEST_CHECK(!scenes.record(town));
    AIVERSE_TEST_CHECK(!scenes.activate(town));
    AIVERSE_TEST_CHECK(!scenes.adopt(replacement, foreignChild));
    AIVERSE_TEST_CHECK(scenes.transfer(foreignChild, replacement));
    AIVERSE_TEST_CHECK(scenes.ownerOf(foreignChild) == replacement);

    const sc::SceneId failed = scenes.beginLoad("scenes/broken", sc::LoadMode::Replace);
    Entity partial = scenes.createEntity(failed);
    AIVERSE_TEST_CHECK(world.alive(partial));
    AIVERSE_TEST_CHECK(scenes.cancel(failed, &graph));
    AIVERSE_TEST_CHECK(!world.alive(partial) && world.alive(desert));
    AIVERSE_TEST_CHECK(!scenes.activate(failed));
    const sc::SceneId next = scenes.beginLoad("scenes/next");
    AIVERSE_TEST_CHECK(next.value > failed.value); // No scene handle recycling.
    AIVERSE_TEST_CHECK(scenes.cancel(next, &graph));

    AIVERSE_TEST_CHECK(scenes.unload(replacement, &graph));
    AIVERSE_TEST_CHECK(!world.alive(desert) && !world.alive(foreignChild));
    AIVERSE_TEST_CHECK(world.alive(noScene));
    AIVERSE_TEST_CHECK(scenes.record(global) != nullptr);
    AIVERSE_TEST_CHECK(!scenes.unload(replacement));
}

static void cowRollback() {
    World world;
    sc::SceneRuntime scenes(world);
    const auto island = scenes.beginLoad("scenes/island");
    const Entity tree = scenes.createEntity(island);
    AIVERSE_TEST_CHECK(scenes.activate(island));
    world.enableCowSnapshots(true);
    const World before = world;

    const auto cave = scenes.beginLoad("scenes/cave");
    const Entity rock = scenes.createEntity(cave);
    AIVERSE_TEST_CHECK(scenes.activate(cave));
    AIVERSE_TEST_CHECK(scenes.unload(island));
    AIVERSE_TEST_CHECK(!world.alive(tree));
    AIVERSE_TEST_CHECK(world.alive(rock));

    world.copyFrom(before);
    AIVERSE_TEST_CHECK(world.alive(tree));
    AIVERSE_TEST_CHECK(!world.alive(rock));
    AIVERSE_TEST_CHECK(scenes.record(island) != nullptr);
    AIVERSE_TEST_CHECK(scenes.record(cave) == nullptr);
    AIVERSE_TEST_CHECK(scenes.ownerOf(tree) == island);
    AIVERSE_TEST_CHECK(scenes.count() == 1);
}

static void archiveRoundtrip() {
    sc::SceneState state;
    state.nextId = 3;
    state.pendingReplace = 2;
    state.scenes.emplace(1, sc::SceneRecord{"city", sc::ScenePhase::Active, false});
    state.scenes.emplace(2, sc::SceneRecord{"level2", sc::ScenePhase::Loading, false});
    std::string data;
    Archive write(&data);
    write(state);
    AIVERSE_TEST_CHECK(write.ok());
    sc::SceneState restored;
    Archive read(data);
    read(restored);
    AIVERSE_TEST_CHECK(read.ok() && read.remaining() == 0);
    AIVERSE_TEST_CHECK(restored.pendingReplace == 2);
    AIVERSE_TEST_CHECK(restored.scenes.at(1).asset == "city");
    AIVERSE_TEST_CHECK(restored.scenes.at(2).phase == sc::ScenePhase::Loading);
}

class NativeSceneModule final : public GameModule {
public:
    std::string name() const override { return "scene-native-test"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        sc::registerScenePersistence(sim);
        sc::SceneRuntime runtime(sim.world());
        const sc::SceneId scene = runtime.beginLoad("scenes/arena");
        AIVERSE_TEST_CHECK(scene.valid());
        Entity actor = runtime.createEntity(scene);
        AIVERSE_TEST_CHECK(actor.valid());
        AIVERSE_TEST_CHECK(runtime.activate(scene));
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher&) override {}
    bool serializeState(Simulation&, Archive&) override { return true; }
    int result(Simulation&) override { return 0; }
};

static void simulationPersistence() {
    Simulation original({777, 20, 0, true}, std::make_unique<NativeSceneModule>());
    original.setup(Json::object(), Json::object());
    sc::SceneRuntime runtime(original.world());
    AIVERSE_TEST_CHECK(runtime.count() == 1);
    const uint64_t checksum = original.checksum();
    const std::string serialized = original.serialize();
    AIVERSE_TEST_CHECK(!serialized.empty());

    SimSnapshot saved;
    original.saveState(saved);
    const auto pending = runtime.beginLoad("scenes/temporary", sc::LoadMode::Replace);
    const Entity temporary = runtime.createEntity(pending);
    AIVERSE_TEST_CHECK(runtime.cancel(pending));
    AIVERSE_TEST_CHECK(!original.world().alive(temporary));
    original.loadState(saved);
    AIVERSE_TEST_CHECK(original.checksum() == checksum && runtime.count() == 1);

    Simulation loaded({777, 20, 0, true}, std::make_unique<NativeSceneModule>());
    loaded.setup(Json::object(), Json::object());
    std::string error;
    AIVERSE_TEST_CHECK(loaded.deserialize(serialized, &error));
    AIVERSE_TEST_CHECK(loaded.checksum() == original.checksum());
    sc::SceneRuntime loadedScenes(loaded.world());
    AIVERSE_TEST_CHECK(loadedScenes.count() == 1);
}

int main() {
    additiveAndReplacement();
    cowRollback();
    archiveRoundtrip();
    simulationPersistence();
    std::cout << "PASS scene additive/replace/cancel, hierarchy, COW, binary persistence\n";
}
