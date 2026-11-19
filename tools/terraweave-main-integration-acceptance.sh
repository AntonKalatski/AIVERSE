#!/usr/bin/env bash
# TerraWeave + current main: one native build, one unfiltered CTest.
# Run from an ISOLATED checkout of integration/terraweave-main-20261120.
# This does not merge branches, touch another worktree, or change game data.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

# Stable folder is kept separate from earlier TerraWeave/PHYSIS QA builds.
build="${AIVERSE_INTEGRATION_BUILD_DIR:-build/terraweave-main-acceptance}"
jobs="${AIVERSE_INTEGRATION_JOBS:-4}"
ctest_jobs="${AIVERSE_INTEGRATION_CTEST_JOBS:-2}"
mkdir -p "$build"
build="$(cd "$build" && pwd)"
reports="$build/acceptance-reports"
mkdir -p "$reports"

printf 'repo=%s\n' "$root" | tee "$reports/provenance.txt"
printf 'git_sha=%s\n' "$(git rev-parse HEAD)" | tee -a "$reports/provenance.txt"
printf 'git_branch=%s\n' "$(git branch --show-current)" | tee -a "$reports/provenance.txt"
git status --short | tee "$reports/git-status.txt"

# Prevent a source checkout that predates one side of the 2026-11-20 merge
# from being mistaken for its integrated descendant.
git merge-base --is-ancestor d9de76dcd8aa4e7b4e01a42e183a0b9c98c4854b HEAD || {
  echo '[integration] FAIL: TerraWeave baseline is not an ancestor' >&2
  exit 1
}
git merge-base --is-ancestor d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1 HEAD || {
  echo '[integration] FAIL: main baseline is not an ancestor' >&2
  exit 1
}

echo '========== configure merged native/editor/physics =========='
cmake -S . -B "$build" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_BUILD_CLIENT=OFF \
  -DAIVERSE_BUILD_HEADLESS=ON \
  -DAIVERSE_BUILD_TOOLS=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_BUILD_PHYSICS_DEMO=OFF \
  -DAIVERSE_PLACE_FAST_DEBUG=ON \
  -DAIVERSE_FBX=OFF \
  2>&1 | tee "$reports/configure.log"

grep -E '^(CMAKE_BUILD_TYPE|AIVERSE_BUILD_TESTS|AIVERSE_ENABLE_PHYSICS|AIVERSE_PLACE_FAST_DEBUG|AIVERSE_BUILD_EDITOR):' \
  "$build/CMakeCache.txt" | tee "$reports/cmake-cache-flags.txt"

echo '========== build ALL enabled native targets =========='
cmake --build "$build" --parallel "$jobs" \
  2>&1 | tee "$reports/build.log"

echo '========== registered native tests =========='
ctest --test-dir "$build" -N \
  2>&1 | tee "$reports/inventory.log"

echo '========== run ONE complete CTest =========='
ctest --test-dir "$build" --output-on-failure --parallel "$ctest_jobs" \
  --output-junit "$reports/ctest.xml" \
  2>&1 | tee "$reports/ctest.log"

echo "PASS: merged native/editor/Box3D CTest; provenance and logs: $reports"
echo 'GPU Metal/Vulkan/GLES screenshots and actual live erosion/fall world remain separate acceptance gates.'
