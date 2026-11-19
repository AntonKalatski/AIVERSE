#!/usr/bin/env bash
# LumaWeave FINAL one-shot native acceptance; inert without explicit --run.
# Builds/tests Debug+Release and compiles ACTUAL shader literals via Xcode.
# GPU image/performance verification still requires an on-device visual fixture.
set -euo pipefail
if [[ "$#" -ne 1 || "$1" != "--run" ]]; then
  echo "Usage: bash tools/lumaweave_acceptance.sh --run"
  echo "No builds, tests or benchmarks were executed."
  exit 2
fi
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
if [[ "$(git branch --show-current)" != "feature/lumaweave-massive-lighting" ]]; then
  echo "ERROR: checkout feature/lumaweave-massive-lighting first" >&2
  exit 3
fi
if [[ -n "$(git status --porcelain)" && "$(printenv ALLOW_DIRTY || echo 0)" != "1" ]]; then
  echo "ERROR: uncommitted changes. Commit/stash first, or explicitly ALLOW_DIRTY=1" >&2
  exit 3
fi
if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "ERROR: consolidated Metal acceptance requires macOS" >&2
  exit 3
fi
if ! xcrun --sdk macosx --find metal >/dev/null 2>&1; then
  echo "ERROR: Xcode Metal compiler unavailable. Select full Xcode via xcode-select." >&2
  exit 4
fi

sha="$(git rev-parse HEAD)"
jobs="$(printenv JOBS || echo 8)"
base="$(printenv LUMA_ACCEPTANCE_BUILD || echo build/lumaweave-acceptance)"
report="$(printenv LUMA_ACCEPTANCE_REPORT || echo build/lumaweave-acceptance-report)"
mkdir -p "$report"
echo "LumaWeave consolidated validation $(date -u '+%Y-%m-%dT%H:%M:%SZ')" | tee "$report/manifest.txt"
printf 'branch=%s\nsha=%s\nhost=%s\njobs=%s\n' \
  "$(git branch --show-current)" "$sha" "$(uname -a)" "$jobs" | tee -a "$report/manifest.txt"
echo "Native CTest and Metal source compiler results are separate from GPU image/perf acceptance." | tee -a "$report/manifest.txt"

for config in Release Debug; do
  build="$base/$config"
  echo "== Configure $config ==" | tee "$report/$config-status.txt"
  cmake -S . -B "$build" -DCMAKE_BUILD_TYPE="$config" \
    -DAIVERSE_BUILD_TESTS=ON \
    -DAIVERSE_BUILD_TOOLS=ON \
    -DAIVERSE_BUILD_EDITOR=OFF \
    -DAIVERSE_BUILD_CLIENT=OFF 2>&1 | tee "$report/$config-configure.log"
  echo "== Build $config ==" | tee -a "$report/$config-status.txt"
  cmake --build "$build" -j "$jobs" 2>&1 | tee "$report/$config-build.log"
  echo "== Full CTest $config ==" | tee -a "$report/$config-status.txt"
  ctest --test-dir "$build" --output-on-failure -j "$jobs" \
    2>&1 | tee "$report/$config-ctest.log"
  echo "ALL_CTEST_PASS for $config at $sha" | tee -a "$report/$config-status.txt"
done

release="$base/Release"
mkdir -p "$report/msl"
echo "== Compiling actual assembled MSL variants with xcrun metal =="
"$release/aiverse_lumaweave_shader_dump" --list > "$report/msl/variants.txt"
while IFS= read -r variant; do
  [[ -z "$variant" ]] && continue
  "$release/aiverse_lumaweave_shader_dump" "$variant" > "$report/msl/$variant.metal"
  xcrun --sdk macosx metal -c "$report/msl/$variant.metal" \
    -o "$report/msl/$variant.air" \
    2>&1 | tee "$report/msl/$variant-build.log"
  printf '%s: MSL_COMPILED\n' "$variant" | tee -a "$report/msl/status.txt"
done < "$report/msl/variants.txt"

echo "== Deterministic 100 / 500 / 1000 light CPU planning benchmarks =="
"$release/aiverse_lumaweave_bench" 40 2>&1 | tee "$report/lumaweave-bench.csv"

{
  echo ""
  echo "Native compiler/CTest and MSL source compilation: COMPLETED at $sha"
  echo "GPU visuals, BRDF pixel parity, Metal API Validation, GPU time and FPS: NOT VERIFIED BY THIS SCRIPT"
  echo "On-device validation checklist:"
  echo "  - render 100/500/1000 moving-light HDR scenes with real receiver material and depth"
  echo "  - compare CPU LumaWeavePbrReference with GPU reflected lighting statistically"
  echo "  - inspect normal-map specular, black dielectric Fresnel, metallic roughness"
  echo "  - verify no legacy LightSystem or baked GI double counting"
  echo "  - inspect history rejection at camera cuts, geometry changes and GPU errors"
  echo "  - record actual Metal GPU timestamps and 720p/1080p frame time"
  echo "  - verify GLES/mobile raster fallback and explicit overflow diagnostics"
  echo "Benchmark output's gpu_ms is NOT_MEASURED and MUST NOT be reported as FPS"
} | tee -a "$report/manifest.txt"
echo "Acceptance logs in: $report"
