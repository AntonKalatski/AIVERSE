#!/usr/bin/env bash
# Stage 80: repeat fixed-scene Metal CPU benchmark in rotated order.
# Each executable run contains its own Direct baseline and warmup.
# This is CPU renderer.render() timing, NOT GPU execution time/FPS.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${AIVERSE_BATCH_BENCH_BIN:-build/batching-gpu-verify/aiverse_material_batches}"
ITEMS="${1:-1024}"
OUT="${2:-build/batching-gpu-verify/stage80}"
if ! [[ "$ITEMS" =~ ^[0-9]+$ ]] || (( ITEMS < 64 || ITEMS > 100000 )); then
  echo "Expected item count 64..100000" >&2
  exit 2
fi
if [[ ! -x "$BIN" ]]; then
  echo "Build fixture first: cmake --build build/batching-gpu-verify --target aiverse_material_batches --parallel 8" >&2
  exit 2
fi
mkdir -p "$OUT"
run_mode() {
  local cycle="$1" mode="$2"
  local label="stage80-${ITEMS}-cycle${cycle}-${mode}"
  echo "===== ${label} ====="
  # unset inherited experimental GPU ICB/auto switches so a prior shell
  # session cannot contaminate CPU-only comparisons.
  local -a opts=(AIVERSE_PALETTE_TEST_CASE=auto_stress
    AIVERSE_BATCH_STRESS_ITEMS="$ITEMS" AIVERSE_BATCH_CPU_TIMING=1
    AIVERSE_BATCH_STRESS_POLICY="$mode")
  if [[ "$mode" == auto ]]; then
    opts+=(AIVERSE_BATCH_AUTO=1)
  else
    opts+=(AIVERSE_METAL_CPU_MDI=1)
  fi
  if [[ "$mode" == mdi ]]; then
    opts+=(AIVERSE_CAPTURE_SUCCESSFUL_MDI=1)
  fi
  env -u AIVERSE_BATCH_AUTO -u AIVERSE_EXPERIMENTAL_METAL_ICB \
    -u AIVERSE_GPU_ENCODED_ICB -u AIVERSE_GPU_AUTHORED_DRAWS \
    -u AIVERSE_GPU_ICB_RIGID -u AIVERSE_BATCH_BENCH_MODE \
    "${opts[@]}" "$BIN" unused.obj "$OUT/$label" \
    2>&1 | tee "$OUT/$label.log"
  grep -q 'changed_channels=0' "$OUT/$label.log"
  if [[ "$mode" == mdi ]]; then
    grep -q 'actual MDI pixel parity frame=.*changed_channels=0' "$OUT/$label.log"
  fi
}
# Counterbalance which comparison runs first between cycles.
for mode in direct mdi auto; do run_mode 1 "$mode"; done
for mode in auto mdi direct; do run_mode 2 "$mode"; done
echo "Stage 80 runs completed. Compare path-labelled distributions in $OUT/*.log"
