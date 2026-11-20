#!/usr/bin/env bash
# One-pass validation of experimental AIVERSE Studio HTML5 on macOS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$ROOT/build/studio-html5-verify"

if [[ "$(uname -s)" != Darwin ]]; then
  echo "This native build requires macOS, WKWebView and the Apple Metal SDK." >&2
  exit 2
fi
for required in npm cmake ctest xcrun; do
  if ! command -v "$required" >/dev/null 2>&1; then
    echo "Missing dependency: $required" >&2
    exit 2
  fi
done

echo "==== Step 1: HTML5 TypeScript/Vite ===="
(
  cd "$ROOT/apps/studio-web"
  npm install --no-audit --no-fund
  npm run typecheck
  npm run build
)
if [[ ! -f "$ROOT/apps/studio-web/dist/index.html" ]]; then
  echo "Vite build did not produce dist/index.html" >&2
  exit 1
fi
echo "==== Step 1b: verify bundled ES module asset paths ===="
python3 tools/studio/test_web_bundle.py

echo "==== Step 2: CMake configure ===="
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DAIVERSE_BUILD_EDITOR=ON \
  -DAIVERSE_BUILD_STUDIO_HTML5=ON \
  -DAIVERSE_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug

echo "==== Step 3: native target and tests ===="
cmake --build "$BUILD_DIR" \
  --target aiverse_studio aiverse_studio_content_tests aiverse_studio_asset_catalog_tests aiverse_studio_session_tests aiverse_studio_lighting_validation_tests aiverse_studio_material_validation_tests aiverse_studio_pbr_adapter_tests aiverse_studio_shader_graph_name_tests aiverse_studio_shader_graph_draft_tests aiverse_studio_light_preview_math_tests aiverse_studio_authored_lights_tests aiverse_studio_fps_lighting_contract_tests aiverse_studio_fps_lighting_document_tests aiverse_studio_scene_nodes_tests aiverse_studio_document_visuals_tests aiverse_studio_viewport_layout_tests aiverse_studio_terrain_tests \
  --parallel 4

echo "==== Step 4: studio contracts ===="
ctest --test-dir "$BUILD_DIR" --output-on-failure \
  -R '^studio_(content_directory|asset_catalog|session|shader_graph_name|shader_graph_draft|pbr_adapter|material_validation|lighting_validation|light_preview_math|authored_lights|fps_lighting_contract|fps_lighting_document|scene_nodes|document_visuals|viewport_layout|terrain_geometry)$'

echo "==== Automated verification finished; macOS visual testing still required ===="
echo "Open the generated AiverseStudioHTML5.app with --project /path/to/project."
echo "Manually verify Metal rendering, resizing, picking, history and native popovers."
