#!/usr/bin/env bash
# NEREID regression gate. Run from any directory on macOS/Linux.
# Usage:
#   bash tools/nereid/validate.sh --focused  # NEREID + VFX + skill checks
#   bash tools/nereid/validate.sh --full     # focused checks, then all CTest
# Optional: AIVERSE_NEREID_BUILD_DIR=build/nereid-test AIVERSE_NEREID_JOBS=4
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
MODE="${1:---focused}"
if [[ "$#" -gt 1 || ( "$MODE" != "--focused" && "$MODE" != "--full" ) ]]; then
    echo "Usage: bash tools/nereid/validate.sh [--focused|--full]" >&2
    exit 2
fi
BUILD_DIR="${AIVERSE_NEREID_BUILD_DIR:-build/nereid-validation}"
if [[ "$BUILD_DIR" != /* ]]; then BUILD_DIR="$ROOT/$BUILD_DIR"; fi
JOBS="${AIVERSE_NEREID_JOBS:-4}"
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "AIVERSE_NEREID_JOBS must be a positive integer" >&2
    exit 2
fi
LOG_DIR="$BUILD_DIR/nereid-logs"
mkdir -p "$LOG_DIR"

GIT_HEAD="$(git rev-parse HEAD)"
GIT_BRANCH="$(git branch --show-current)"
printf 'NEREID validation\nbranch=%s\nhead=%s\nmode=%s\nbuild=%s\n' \
    "$GIT_BRANCH" "$GIT_HEAD" "$MODE" "$BUILD_DIR" | tee "$LOG_DIR/context.txt"

# Preserve both stdout/stderr per stage, including failing output. The runner
# reports the real command's status, rather than masking it through tee.
run_stage() {
    local name="$1"
    shift
    echo "=== $name ==="
    set +e
    "$@" 2>&1 | tee "$LOG_DIR/$name.log"
    local result=${PIPESTATUS[0]}
    set -e
    printf '%s exit=%d\n' "$name" "$result" | tee -a "$LOG_DIR/summary.txt"
    if [[ "$result" -ne 0 ]]; then
        echo "FAIL: $name. Log: $LOG_DIR/$name.log" >&2
        return "$result"
    fi
}
: > "$LOG_DIR/summary.txt"

# Metadata first: broken skill identity or stale generated catalogs must fail
# before expensive C++ compilation. Tests enforce native package routing.
run_stage skill-catalog python3 tools/skills/catalog.py check
run_stage skill-routing python3 tests/skills/catalog_test.py
run_stage skill-audit python3 tools/skills/audit.py --skills \
    aiverse-nereid-water,aiverse-nereid-buoyancy,aiverse-nereid-interactions,aiverse-nereid-cascades,aiverse-nereid-currents,aiverse-nereid-vortices

run_stage configure cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_TESTS=ON

# A --full run builds everything first, so ctest cannot silently execute
# stale test binaries from a previous source revision.
if [[ "$MODE" == "--full" ]]; then
    run_stage build-all cmake --build "$BUILD_DIR" --parallel "$JOBS"
else
    run_stage build-focused cmake --build "$BUILD_DIR" --parallel "$JOBS" \
        --target \
        aiverse_ecs_nereid_water_test \
        aiverse_ecs_nereid_buoyancy_test \
        aiverse_ecs_nereid_geometry_test \
        aiverse_ecs_nereid_query_index_test \
        aiverse_ecs_nereid_legacy_raster_test \
        aiverse_ecs_nereid_native_pipeline_test \
        aiverse_ecs_nereid_surface_mesh_test \
        aiverse_ecs_nereid_renderer_contract_test \
        aiverse_ecs_nereid_confluence_contract_test \
        aiverse_vfx_graph_tests \
        aiverse_vfx_ecs_tests \
        aiverse_vfx_assets_tests \
        aiverse_ecs_native_rts_integration_test
fi

FOCUSED='^(ecs_nereid_.*_test|ecs_native_rts_integration_test|vfx_(graph_compiler|ecs_presentation|graph_assets)|skill_(catalog|routing|audit))$'
run_stage ctest-focused ctest --test-dir "$BUILD_DIR" \
    --output-on-failure --parallel "$JOBS" -R "$FOCUSED"
if [[ "$MODE" == "--full" ]]; then
    run_stage ctest-full ctest --test-dir "$BUILD_DIR" \
        --output-on-failure --parallel "$JOBS"
fi
echo "PASS: all requested NEREID validation stages. Logs: $LOG_DIR"
