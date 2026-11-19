#!/usr/bin/env bash
# AIVERSE Static/Shared + SceneRuntime one-shot native verification.
# Usage:
#   bash tools/ecs-static-scene-validate.sh [build-dir] [smoke|extended|all] [jobs] [rss-entities]
# 'all' configures once, builds ALL native targets, runs the COMPLETE
# registered CTest suite once, then measures RSS in separate processes.
# No checkout, fetch, branch changes, merge or background promises.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build_dir="${1:-build/static-scene}"
mode="${2:-all}"
jobs="${3:-8}"
rss_entities="${4:-1000000}"
case "$mode" in
    smoke|extended|all) ;;
    *) echo "Expected mode smoke, extended or all, got: $mode" >&2; exit 2 ;;
esac
if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]] ||
   ! [[ "$rss_entities" =~ ^[1-9][0-9]*$ ]] ||
   (( rss_entities>2000000 )); then
    echo "Invalid jobs (positive) or RSS entities (1..2000000)" >&2
    exit 2
fi

mkdir -p "$build_dir/verification"
log="$build_dir/verification/ecs-static-scene-${mode}.log"
rsslog="$build_dir/verification/ecs-static-scene-rss.jsonl"

echo "AIVERSE scene and immutable configs validation"
echo "Branch: $(git branch --show-current)"
echo "HEAD: $(git rev-parse HEAD)"
echo "Mode: $mode, jobs: $jobs"
echo "Log: $log"
git status --short

tests=(
    ecs_static_scene_multidomain_test
    ecs_static_scene_editor_contract_test
    ecs_static_scene_integration_test
    ecs_prefab_static_catalog_test
    ecs_shared_static_components_test
    ecs_scene_runtime_test
    ecs_multiworld_test
    ecs_scene_load_coordinator_test
    ecs_scene_resource_pipeline_test
    ecs_scene_collision_namespaces_test
    ecs_scene_collision_bridge_test
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

# tee and pipefail preserve actual cmake/CTest failures and full log.
{
    echo "Git SHA: $(git rev-parse HEAD)"
    echo "Branch: $(git branch --show-current)"
    echo "UTC: $(date -u +'%Y-%m-%dT%H:%M:%SZ')"
    cmake -S . -B "$build_dir" \
        -DAIVERSE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release || exit $?
    if [[ "$mode" == "all" ]]; then
        echo "Building ALL native targets (full AIVERSE validation)"
        cmake --build "$build_dir" --parallel "$jobs" || exit $?
        echo "Running COMPLETE registered CTest suite (serial for test isolation)"
        ctest --test-dir "$build_dir" --output-on-failure || exit $?
    else
        echo "Building selected native targets: ${#targets[@]}"
        cmake --build "$build_dir" --parallel "$jobs" --target "${targets[@]}" || exit $?
        ctest --test-dir "$build_dir" -R "$regex" --output-on-failure || exit $?
    fi
} 2>&1 | tee "$log"

if [[ "$mode" == "all" ]]; then
    # Important: three modes are separate OS processes so their RSS
    # measurements do not inherit allocations or fragmentation.
    bench="$build_dir/aiverse_ecs_shared_static_rss_bench"
    if [[ ! -x "$bench" ]]; then
        echo "RSS bench executable missing: $bench" >&2
        exit 1
    fi
    : > "$rsslog"
    for unique in 16 64 1024; do
        for storage in ordinary shared static; do
            echo "RSS sample: ${storage}, ${rss_entities} entities, ${unique} unique"
            "$bench" "$storage" "$rss_entities" "$unique" | tee -a "$rsslog"
        done
    done
    echo "RSS results: $rsslog"
    # Only compare equivalent checksums and real measured resident-byte
    # deltas; never fail on performance ratios or claim raw payload as RSS.
    python3 - "$rsslog" <<'PY'
import json
import sys
from collections import defaultdict

source = sys.argv[1]
records = [json.loads(line) for line in open(source, encoding="utf-8")
           if line.strip()]
grouped = defaultdict(dict)
for r in records:
    grouped[(r["entities"], r["unique"])][r["mode"]] = r
for (count, unique), group in sorted(grouped.items()):
    if set(group) != {"ordinary", "shared", "static"}:
        raise SystemExit("missing RSS mode: %s" % ((count, unique),))
    checksums = {v["checksum"] for v in group.values()}
    if len(checksums) != 1:
        raise SystemExit("read checksum mismatch: %s" % ((count, unique),))
    print("RSS verified: entities=%d unique=%d checksum=%d" %
          (count, unique, checksums.pop()))
    for mode in ("ordinary", "shared", "static"):
        r=group[mode]
        rss=r["rss_build_delta_bytes"]
        print("  %-8s RSS delta %.3f MiB, median read %.4f ms" %
              (mode, rss/(1024*1024),r["median_read_ms"]))
    if all(v["rss_after_build_bytes"] == 0 for v in group.values()):
        print("  WARNING: resident set size unsupported on this host")
print("RSS samples validated: %d" % len(records))
PY
fi

echo "SUCCESS: complete $mode run, test log: $log"
