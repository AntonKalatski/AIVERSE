#!/usr/bin/env bash
# PRISMA Stage 96: one complete native Release build and CTest, followed by
# backend-specific HDR/TAA fixtures. Never changes Git branches or worktrees.
# Usage: bash tools/prisma/acceptance.sh [build-dir]
# On macOS GPU fixtures run by default; AIVERSE_PRISMA_RUN_GPU=0 disables them.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARG="${1:-build/prisma-main-acceptance}"
if [[ "$ARG" == /* ]]; then BUILD="$ARG"; else BUILD="$ROOT/$ARG"; fi
LOG="$BUILD/prisma-acceptance"
mkdir -p "$LOG"
JOBS="${AIVERSE_PRISMA_JOBS:-4}"
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || (( JOBS > 64 )); then
    echo "AIVERSE_PRISMA_JOBS must be an integer in 1..64" >&2
    exit 2
fi
OS="$(uname -s)"
REV="$(git -C "$ROOT" rev-parse HEAD)"
BRANCH="$(git -C "$ROOT" symbolic-ref --short -q HEAD || echo detached)"
if [[ -n "${AIVERSE_PRISMA_EXPECT_SHA:-}" && "$REV" != "$AIVERSE_PRISMA_EXPECT_SHA" ]]; then
    echo "SHA mismatch: expected $AIVERSE_PRISMA_EXPECT_SHA but checkout is $REV" >&2
    exit 2
fi
SUMMARY="$LOG/summary.md"
ERRORS=0
SKIPPED=0
printf '# PRISMA native acceptance\n\nRevision: `%s`\nBranch: `%s`\nOS: %s\nBuild: `%s`\n\n| Step | Result | Evidence |\n|---|---|---|\n' \
    "$REV" "$BRANCH" "$OS" "$BUILD" > "$SUMMARY"
git -C "$ROOT" status --short > "$LOG/git-status.txt"
git -C "$ROOT" log -1 --format='%H%n%s%n%ci' > "$LOG/commit.txt"

record() {
    local step="$1" result="$2" evidence="$3"
    printf '| %s | %s | %s |\n' "$step" "$result" "$evidence" >> "$SUMMARY"
    echo "[prisma] $result $step ($evidence)"
    if [[ "$result" == FAIL ]]; then ERRORS=$((ERRORS+1)); fi
    if [[ "$result" == SKIP ]]; then SKIPPED=$((SKIPPED+1)); fi
}
stage() {
    local name="$1"
    shift
    echo "========== $name =========="
    set +e
    "$@" 2>&1 | tee "$LOG/$name.log"
    local rc=${PIPESTATUS[0]}
    set -e
    if (( rc == 0 )); then record "$name" PASS "$name.log"
    else record "$name" FAIL "$name.log (exit $rc)"; fi
    return "$rc"
}
mark_missing() {
    local step="$1" reason="$2"
    record "$step" SKIP "$reason"
    if [[ "${AIVERSE_PRISMA_REQUIRE_GPU:-0}" == 1 ]]; then
        record "${step}_required" FAIL "GPU fixture unavailable"
    fi
}

if ! stage configure cmake -S "$ROOT" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_TESTS=ON \
    -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON; then
    record build SKIP "configure failed"
    record ctest_full SKIP "configure failed"
    exit 1
fi
if ! stage build cmake --build "$BUILD" --parallel "$JOBS"; then
    record ctest_full SKIP "build failed: refuse stale test binaries"
    exit 1
fi

# Registration is metadata only. Execute the complete CTest suite ONCE.
if stage ctest_registered ctest --test-dir "$BUILD" -N; then
    count="$(sed -n 's/^Total Tests: //p' "$LOG/ctest_registered.log" | tail -1)"
    if ! [[ "$count" =~ ^[0-9]+$ ]] || (( count < 100 )); then
        record registration_guard FAIL "expected >=100 registered tests; found ${count:-unknown}"
    else
        record registration_guard PASS "$count registered (not executed)"
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
if [[ "$OS" != Darwin || "$RUN_GPU" == 0 ]]; then
    mark_missing metal_hdr "not executed (requires macOS and AIVERSE_PRISMA_RUN_GPU=1)"
    mark_missing metal_taa "not executed (requires macOS and AIVERSE_PRISMA_RUN_GPU=1)"
    mark_missing gl33_hdr "not executed (requires macOS and AIVERSE_PRISMA_RUN_GPU=1)"
else
    # The HDR scene binary uses the real Metal backend on macOS; hdr_gl is
    # a SEPARATE OpenGL backend, not evidence of Metal rendering.
    metal="$BUILD/aiverse_hdr_scene"
    gl="$BUILD/aiverse_hdr_gl"
    if [[ -x "$metal" ]]; then
        capture="$LOG/metal-hdr.ppm"
        if stage metal_hdr env AIVERSE_HDR_VERIFY=1 AIVERSE_HDR_CAPTURE="$capture" "$metal"; then
            probe_count="$(grep -Ec '^HDR GPU probe [0-9]+:.* PASS$' "$LOG/metal_hdr.log" || true)"
            if grep -q '^GPU: Metal ' "$LOG/metal_hdr.log" \
                && grep -Eq '^Scene shaders:.*PASS' "$LOG/metal_hdr.log" \
                && (( probe_count >= 54 )) && [[ -s "$capture" ]] \
                && [[ "$(head -c 2 "$capture")" == P6 ]]; then
                record metal_hdr_evidence PASS "Metal, ${probe_count}/54+ probes, P6 capture"
            else
                record metal_hdr_evidence FAIL "backend, shaders, probes or capture incomplete"
            fi
        fi
        if stage metal_taa env AIVERSE_TAA_VERIFY=1 "$metal"; then
            if grep -q '^GPU: Metal ' "$LOG/metal_taa.log" \
                && grep -Eq '^TAA stationary centroid drift:.*PASS' "$LOG/metal_taa.log"; then
                record metal_taa_evidence PASS "Metal stationary temporal probe"
            else
                record metal_taa_evidence FAIL "missing backend marker or TAA PASS"
            fi
        fi
    else
        mark_missing metal_hdr "aiverse_hdr_scene executable missing"
        mark_missing metal_taa "aiverse_hdr_scene executable missing"
    fi
    if [[ -x "$gl" ]]; then
        capture="$LOG/gl33-hdr.ppm"
        if stage gl33_hdr env AIVERSE_HDR_VERIFY=1 AIVERSE_HDR_CAPTURE="$capture" "$gl"; then
            probe_count="$(grep -Ec '^HDR GPU probe [0-9]+:.* PASS$' "$LOG/gl33_hdr.log" || true)"
            if grep -q '^GPU: GL33 ' "$LOG/gl33_hdr.log" \
                && grep -Eq '^Scene shaders:.*PASS' "$LOG/gl33_hdr.log" \
                && (( probe_count >= 54 )) && [[ -s "$capture" ]] \
                && [[ "$(head -c 2 "$capture")" == P6 ]]; then
                record gl33_hdr_evidence PASS "GL33, ${probe_count}/54+ probes, P6 capture"
            else
                record gl33_hdr_evidence FAIL "backend, shaders, probes or capture incomplete"
            fi
        fi
    else
        mark_missing gl33_hdr "aiverse_hdr_gl executable missing"
    fi
fi
printf '\n**Failures:** %d  \n**Skipped:** %d\n\n' "$ERRORS" "$SKIPPED" >> "$SUMMARY"
printf 'Vulkan/GLES/WebGL GPU runtime parity: **NOT VERIFIED by this script**.\n' >> "$SUMMARY"
cat "$SUMMARY"
echo "[prisma] logs and PPM captures: $LOG"
if (( ERRORS != 0 )); then exit 1; fi
