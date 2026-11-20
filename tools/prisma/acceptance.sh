#!/usr/bin/env bash
# PRISMA Stage 96 native acceptance. Run from any directory:
#   bash tools/prisma/acceptance.sh [build-dir]
# No git checkout/merge/reset. One full Release build and CTest run.
# On macOS separately executes real Metal HDR/TAA, GL33 HDR and image capture.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARG="${1:-build/prisma-main-acceptance}"
if [[ "$ARG" == /* ]]; then BUILD="$ARG"; else BUILD="$ROOT/$ARG"; fi
LOG="$BUILD/prisma-acceptance"
mkdir -p "$LOG"
JOBS="${AIVERSE_PRISMA_JOBS:-4}"
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || (( JOBS > 64 )); then
  echo "AIVERSE_PRISMA_JOBS must be 1..64" >&2
  exit 2
fi
OS="$(uname -s)"
REV="$(git -C "$ROOT" rev-parse HEAD)"
BRANCH="$(git -C "$ROOT" symbolic-ref -q --short HEAD || echo detached)"
if [[ -n "${AIVERSE_PRISMA_EXPECT_SHA:-}" && "$REV" != "$AIVERSE_PRISMA_EXPECT_SHA" ]]; then
  echo "Wrong SHA: expected $AIVERSE_PRISMA_EXPECT_SHA, actual $REV" >&2
  exit 2
fi
SUMMARY="$LOG/summary.md"
ERRORS=0
SKIPPED=0
printf '# PRISMA native acceptance\n\nSHA: `%s`\nBranch: `%s`\nOS: %s\nBuild: `%s`\n\n| Step | Status | Evidence |\n|---|---|---|\n' "$REV" "$BRANCH" "$OS" "$BUILD" > "$SUMMARY"
git -C "$ROOT" status --short > "$LOG/git-status.txt"
git -C "$ROOT" log -1 --format='%H%n%s%n%ci' > "$LOG/commit.txt"

record() {
  local name="$1" verdict="$2" evidence="$3"
  printf '| %s | %s | %s |\n' "$name" "$verdict" "$evidence" >> "$SUMMARY"
  echo "[prisma] $verdict $name: $evidence"
  if [[ "$verdict" == FAIL ]]; then ERRORS=$((ERRORS+1)); fi
  if [[ "$verdict" == SKIP ]]; then SKIPPED=$((SKIPPED+1)); fi
}
stage() {
  local name="$1"; shift
  echo "========== $name =========="
  set +e
  "$@" 2>&1 | tee "$LOG/$name.log"
  local rc=${PIPESTATUS[0]}
  set -e
  if (( rc == 0 )); then record "$name" PASS "$name.log"
  else record "$name" FAIL "$name.log (exit $rc)"; fi
  return "$rc"
}
skip_gpu() {
  record "$1" SKIP "$2"
  if [[ "${AIVERSE_PRISMA_REQUIRE_GPU:-0}" == 1 ]]; then
    record "$1-required" FAIL "mandatory GPU evidence missing"
  fi
}

if ! stage configure cmake -S "$ROOT" -B "$BUILD" \
     -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_TESTS=ON \
     -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_HDR_TESTS=ON; then
  record build SKIP "configure failed"
  record ctest_full SKIP "configure failed"
  exit 1
fi
if ! stage build cmake --build "$BUILD" --parallel "$JOBS"; then
  record ctest_full SKIP "build failed; stale binaries rejected"
  exit 1
fi

# -N only counts tests; the actual complete CTest is executed exactly once.
if stage registration ctest --test-dir "$BUILD" -N; then
  count="$(sed -n 's/^Total Tests: //p' "$LOG/registration.log" | tail -1)"
  if [[ "$count" =~ ^[0-9]+$ ]] && (( count >= 100 )); then
    record count_guard PASS "$count registered cases"
  else
    record count_guard FAIL "expected >=100, found ${count:-unknown}"
  fi
fi
stage ctest_full ctest --test-dir "$BUILD" --output-on-failure \
  --parallel "$JOBS" --timeout 900 || true
grep -E 'tests passed|tests failed|Total Test time|\*\*\*Skipped' \
  "$LOG/ctest_full.log" > "$LOG/ctest-summary.txt" || true

RUN_GPU="${AIVERSE_PRISMA_RUN_GPU:-auto}"
if [[ "$RUN_GPU" == auto ]]; then
  if [[ "$OS" == Darwin ]]; then RUN_GPU=1; else RUN_GPU=0; fi
fi
if [[ "$RUN_GPU" != 0 && "$RUN_GPU" != 1 ]]; then
  echo "AIVERSE_PRISMA_RUN_GPU must be 0, 1 or auto" >&2
  exit 2
fi

# Important: AIVERSE_HDR_VERIFY exercises GPU probes and deliberately DOES
# NOT write AIVERSE_HDR_CAPTURE. A separate normal-scene pass writes a PPM.
if [[ "$OS" != Darwin || "$RUN_GPU" == 0 ]]; then
  for name in metal_hdr metal_capture metal_taa gl33_hdr gl33_capture; do
    skip_gpu "$name" "not run: requires macOS and RUN_GPU=1"
  done
else
  metal="$BUILD/aiverse_hdr_scene"
  gl="$BUILD/aiverse_hdr_gl"
  if [[ -x "$metal" ]]; then
    if stage metal_hdr env AIVERSE_HDR_VERIFY=1 "$metal"; then
      probes="$(grep -Ec '^HDR GPU probe [0-9]+:.* PASS$' "$LOG/metal_hdr.log" || true)"
      if grep -q '^GPU: Metal ' "$LOG/metal_hdr.log" \
          && grep -Eq '^Scene shaders:.*PASS' "$LOG/metal_hdr.log" \
          && (( probes >= 54 )); then
        record metal_hdr_evidence PASS "Metal; $probes GPU probes"
      else
        record metal_hdr_evidence FAIL "missing Metal, shader PASS or 54 probes"
      fi
    fi
    if stage metal_capture env AIVERSE_HDR_CAPTURE="$LOG/metal-hdr.ppm" "$metal"; then
      if grep -q '^GPU: Metal ' "$LOG/metal_capture.log" \
          && [[ -s "$LOG/metal-hdr.ppm" ]] \
          && [[ "$(head -c 2 "$LOG/metal-hdr.ppm")" == P6 ]]; then
        record metal_capture_evidence PASS "real Metal P6 scene capture"
      else
        record metal_capture_evidence FAIL "missing backend or P6 capture"
      fi
    fi
    if stage metal_taa env AIVERSE_TAA_VERIFY=1 "$metal"; then
      if grep -q '^GPU: Metal ' "$LOG/metal_taa.log" \
          && grep -Eq '^TAA stationary centroid drift:.*PASS' "$LOG/metal_taa.log"; then
        record metal_taa_evidence PASS "real Metal stationary temporal probe"
      else
        record metal_taa_evidence FAIL "missing Metal backend or TAA PASS"
      fi
    fi
  else
    for name in metal_hdr metal_capture metal_taa; do
      skip_gpu "$name" "aiverse_hdr_scene missing"
    done
  fi
  if [[ -x "$gl" ]]; then
    if stage gl33_hdr env AIVERSE_HDR_VERIFY=1 "$gl"; then
      probes="$(grep -Ec '^HDR GPU probe [0-9]+:.* PASS$' "$LOG/gl33_hdr.log" || true)"
      if grep -q '^GPU: GL33 ' "$LOG/gl33_hdr.log" \
          && grep -Eq '^Scene shaders:.*PASS' "$LOG/gl33_hdr.log" \
          && (( probes >= 54 )); then
        record gl33_hdr_evidence PASS "GL33; $probes GPU probes"
      else
        record gl33_hdr_evidence FAIL "missing GL33, shader PASS or 54 probes"
      fi
    fi
    if stage gl33_capture env AIVERSE_HDR_CAPTURE="$LOG/gl33-hdr.ppm" "$gl"; then
      if grep -q '^GPU: GL33 ' "$LOG/gl33_capture.log" \
          && [[ -s "$LOG/gl33-hdr.ppm" ]] \
          && [[ "$(head -c 2 "$LOG/gl33-hdr.ppm")" == P6 ]]; then
        record gl33_capture_evidence PASS "real OpenGL P6 scene capture"
      else
        record gl33_capture_evidence FAIL "missing backend or P6 capture"
      fi
    fi
  else
    for name in gl33_hdr gl33_capture; do
      skip_gpu "$name" "aiverse_hdr_gl missing"
    done
  fi
fi

printf '\n**Failed:** %d  \n**Skipped:** %d\n\n' "$ERRORS" "$SKIPPED" >> "$SUMMARY"
printf 'Vulkan/GLES/WebGL runtime GPU parity: **NOT VERIFIED** by this runner.\n' >> "$SUMMARY"
cat "$SUMMARY"
echo "[prisma] all logs and captures: $LOG"
if (( ERRORS > 0 )); then exit 1; fi
