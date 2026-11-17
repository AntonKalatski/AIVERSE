#!/usr/bin/env bash
# Compare real-game RTS rollback/checksums with RTS transform jobs OFF vs ON.
# Optional --full also runs the full game and network unit test targets.
set -euo pipefail
cd "$(dirname "$0")/.."
FULL=0
for arg in "$@"; do
  case "$arg" in
    --full) FULL=1 ;;
    *) echo "Unknown argument: $arg" >&2; exit 2 ;;
  esac
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
  grep "Restore OK" "$ROOT/rtssim-$mode.log" || {
    echo "Restore verification missing in $mode" >&2
    cat "$ROOT/rtssim-$mode.log"; exit 1;
  }
  echo "=== FPS tests: $mode ==="
  ctest --test-dir "$build" --no-tests=error -R "^(fps|fps_transport)$" --output-on-failure
  if [[ "$FULL" == 1 ]]; then
    echo "=== RTS and network tests: $mode ==="
    ctest --test-dir "$build" --no-tests=error -R "^aiverse_(tests|net_tests)$" --output-on-failure
  fi
done
if ! grep -Eq "^[0-9]+ [0-9a-fA-F]{16}$" "$ROOT/rtssim-off.checksums"; then
  echo "No RTS checksum checkpoints collected" >&2
  exit 1
fi
if ! diff -u "$ROOT/rtssim-off.checksums" "$ROOT/rtssim-on.checksums"; then
  echo "FAIL: baseline and opt-in RTS checksums differ" >&2
  exit 1
fi
for mode in off on; do
  echo -n "RTS $mode mean step: "
  awk 'NF==2 && $1 ~ /^[0-9]+$/ && $2 ~ /^[0-9]+$/ {sum+=$2; count++}
       END {if(count) printf "%.3f ms/tick (%d ticks)\n",sum/count/1000,count;
            else {print "no measurements";exit 1}}' "$ROOT/rtssim-$mode.tick-us"
done
echo "PASS RTS checksum equality and FPS regression in baseline/opt-in builds"
echo "Logs: $ROOT"
