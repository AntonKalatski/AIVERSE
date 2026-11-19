#!/usr/bin/env bash
# VIVID one-pass native acceptance: fetch + configure + build + CTest +
# actual macOS Renderer3D/MotionWeave screenshot verification.
# Run: bash tools/vivid-validate.sh
# Use --no-gui for an unattended environment, --full for all engine tests.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BRANCH="feature/vivid-digital-humans"
BUILD="${AIVERSE_VIVID_BUILD_DIR:-$ROOT/build/vivid-onepass}"
JOBS="${AIVERSE_VIVID_JOBS:-4}"
GUI=1
FULL=0
for option in "$@"; do
    case "$option" in
        --no-gui) GUI=0 ;;
        --full) FULL=1 ;;
        *) printf 'Unknown option: %s\n' "$option" >&2; exit 2 ;;
    esac
done
if [[ "$(uname -s)" != Darwin ]]; then
    printf 'One-pass acceptance requires macOS Metal. Use Linux CI for portable subsets.\n' >&2
    exit 2
fi
# Keep unrelated worktrees completely untouched. Do not switch a dirty tree.
if [[ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]]; then
    printf 'Refusing checkout over modified tracked files: %s\n' "$ROOT" >&2
    git -C "$ROOT" status --short >&2
    exit 2
fi
git -C "$ROOT" fetch origin "$BRANCH"
git -C "$ROOT" switch --detach "origin/$BRANCH"
HEAD="$(git -C "$ROOT" rev-parse HEAD)"
mkdir -p "$BUILD"
LOGDIR="$BUILD/vivid-validation"
mkdir -p "$LOGDIR"
printf 'VIVID validation SHA: %s\n' "$HEAD" | tee "$LOGDIR/head.txt"
printf 'Root: %s\nBuild: %s\nLogs: %s\n' "$ROOT" "$BUILD" "$LOGDIR"
trap 'code=$?; printf "\nVIVID one-pass FAILED (exit=%s). Logs: %s\n" "$code" "$LOGDIR" >&2' ERR

run_logged() {
    local label="$1"
    shift
    printf '\n===== %s =====\n' "$label"
    "$@" 2>&1 | tee "$LOGDIR/$label.log"
}
run_logged cmake-configure cmake -S "$ROOT" -B "$BUILD" \
    -DAIVERSE_BUILD_TESTS=ON

if [[ "$FULL" -eq 1 ]]; then
    run_logged cmake-build cmake --build "$BUILD" -j "$JOBS"
else
    run_logged cmake-build cmake --build "$BUILD" --target \
        aiverse_vivid_face_tests \
        aiverse_vivid_sparse_tests \
        aiverse_vivid_shape_tests \
        aiverse_vivid_morph_tests \
        aiverse_vivid_gpu_contract_tests \
        aiverse_vivid_metal_gpu_tests \
        aiverse_vivid_metal_rhi_tests \
        aiverse_vivid_vertex_override_tests \
        aiverse_vivid_actor_retirement_tests \
        aiverse_vivid_motionweave_tests \
        aiverse_motionweave_api_tests \
        aiverse_vivid_renderer_preview \
        -j "$JOBS"
fi

if [[ "$FULL" -eq 1 ]]; then
    run_logged ctest ctest --test-dir "$BUILD" --output-on-failure -j "$JOBS"
else
    # Deterministic sequential GPU tests; build *every* selected target first.
    run_logged ctest ctest --test-dir "$BUILD" --output-on-failure \
        -R '^(vivid_|motionweave_api$)' -j 1
fi

if [[ "$GUI" -eq 1 ]]; then
    PREVIEW="$BUILD/aiverse_vivid_renderer_preview"
    if [[ ! -x "$PREVIEW" ]]; then
        printf 'Missing GUI preview executable: %s\n' "$PREVIEW" >&2
        exit 1
    fi
    PREFIX="$LOGDIR/vivid"
    run_logged renderer-preview env AIVERSE_VIVID_PREVIEW_PREFIX="$PREFIX" "$PREVIEW"
    for stage in baseline morph motionweave; do
        output="$PREFIX-$stage.ppm"
        if [[ ! -s "$output" ]]; then
            printf 'Missing/empty preview screenshot: %s\n' "$output" >&2
            exit 1
        fi
    done
    if ! grep -q 'pass=1' "$LOGDIR/renderer-preview.log"; then
        printf 'Renderer3D/MotionWeave preview did not report pass=1.\n' >&2
        exit 1
    fi
else
    printf 'GUI preview intentionally skipped by --no-gui; this is NOT full visual acceptance.\n'
fi

{
    printf 'SHA=%s\n' "$HEAD"
    printf 'GPU+MotionWeave CTest=PASS\n'
    if [[ "$GUI" -eq 1 ]]; then
        printf 'Renderer3D colour/shadow/MotionWeave visual preview=PASS\n'
    else
        printf 'Renderer3D visual preview=NOT RUN (--no-gui)\n'
    fi
    printf 'Logs=%s\n' "$LOGDIR"
} | tee "$LOGDIR/summary.txt"
printf '\nVIVID one-pass verification COMPLETED.\n'
