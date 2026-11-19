#!/usr/bin/env bash
# AIVERSE opt-in StaticScene integration validation.
# Usage: bash tools/ecs-static-scene-validate.sh [build-dir] [smoke|extended] [jobs]
# Never edits Git refs. A separate worktree with the desired branch must
# already be checked out. Results must be copied to the agent handoff.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build_dir="${1:-build/static-scene}"
mode="${2:-extended}"
jobs="${3:-8}"
case "$mode" in
    smoke|extended) ;;
    *) echo "Expected mode smoke or extended, got: $mode" >&2; exit 2 ;;
esac
if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    echo "Invalid positive build jobs count: $jobs" >&2
    exit 2
fi

echo "Repository: $root"
echo "Git HEAD: $(git rev-parse HEAD)"
git status --short

cmake -S . -B "$build_dir" -DAIVERSE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release

tests=(
    ecs_static_scene_multidomain_test
    ecs_static_scene_integration_test
    ecs_prefab_static_catalog_test
    ecs_shared_static_components_test
    ecs_scene_runtime_test
    ecs_multiworld_test
    ecs_scene_load_coordinator_test
    ecs_scene_resource_pipeline_test
    ecs_scene_hierarchy_test
    ecs_scene_hierarchy_simulation_test
    ecs_cow_snapshot_test
    ecs_cow_parallel_test
)

if [[ "$mode" == "extended" ]]; then
    tests+=(
        ecs_cow_exception_test
        ecs_cow_rollback_session_test
        ecs_wave_replay_test
        ecs_simulation_bridge_test
        ecs_guarded_parallel_test
        ecs_guarded_failure_test
        ecs_native_persistence_test
        ecs_native_rts_integration_test
        ecs_real_genre_replay_test
    )
fi

targets=()
regex='^('
for name in "${tests[@]}"; do
    targets+=("aiverse_${name}")
    regex+="${name}|"
done
regex="${regex%|})$"

mkdir -p "$build_dir/verification"
log="$build_dir/verification/ecs-static-scene-${mode}.log"
echo "Mode: $mode, tests: ${#tests[@]}, jobs: $jobs"
echo "Build log and CTest results: $log"
{
    echo "HEAD: $(git rev-parse HEAD)"
    echo "Mode: $mode"
    cmake --build "$build_dir" --parallel "$jobs" --target "${targets[@]}"
    ctest --test-dir "$build_dir" -R "$regex" --output-on-failure
} 2>&1 | tee "$log"

echo "Validation succeeded; log: $log"
