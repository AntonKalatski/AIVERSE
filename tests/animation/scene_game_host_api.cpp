// Stage 176: compile-time native game-host integration fixture.
// No GPU device required. Full CMake/CTest and actual scene load exercise
// are deferred to the coordinated Mac acceptance campaign.
#include "engine/view/MotionWeaveSceneGameHost.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace aiverse;

namespace {
// Deliberately not called; forces the C++ compiler/linker to instantiate
// the REAL SceneLoadCoordinator::commitReady() adapter, not just check
// method names through requires expressions.
bool instantiateCommitPath(view::MotionWeaveSceneGameHost& host,
                           scene::SceneTicket ticket) {
    auto build=[](const scene::SceneAsset&,
                  view::MotionWeaveSceneGameHost::Owner&,
                  std::vector<view::MotionWeaveSceneGameHost::Object>&,
                  std::string&) -> bool {
        return false; // no GPU allocations in a compile fixture
    };
    view::MotionWeaveSceneGameHost::Statistics stats;
    std::string error;
    return host.commitReady(ticket,build,stats,&error);
}
} // namespace

int main() {
    using Host=view::MotionWeaveSceneGameHost;
    static_assert(!std::is_copy_constructible_v<Host>);
    static_assert(std::is_constructible_v<
        Host,Simulation&,scene::SceneLoadCoordinator&,gfx::Device&,
        render::RenderDeviceLifetime::Lease>);
    static_assert(std::is_same_v<
        decltype(std::declval<Host&>().request(std::string{})),
        scene::SceneTicket>);
    static_assert(std::is_same_v<
        decltype(std::declval<Host&>().restoreResidencyAfterRollback()),
        std::vector<scene::SceneTicket>>);
    static_assert(std::is_same_v<
        decltype(std::declval<Host&>().collectRetired()),bool>);
    static_assert(std::is_same_v<
        decltype(std::declval<Host&>().completeForShutdown()),bool>);
    (void)&instantiateCommitPath;

    // Real canonical SceneState types from main, with persistent and
    // preparing records. Only Active scenes can retain visuals on Replace.
    scene::SceneState states;
    states.scenes.emplace(12,scene::SceneRecord{
        "scenes/active",scene::ScenePhase::Active,true});
    states.scenes.emplace(13,scene::SceneRecord{
        "scenes/loading",scene::ScenePhase::Loading,false});
    states.scenes.emplace(14,scene::SceneRecord{
        "scenes/other",scene::ScenePhase::Active,false});
    const auto ids=
        view::MotionWeaveSceneCoordinatorAdapter::
            canonicalActiveSceneIds(&states);
    assert((ids==std::vector<uint64_t>{12,14}));
    std::puts("MotionWeave typed scene host contract OK");
}
