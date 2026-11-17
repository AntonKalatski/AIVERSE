#!/usr/bin/env bash
# Real-module ECS regression comparison: baseline vs opt-in RTS transform pass.
# bash tests/run_ecs_gameplay_compare.sh [--full]
# Builds in AIVERSE_ECS_BUILD_ROOT (default: /tmp/aiverse-ecs-gameplay).
set -euo pipefail
cd "$(dirname "$0")/.."
FULL=0
for arg in "$@"; do
  case "$arg" in --full) FULL=1;; *) echo "Unknown option $arg" >&2; exit 2;; esac
done
ROOT="${AIVERSE_ECS_BUILD_ROOT:-${TMPDIR:-/tmp}/aiverse-ecs-gameplay}"
mkdir -p "$ROOT"
COMMON=(
  -DCMAKE_BUILD_TYPE=Release
  -DAIVERSE_BUILD_CLIENT=OFF
  -DAIVERSE_BUILD_EDITOR=OFF
  -DAIVERSE_BUILD_TOOLS=OFF
  -DAIVERSE_BUILD_SERVER=OFF
  -DAIVERSE_BUILD_HEADLESS=ON
  -DAIVERSE_BUILD_FPS_CLIENT=OFF
  -DAIVERSE_BUILD_TESTS=ON
)
for mode in off on; do
  build="$ROOT/build-$mode"
  flag=OFF
  [[ "$mode" == on ]] && flag=ON
  cmake -S . -B "$build" "${COMMON[@]}" "-DAIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS=$flag"
  targets=(aiverse_headless aiverse_fps_tests)
  if [[ "$FULL" == 1 ]]; then targets+=(aiverse_tests aiverse_net_tests); fi
  cmake --build "$build" --parallel "${AIVERSE_ECS_BUILD_JOBS:-4}" --target "${targets[@]}"
  echo "=== RTS headless: $mode ==="
  "$build/aiverse_headless" --project templates/skirmish --seed 3 --max-seconds 10 \
    --verify-restore-at 2 --verify-restore-ticks 40 \
    --tick-times "$ROOT/rtssim-$mode.tick-us" \
    --checksum-log "$ROOT/rtssim-$mode.checksums" > "$ROOT/rtssim-$mode.log" 2>&1 || {
    cat "$ROOT/rtssim-$mode.log"; exit 1;
  }
  grep 'Restore OK' "$ROOT/rtssim-$mode.log" || {
    echo "Restore verification missing in $mode" >&2
    cat "$ROOT/rtssim-$mode.log"; exit 1;
  }
  echo "=== FPS tests: $mode ==="
  ctest --test-dir "$build" -R '^(fps|fps_transport)$' --output-on-failure
  if [[ "$FULL" == 1 ]]; then
    echo "=== RTS/game test suite: $mode ==="
    ctest --test-dir "$build" -R '^aiverse_(tests|net_tests)
  fi
done
if ! grep -Eq '^[0-9]+ [0-9a-fA-F]{16}$' "$ROOT/rtssim-off.checksums"; then
  echo "No RTS checksum checkpoints collected" >&2
  exit 1
fi
if ! diff -u "$ROOT/rtssim-off.checksums" "$ROOT/rtssim-on.checksums"; then
  echo "FAIL: baseline and opt-in RTS checksums differ" >&2
  exit 1
fi
echo "PASS RTS baseline vs opt-in per-tick checkpoints; FPS tests both variants"
echo "Logs: $ROOT"
 --output-on-failure
  fi
done
if ! grep -Eq '^[0-9]+ [0-9a-fA-F]{16}$' "$ROOT/rtssim-off.checksums"; then
  echo "No RTS checksum checkpoints collected" >&2
  exit 1
fi
if ! diff -u "$ROOT/rtssim-off.checksums" "$ROOT/rtssim-on.checksums"; then
  echo "FAIL: baseline and opt-in RTS checksums differ" >&2
  exit 1
fi
echo "PASS RTS baseline vs opt-in per-tick checkpoints; FPS tests both variants"
echo "Logs: $ROOT"
