#!/usr/bin/env bash
# AIVERSE Texture RHI native macOS one-shot validation.
# Pull feature/texture-rhi-unification before invoking.
# bash tools/texture-rhi-final-verify.sh [--full-engine] [--jobs N]
# GPU tests are always separate processes and run sequentially.
set -euo pipefail
JOBS=4
FULL_ENGINE=0
while (($#)); do
  case "$1" in
    --full-engine) FULL_ENGINE=1; shift;;
    --jobs)
      (($#>=2)) && [[ "$2" =~ ^[1-9][0-9]*$ ]] || {
        echo "FAIL: --jobs needs a positive integer" >&2; exit 2;
      }
      JOBS="$2"; shift 2;;
    *) echo "Usage: bash tools/texture-rhi-final-verify.sh [--full-engine] [--jobs N]" >&2; exit 2;;
  esac
done
ROOT="$(git -C "$(dirname "$0")/.." rev-parse --show-toplevel)"
cd "$ROOT"
# This verification worktree is intentionally often in detached HEAD.
# Require the exact fetched remote commit instead of requiring a local
# branch name. Never accept an unrelated detached/feature revision.
EXPECTED_BRANCH="feature/texture-rhi-unification"
CURRENT_BRANCH="$(git symbolic-ref --quiet --short HEAD || true)"
CURRENT_SHA="$(git rev-parse --verify HEAD)"
REMOTE_SHA="$(git rev-parse --verify "refs/remotes/origin/$EXPECTED_BRANCH^{commit}" 2>/dev/null || true)"
if [[ -z "$REMOTE_SHA" || "$CURRENT_SHA" != "$REMOTE_SHA" ]]; then
  echo "FAIL: HEAD must equal the fetched origin/$EXPECTED_BRANCH revision" >&2
  echo "HEAD=$CURRENT_SHA remote=${REMOTE_SHA:-not-fetched}" >&2
  echo "Run: git fetch origin $EXPECTED_BRANCH && git pull --ff-only origin $EXPECTED_BRANCH" >&2
  exit 2
fi
if [[ -n "$CURRENT_BRANCH" && "$CURRENT_BRANCH" != "$EXPECTED_BRANCH" ]]; then
  echo "FAIL: refusing another local branch: $CURRENT_BRANCH" >&2
  exit 2
fi
printf 'Validated source: %s (%s)\n' "$CURRENT_SHA" "${CURRENT_BRANCH:-detached HEAD}"
[[ "$(uname -s)" == "Darwin" ]] || {
  echo "FAIL: native Metal Mac required (no silent GPU skip)" >&2; exit 2;
}
LOGS="$ROOT/build/texture-final-logs"
SAFE="$ROOT/build/texture-final-safe"
GPU="$ROOT/build/texture-final-gpu"
mkdir -p "$LOGS"
trap 'echo "FAIL: one-shot texture verification stopped at line $LINENO; logs in build/texture-final-logs" >&2' ERR
run_logged() {
  local name="$1"
  shift
  printf "\n======= %s =======\n" "$name"
  "$@" 2>&1 | tee "$LOGS/$name.log"
}
printf 'HEAD=%s jobs=%s full-engine=%s\n' "$(git rev-parse HEAD)" "$JOBS" "$FULL_ENGINE"

run_logged safe-configure cmake -S . -B "$SAFE" \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_GUTTER_GPU_TESTS=OFF
safe_targets=(
  aiverse_shader_graph_tests
  aiverse_texture_contract_test aiverse_render_target_set_test
  aiverse_ktx2_layers_test aiverse_ktx2_transcode_test
  aiverse_texture_asset_test aiverse_hdr_texture_test
  aiverse_exr_texture_test aiverse_dds_layers_test
  aiverse_texture_streaming_test aiverse_texture_mip_residency_test
  aiverse_texture_resident_streamer_test
  aiverse_virtual_texture_pages_test aiverse_virtual_texture_page_table_test
  aiverse_virtual_texture_streamer_test aiverse_virtual_texture_disk_queue_test
  aiverse_virtual_texture_file_streamer_test aiverse_virtual_texture_prefetch_test
  aiverse_virtual_texture_feedback_test aiverse_virtual_texture_feedback_pass_test
  aiverse_gpu_icb_material_contract_test
)
safe_tests=(
  shader_graph texture_contract render_target_set ktx2_layers ktx2_transcode
  texture_asset hdr_texture exr_texture dds_layers texture_streaming
  texture_mip_residency texture_resident_streamer
  virtual_texture_pages virtual_texture_page_table virtual_texture_streamer
  virtual_texture_disk_queue virtual_texture_file_streamer virtual_texture_prefetch
  virtual_texture_feedback virtual_texture_feedback_pass gpu_icb_material_contract
)
run_logged safe-build cmake --build "$SAFE" --target ${safe_targets[@]} --parallel "$JOBS"
safe_regex="$(IFS='|'; echo "${safe_tests[*]}")"
run_logged safe-ctest ctest --test-dir "$SAFE" \
  --output-on-failure --no-tests=error --parallel "$JOBS" -R "^($safe_regex)$"
