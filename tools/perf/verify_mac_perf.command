#!/bin/bash
# Mac verification of the 2026-11-21 performance branches (merged in verify/aiv20261121_perf):
# Studio build + studio_* tests, game client build, two traced runs of the same map
# (new defaults vs. old behaviour via kill switches) and a pixel comparison of their
# last frames. Results: <repo>/logs/perf-verify/summary.txt (DONE marker at the end).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LOGS="$ROOT/logs/perf-verify"
mkdir -p "$LOGS"
exec > >(tee "$LOGS/run.log") 2>&1
echo "== start $(date) root=$ROOT commit=$(git -C "$ROOT" log -1 --format=%h)"
OUT=/Volumes/KINGSTON/AIVERSE-builds/perf-verify
if [[ ! -d /Volumes/KINGSTON || ! -w /Volumes/KINGSTON ]]; then OUT="$ROOT/build/perf-verify"; fi
mkdir -p "$OUT"
JOBS=$(sysctl -n hw.ncpu)
GEN=""; command -v ninja >/dev/null && GEN="-G Ninja"
SUMMARY="$LOGS/summary.txt"; : > "$SUMMARY"
note() { echo "$*" | tee -a "$SUMMARY"; }
note "out=$OUT jobs=$JOBS"

# ---- 1. Studio (RelWithDebInfo) and its tests
WEB=/Volumes/KINGSTON/AIVERSE-builds/studio-html5-verify/web
if [[ ! -f "$WEB/index.html" ]]; then
  WEB="$OUT/web"
  (cd "$ROOT/apps/studio-web" && npm install --no-audit --no-fund && ./node_modules/.bin/vite build --outDir "$WEB") > "$LOGS/studio-web.log" 2>&1 || note "STUDIO WEB BUILD FAILED (logs/perf-verify/studio-web.log)"
fi
cmake -S "$ROOT" -B "$OUT/studio" $GEN -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_STUDIO_HTML5=ON \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_STUDIO_WEB_DIST="$WEB" -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$LOGS/studio-configure.log" 2>&1 \
  && cmake --build "$OUT/studio" --parallel "$JOBS" --target aiverse_studio aiverse_studio_render_thread_tests \
     aiverse_studio_metal_lighting_tests aiverse_studio_session_tests aiverse_studio_viewport_layout_tests \
     aiverse_studio_document_visuals_tests aiverse_studio_scene_nodes_tests > "$LOGS/studio-build.log" 2>&1
note "studio build rc=$? (logs/perf-verify/studio-build.log)"
ctest --test-dir "$OUT/studio" --output-on-failure -R '^studio_(render_thread|metal_lighting|session|viewport_layout|document_visuals|scene_nodes)$' > "$LOGS/studio-tests.log" 2>&1
note "studio tests rc=$? $(grep -E 'tests passed|tests failed' "$LOGS/studio-tests.log" | tail -1)"

# ---- 2. Game client (Release) on a project folder, like the editor's native build
PROJECT=""
for p in /Volumes/KINGSTON/AIVERSE-builds/studio-html5-completion/qa-lua-lab "$ROOT/templates/rts" "$ROOT/examples/rts"; do
  if [[ -f "$p/rules.json" && -d "$p/maps" ]]; then PROJECT="$p"; break; fi
done
note "project=$PROJECT"
if [[ -n "$PROJECT" ]]; then
  cmake -S "$ROOT" -B "$OUT/client" $GEN -DCMAKE_BUILD_TYPE=Release -DAIVERSE_CONTENT_DIR="$PROJECT" -DAIVERSE_NATIVE_DIR= \
    -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_CLIENT=ON -DAIVERSE_CLIENT_BUNDLE_CONTENT=OFF \
    -DAIVERSE_GAME_EXECUTABLE=AiverseGame > "$LOGS/client-configure.log" 2>&1 \
    && cmake --build "$OUT/client" --parallel "$JOBS" --target aiverse_client > "$LOGS/client-build.log" 2>&1
  note "client build rc=$? (logs/perf-verify/client-build.log)"
  BIN="$OUT/client/AiverseGame.app/Contents/MacOS/AiverseGame"
  run_mode() { # name, then env assignments
    local name=$1; shift
    rm -f "$LOGS/$name.ppm"
    ( cd "$PROJECT" && env "$@" AIVERSE_CONTENT="$PROJECT" AIVERSE_SEED=7 AIVERSE_SPLASH=off AIVERSE_FRAME_TRACE=1 \
        AIVERSE_BENCH_UNCAPPED=1 AIVERSE_EXIT_AFTER_FRAMES=1500 AIVERSE_SCREENSHOT="$LOGS/$name.ppm" \
        "$BIN" --map 0 --size 1280x800 > "$LOGS/$name.log" 2>&1 ) &
    local pid=$!
    ( sleep 300; kill $pid 2>/dev/null ) & local guard=$!
    wait $pid; local rc=$?
    kill $guard 2>/dev/null
    note "run $name rc=$rc"
    # warm averages: the last 5 samples of each trace line
    for tag in mac-frame view-stages view-objects rts-view-cull rts-placement rts-static-animate rts-ground-cache scene-stages; do
      grep "\[$tag\] avg" "$LOGS/$name.log" | tail -5 | sed "s/^/  $name /" >> "$SUMMARY"
    done
  }
  if [[ -x "$BIN" ]]; then
    run_mode new
    run_mode old AIVERSE_RTS_VIEW_CULL=0 AIVERSE_RTS_PLACEMENT_CACHE=0 AIVERSE_RTS_STATIC_ANIMATE_FAST=0 \
      AIVERSE_RTS_GROUND_CACHE=0 AIVERSE_METAL_EARLY_DRAWABLE=1
    python3 - "$LOGS/new.ppm" "$LOGS/old.ppm" >> "$SUMMARY" 2>&1 <<'PY'
import sys
def read(p):
    d=open(p,'rb').read()
    parts=d.split(maxsplit=4)
    w,h=int(parts[1]),int(parts[2])
    return w,h,parts[4][-w*h*3:]
try:
    a,b=read(sys.argv[1]),read(sys.argv[2])
    if a[:2]!=b[:2]: print('pixels: size differs',a[:2],b[:2]); raise SystemExit
    pa,pb=a[2],b[2]; n=len(pa)
    diff=sum(abs(x-y) for x,y in zip(pa,pb))
    changed=sum(1 for i in range(0,n,3) if abs(pa[i]-pb[i])+abs(pa[i+1]-pb[i+1])+abs(pa[i+2]-pb[i+2])>24)
    print('pixels: %dx%d mean abs diff %.3f/255, changed pixels %d (%.3f%%)'%(a[0],a[1],diff/n,changed,100*changed/(n//3)))
except Exception as e: print('pixels: comparison failed',e)
PY
  fi
fi
note "DONE $(date)"
