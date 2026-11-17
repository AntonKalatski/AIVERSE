#!/usr/bin/env bash
# Run from any directory: bash tests/run_ecs_experiment_suite.sh [--bench] [--sanitize]
# Keep this file LF-only; .gitattributes enforces eol=lf for *.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-c++}"
SAN=""
DO_BENCH=0
for arg in "$@"; do
    case "$arg" in
        --bench) DO_BENCH=1 ;;
        --sanitize) SAN="-fsanitize=address,undefined -fno-omit-frame-pointer" ;;
        *) echo "Unknown argument: $arg" >&2; exit 2 ;;
    esac
done
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/aiverse-ecs-tests.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
COMMON=(-std=c++20 -O2 -pthread -I. -Wall -Wextra)
if [[ -n "$SAN" ]]; then
    COMMON+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
compile_run() {
    local name="$1"
    shift
    echo "=== $name ==="
    "$CXX" "${COMMON[@]}" "$@" -o "$BUILD_DIR/$name"
    "$BUILD_DIR/$name"
}
compile_run ecs-experiments tests/ecs_experiments_test.cpp
compile_run ecs-waves tests/ecs_wave_executor_test.cpp
compile_run ecs-guarded tests/ecs_guarded_world_test.cpp
compile_run ecs-guarded-parallel tests/ecs_guarded_parallel_test.cpp
compile_run ecs-world-replay tests/ecs_wave_replay_test.cpp
compile_run ecs-simulation-bridge tests/ecs_simulation_bridge_test.cpp \
    engine/sim/Simulation.cpp engine/core/Fixed.cpp \
    engine/core/FixedMath.cpp engine/data/Json.cpp engine/data/JsonCompose.cpp
if [[ "$DO_BENCH" == 1 ]]; then
    compile_run ecs-query-bench tests/ecs_architecture_bench.cpp
    compile_run ecs-bitmap-bench tests/ecs_bitmap_query_bench.cpp
    compile_run ecs-hier-bench tests/ecs_hierarchical_bench.cpp
    compile_run ecs-persistent-bench tests/ecs_persistent_jobs_bench.cpp
    compile_run ecs-adaptive-bench tests/ecs_adaptive_jobs_bench.cpp
fi
echo "PASS ECS experiment suite"
