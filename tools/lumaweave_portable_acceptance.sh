#!/usr/bin/env bash
# Final one-shot Linux validation for portable Vulkan/GLES3 lighting sources.
# Intentionally inert unless explicitly passed --run.
# These smoke scenes exercise existing renderer/RHI, NOT an automatically
# enabled LumaWeave native GI scene. GPU parity requires a dedicated fixture.
set -euo pipefail
if [[ "${1:-}" != "--run" || "$#" != 1 ]]; then
  echo "Usage: bash tools/lumaweave_portable_acceptance.sh --run"
  echo "No configure/build/CTests or renderer captures were executed."
  exit 2
fi
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
branch="$(git branch --show-current)"
if [[ "$branch" != "feature/gi-lumaweave-frame-integration-20261119" ]]; then
  echo "ERROR: checkout GI+LumaWeave integration branch first" >&2
  exit 3
fi
if [[ -n "$(git status --porcelain)" && "${ALLOW_DIRTY:-0}" != 1 ]]; then
  echo "ERROR: uncommitted sources; commit/stash before acceptance" >&2
  exit 3
fi
if [[ "$(uname -s)" != Linux ]]; then
  echo "ERROR: Vulkan/GLES3 acceptance requires Linux; use the Metal script on macOS" >&2
  exit 4
fi
for cmd in cmake ctest xvfb-run python3 timeout; do
  command -v "$cmd" >/dev/null || {
    echo "ERROR: missing $cmd" >&2
    exit 4
  }
done
sha="$(git rev-parse HEAD)"
report="${LIGHT_PORTABLE_REPORT:-build/gi-portable-acceptance-report}"
base="${LIGHT_PORTABLE_BUILD:-build/gi-portable-acceptance}"
jobs="${JOBS:-4}"
mkdir -p "$report"
printf 'sha=%s\nbranch=%s\nhost=%s\n' "$sha" "$branch" "$(uname -a)" |
  tee "$report/manifest.txt"
echo 'Vulkan and GLES3 use raster direct plus opt-in CPU-probe GI; no compute support is assumed.' |
  tee -a "$report/manifest.txt"
for config in Release Debug; do
  build="$base/$config"
  cmake -S . -B "$build" -DCMAKE_BUILD_TYPE="$config" \
    -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON \
    -DAIVERSE_BUILD_CLIENT=ON -DAIVERSE_BUILD_EDITOR=OFF \
    -DAIVERSE_VULKAN=ON 2>&1 | tee "$report/$config-configure.log"
  cmake --build "$build" -j "$jobs" 2>&1 | tee "$report/$config-build.log"
  ctest --test-dir "$build" --no-tests=error --output-on-failure \
    -j "$jobs" 2>&1 | tee "$report/$config-ctest.log"
  printf '%s FULL_CTEST_PASS at %s\n' "$config" "$sha" |
    tee -a "$report/manifest.txt"
done
build="$base/Release"
if [[ ! -x "$build/aiverse_vulkan_tests" || ! -x "$build/AiverseGame" ]]; then
  echo "ERROR: Vulkan test or client missing; do not report success" >&2
  exit 5
fi
xvfb-run -a "$build/aiverse_vulkan_tests" 2>&1 |
  tee "$report/vulkan-device.log"
if grep -q 'Vulkan backend: skipped' "$report/vulkan-device.log"; then
  echo "ERROR: real Vulkan driver was unavailable (software lavapipe is allowed)" >&2
  exit 5
fi
for gfx in gl vulkan; do
  shot="$report/$gfx.ppm"
  env AIVERSE_GFX="$gfx" AIVERSE_CONTENT=templates/skirmish \
    AIVERSE_SPLASH=off AIVERSE_AUDIO=off AIVERSE_SHADOWS=high \
    AIVERSE_SCREENSHOT="$shot" AIVERSE_EXIT_AFTER_FRAMES=90 \
    xvfb-run -a timeout 300 "$build/AiverseGame" --map 0 --spectate \
    2>&1 | tee "$report/$gfx-render.log"
  test -s "$shot" || { echo "ERROR: $gfx screenshot absent" >&2; exit 6; }
done
grep -Fq '[gfx] Vulkan' "$report/vulkan-render.log" ||
  { echo "ERROR: Vulkan request silently fell back to OpenGL" >&2; exit 6; }
python3 .github/scripts/check-screenshot.py "$report/gl.ppm" "$report/vulkan.ppm" \
  2>&1 | tee "$report/screenshot-check.log"
{
  echo "Portable RHI smoke/test suite completed at $sha"
  echo "Actual LumaWeave+GI frame visuals, Vulkan GPU RT/compute, GLSL ES3 mobile driver,"
  echo "WebGL2 browser parity, HDR GI benchmark, and physical GPU ms: NOT VERIFIED"
  echo "Raster direct and CPU probe GI must still be captured in a native"
  echo "scene intentionally invoking LumaWeaveNativeDriver::frame."
} | tee -a "$report/manifest.txt"
echo "Logs: $report"