if ctest --test-dir "$SAFE" -N -R '^(virtual_texture_pack|virtual_texture_feedback_(shaders|msl))$' |
   grep -Eq 'Test +#[0-9]'; then
  run_logged safe-optional-vt-ctest ctest --test-dir "$SAFE" \
    --output-on-failure --no-tests=error \
    -R '^(virtual_texture_pack|virtual_texture_feedback_(shaders|msl))$'
fi
if ((FULL_ENGINE)); then
  run_logged full-engine-build cmake --build "$SAFE" --parallel "$JOBS"
  run_logged full-engine-ctest ctest --test-dir "$SAFE" \
    --output-on-failure --no-tests=error --parallel "$JOBS"
fi

# The seven real Metal tests are opt-in and compiled from current source
# before ANY CTest executes. CTest tests run serially, in fresh processes.
run_logged gpu-configure cmake -S . -B "$GPU" \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_GUTTER_GPU_TESTS=ON
gpu_targets=(
  aiverse_metal_texture_gpu_test aiverse_metal_texture_icb_graph_gpu_test
  aiverse_metal_renderer_graph_icb_gpu_test
  aiverse_metal_renderer_graph_vt_icb_gpu_test
  aiverse_metal_renderer_graph_vt_seam_gpu_test
  aiverse_metal_vt_bilinear_gpu_test
  aiverse_metal_renderer_graph_vt_gutter_gpu_test
)
gpu_tests=(
  metal_texture_gpu metal_texture_icb_graph_gpu
  metal_renderer_graph_icb_gpu metal_renderer_graph_vt_icb_gpu
  metal_renderer_graph_vt_seam_gpu metal_vt_bilinear_gpu
  metal_renderer_graph_vt_gutter_gpu
)
markers=(
  ''
  'PASS: GPU ICB graph sources=4, per-command textures/U2 pixel parity'
  'PASS: production Renderer3D compiled Shader Graph 4-source mixed Metal GPU ICB pixels'
  'PASS: production Renderer3D Shader Graph+disk VT mixed Metal GPU ICB coarse/fine/LRU pixel parity'
  'PASS: Renderer3D Shader Graph disk VT missing-page and Repeat/MirrorRepeat seam GPU ICB oracle'
  'PASS: native Metal canonical VT bilinear nonuniform gutter seam and Repeat/MirrorRepeat CPU pixel oracle'
  'PASS: production Renderer3D Shader Graph nonuniform guttered disk VT mixed Metal GPU ICB seam parity'
)
run_logged gpu-build cmake --build "$GPU" --target ${gpu_targets[@]} --parallel "$JOBS"
for index in "${!gpu_tests[@]}"; do
  test_name="${gpu_tests[$index]}"
  run_logged "gpu-$test_name" ctest --test-dir "$GPU" --verbose \
    --output-on-failure --no-tests=error -R "^$test_name$"
  marker="${markers[$index]}"
  if [[ -n "$marker" ]] && ! grep -Fq "$marker" "$LOGS/gpu-$test_name.log"; then
    echo "FAIL: $test_name CTest lacked mandatory execution marker: $marker" >&2
    exit 1
  fi
done
{
  echo "sha=$(git rev-parse HEAD)"
  echo "safe_texture_tests=21"
  echo "native_metal_gpu_tests=7"
  echo "full_engine=$FULL_ENGINE"
  echo "result=PASS"
} | tee "$LOGS/acceptance.txt"
echo "PASS: all 21 portable texture/graph and 7 native Metal GPU tests"
echo "Logs: $LOGS"
