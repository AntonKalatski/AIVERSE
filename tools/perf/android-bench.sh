#!/bin/bash
# Android build + GLES / Vulkan benchmark on a device connected over USB (macOS or Linux host).
#   tools/perf/android-bench.sh [--no-build | --fast] [--project templates/skirmish] [--map 0] [--frames 1200]
#                               [--cases quick|features] [--out /Volumes/KINGSTON/AIVERSE-builds/android-bench]
# Builds aiversebuild (host), packages the project as a Release APK, installs it and runs every case
# twice (gles, vulkan). Settings reach the game through debug.aiverse.* properties (PlatformAndroid.cpp);
# frame times come from its "[perf]" logcat lines. Results: <out>/results.tsv, raw logs in <out>/runs/.
set -u
# bash reads a script while running it: run a private copy so edits of this file can't hit a running bench
if [ -z "${AIVERSE_BENCH_ROOT:-}" ]; then
  export AIVERSE_BENCH_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
  c="${TMPDIR:-/tmp}/android-bench.$$.sh"; cp "$0" "$c"; exec bash "$c" "$@"
fi
ROOT="$AIVERSE_BENCH_ROOT"
XENV=""; PROFILE=""; PSEC=12
PROJECT="$ROOT/templates/skirmish"; MAP=0; FRAMES=1200; CASES=features; BUILD=1
OUT=/Volumes/KINGSTON/AIVERSE-builds/android-bench
while [ $# -gt 0 ]; do case "$1" in
  --no-build) BUILD=0;; --fast) BUILD=2;; --project) PROJECT="$2"; shift;; --map) MAP="$2"; shift;; --frames) FRAMES="$2"; shift;;
  --cases) CASES="$2"; shift;; --env) XENV="${XENV:-} $2"; shift;; --profile) PROFILE="$2"; shift;; --seconds) PSEC="$2"; shift;; --out) OUT="$2"; shift;; *) echo "unknown arg $1"; exit 2;; esac; shift; done
case "$PROJECT" in /*) ;; *) PROJECT="$ROOT/$PROJECT";; esac
NAME="$(basename "$PROJECT")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
case "$OUT" in /Volumes/KINGSTON/*) [ -w /Volumes/KINGSTON ] || { echo "ОШИБКА: /Volumes/KINGSTON не подключён"; exit 1; };; esac
mkdir -p "$OUT/runs"
STATUS="$OUT/status.txt"
say() { echo "== $(date +%H:%M:%S) $*"; echo "$(date +%H:%M:%S) $*" >> "$STATUS"; }
: > "$STATUS"

# ---- adb
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
for c in "$SDK" "$HOME/.aiverse/toolchains/android-sdk" "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do
  [ -n "$c" ] && [ -x "$c/platform-tools/adb" ] && { SDK="$c"; break; }; done
ADB="$(command -v adb || true)"; [ -x "$SDK/platform-tools/adb" ] && ADB="$SDK/platform-tools/adb"

# ---- build
APK_DIR="$OUT/apk/$NAME"
if [ $BUILD = 1 ]; then
  say "host: aiversebuild"
  cmake -S "$ROOT" -B "$OUT/host" -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_EDITOR=OFF > "$OUT/host-configure.log" 2>&1 \
    || { say "FAIL host configure (host-configure.log)"; tail -30 "$OUT/host-configure.log"; exit 1; }
  cmake --build "$OUT/host" --target aiversebuild -j 8 > "$OUT/host-build.log" 2>&1 \
    || { say "FAIL host build (host-build.log)"; grep -E "error" "$OUT/host-build.log" | head -30; exit 1; }
  AB="$(find "$OUT/host" -name aiversebuild -type f -perm -u+x | head -1)"
  say "project copy: $NAME"
  mkdir -p "$OUT/projects"; rm -rf "$OUT/projects/$NAME"; cp -R "$PROJECT" "$OUT/projects/$NAME"
  say "android: aiversebuild --target android --config Release (долго: движок x2 ABI)"
  "$AB" --project "$OUT/projects/$NAME" --engine "$ROOT" --target android --config Release --out "$APK_DIR" --install-missing > "$OUT/android-build.log" 2>&1
  rc=$?
  grep -E "error:|ОШИБКА|FAILURE|APK" "$OUT/android-build.log" | sort -u | head -40
  [ $rc = 0 ] || { say "FAIL android build rc=$rc (android-build.log)"; exit 1; }
  G="$(ls -d "$APK_DIR"/.temp/build/android/*/ 2>/dev/null | head -1)"; [ -n "$G" ] && git -C "$ROOT" rev-parse HEAD > "$G/.aiverse-export-commit"
  for c in "$HOME/.aiverse/toolchains/android-sdk" "$HOME/Library/Android/sdk"; do
    [ -z "$ADB" ] && [ -x "$c/platform-tools/adb" ] && { SDK="$c"; ADB="$c/platform-tools/adb"; }; done
fi
# --fast: reuse the exported Gradle project of the last full build, copy only the engine files whose
# content changed (incremental native build), arm64 only
GP="$(ls -d "$APK_DIR"/.temp/build/android/*/ 2>/dev/null | head -1)"
if [ $BUILD = 2 ]; then
  [ -n "$GP" ] && [ -x "$GP/gradlew" ] || { say "FAIL: --fast needs a full build first"; exit 1; }
  say "fast: sync changed sources into $GP"
  for d in "$GP"app/src/main/cpp/*; do
    n="$(basename "$d")"; [ -e "$ROOT/$n" ] || continue
    if [ -d "$d" ]; then rsync -rc --existing "$ROOT/$n/" "$d/"; else rsync -c "$ROOT/$n" "$d"; fi
  done
  # files added since the export (the content sync above only updates existing ones): copy them when
  # their folder is part of the exported tree
  BASE="$(cat "$GP/.aiverse-export-commit" 2>/dev/null || echo f53826ef9)"
  git -C "$ROOT" diff --name-only --diff-filter=A "$BASE" -- . 2>/dev/null | while read -r f; do
    [ -d "$GP/app/src/main/cpp/$(dirname "$f")" ] && cp "$ROOT/$f" "$GP/app/src/main/cpp/$f" && echo "fast: + $f"; done
  sed -i.bak "s/abiFilters 'arm64-v8a', 'x86_64'/abiFilters 'arm64-v8a'/" "$GP/app/build.gradle"
  # profiling over adb (simpleperf --app) without making the APK debuggable; bench builds only
  M="$GP/app/src/main/AndroidManifest.xml"
  grep -q "<profileable" "$M" || perl -0pi -e 's/(<application[^>]*>)/$1\n        <profileable android:shell="true" \/>/' "$M"
  # the same JDK range aiversebuild accepts (Gradle 8.10 / AGP 8.7 need 17..23)
  unset JAVA_HOME
  for j in "$HOME/.aiverse/toolchains"/*/ "$HOME/.aiverse/toolchains"/*/Contents/Home/ /Library/Java/JavaVirtualMachines/*/Contents/Home/ \
           "/Applications/Android Studio.app/Contents/jbr/Contents/Home/" "$HOME/Library/Java/JavaVirtualMachines"/*/Contents/Home/; do
    [ -x "$j/bin/java" ] && [ -f "$j/release" ] || continue
    v="$(grep -m1 '^JAVA_VERSION=' "$j/release" | tr -d '"' | cut -d= -f2 | cut -d. -f1)"
    [ -n "$v" ] && [ "$v" -ge 17 ] && [ "$v" -le 23 ] && { export JAVA_HOME="${j%/}"; break; }
  done
  say "fast: gradlew assembleRelease (JAVA_HOME=${JAVA_HOME:-?})"
  (cd "$GP" && ./gradlew assembleRelease --console=plain) > "$OUT/android-build.log" 2>&1 \
    || { say "FAIL fast build (android-build.log)"; grep -E "error:" "$OUT/android-build.log" | sort -u | head -30; exit 1; }
  APK="$GP/app/build/outputs/apk/release/app-release.apk"
  for c in "$HOME/.aiverse/toolchains/android-sdk" "$HOME/Library/Android/sdk"; do
    [ -z "$ADB" ] && [ -x "$c/platform-tools/adb" ] && { SDK="$c"; ADB="$c/platform-tools/adb"; }; done
else
  APK="$(find "$APK_DIR" -maxdepth 3 -name '*-release.apk' -not -path '*/.temp/*' | head -1)"
fi
[ -n "$APK" ] || { say "FAIL: нет APK в $APK_DIR"; exit 1; }
[ -x "$ADB" ] || { say "FAIL: adb не найден"; exit 1; }

# ---- device
"$ADB" start-server >/dev/null 2>&1
DEV="$("$ADB" devices | awk 'NR>1 && $2=="device"{print $1; exit}')"
[ -n "$DEV" ] || { say "FAIL: устройство не видно (adb devices: $("$ADB" devices | tail -n +2 | tr '\n' ' '))"; exit 1; }
A() { "$ADB" -s "$DEV" "$@"; }
AAPT="$(ls -d "$SDK"/build-tools/*/aapt2 2>/dev/null | tail -1)"
PKG="$("$AAPT" dump packagename "$APK" 2>/dev/null | tail -1)"
{
  echo "device=$(A shell getprop ro.product.manufacturer) $(A shell getprop ro.product.model) android=$(A shell getprop ro.build.version.release) sdk=$(A shell getprop ro.build.version.sdk)"
  echo "soc=$(A shell getprop ro.soc.model) $(A shell getprop ro.board.platform) vulkan_hw=$(A shell getprop ro.hardware.vulkan) egl=$(A shell getprop ro.hardware.egl)"
  A shell dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -E "^GLES:"
  A shell wm size; A shell dumpsys display 2>/dev/null | grep -m1 -oE "renderFrameRate [0-9.]+|fps=[0-9.]+"
  echo "apk=$APK pkg=$PKG size=$(du -h "$APK" | cut -f1)"
} | tee "$OUT/device.txt"
say "install $PKG"
A install -r "$APK" > "$OUT/install.log" 2>&1 || { say "FAIL install"; cat "$OUT/install.log"; exit 1; }
ACT="$(A shell cmd package resolve-activity --brief "$PKG" | tail -1 | tr -d '\r')"

clearprops() { A shell setprop debug.aiverse.args "''"; for i in $(seq 0 15); do A shell setprop debug.aiverse.env$i "''"; done; }
thermal() { A shell dumpsys thermalservice 2>/dev/null | grep -m1 -i "Thermal Status" | grep -oE "[0-9]+$"; }
battemp() { A shell dumpsys battery | grep -m1 temperature | grep -oE "[0-9]+$"; }

# one run: name gfx env...
run() {
  local name="$1" gfx="$2"; shift 2
  local tag="${name}_${gfx}" log="$OUT/runs/${NAME}_${name}_${gfx}.log"
  A shell am force-stop "$PKG"; clearprops
  A shell setprop debug.aiverse.args "'--map $MAP --spectate --gfx $gfx'"
  local i=0
  for kv in AIVERSE_PERF_LOG=5 AIVERSE_EXIT_AFTER_FRAMES=$FRAMES AIVERSE_SEED=3 AIVERSE_GFX=$gfx $XENV "$@"; do
    A shell setprop debug.aiverse.env$i "'$kv'"; i=$((i+1)); done
  local th0="$(thermal)" bt0="$(battemp)"
  A logcat -c; A shell am start -W -n "$ACT" > /dev/null
  local t=0 res=""
  while [ $t -lt $((FRAMES/20 + 150)) ]; do
    sleep 2; t=$((t+2))
    res="$(A logcat -d -s AIVERSE:I | grep -m1 '\[perf\] summary')"
    [ -n "$res" ] && break
    if [ $t -gt 20 ] && [ -z "$(A shell pidof "$PKG" | tr -d '\r')" ]; then res="CRASH"; break; fi
  done
  A logcat -d > "$log"; A logcat -d -b crash >> "$log" 2>/dev/null
  A shell am force-stop "$PKG"
  local be="$(echo "$res" | grep -oE 'backend=[a-z]+' | cut -d= -f2)"
  [ -z "$res" ] && res="TIMEOUT"
  getv() { echo "$res" | grep -oE "$1=[0-9.]+" | head -1 | cut -d= -f2; }
  local fps=$(echo "$res" | grep -oE 'fps=[0-9.]+' | cut -d= -f2)
  local wavg=$(echo "$res" | grep -oE 'wall_ms avg=[0-9.]+' | cut -d= -f2)
  local p50=$(getv p50) p95=$(getv p95) p99=$(getv p99) mx=$(getv max) jank=$(getv jank33)
  local cavg=$(echo "$res" | grep -oE 'cpu_ms avg=[0-9.]+' | cut -d= -f2)
  local gpu="$(grep -m1 -oE '\[gfx\] (Vulkan .*|OpenGL.*|GL .*)' "$log" | head -c 120)"
  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$name" "$gfx" "${be:-?}" "${fps:-}" "${wavg:-}" "${p50:-}" "${p95:-}" "${p99:-}" "${mx:-}" "${cavg:-}" "${jank:-}" "$th0" "$bt0" "$(echo $res | grep -qE 'CRASH|TIMEOUT' && echo $res)" "$gpu" >> "$OUT/results.tsv"
  say "$tag: fps=${fps:-?} p95=${p95:-?} cpu=${cavg:-?} backend=${be:-?} ${res:0:7}"
  sleep 8   # a short cool-down between runs
}

printf "case\tgfx\tbackend\tfps\twall_avg\tp50\tp95\tp99\tmax\tcpu_avg\tjank33\tthermal0\tbattemp0\tfail\tgpu\n" > "$OUT/results.tsv"
U="AIVERSE_ADAPTIVE=off AIVERSE_BENCH_UNCAPPED=1"
cases_quick() {
  for g in gl vulkan; do run vsync_default $g; done
  for g in gl vulkan; do run uncapped $g $U; done
}
# render resolution (PlatformAndroid applyRenderScale) on default tiers, vsync on and off
cases_scale() {
  for g in gl vulkan; do run vsync_default $g; done
  for g in gl vulkan; do run uncapped $g $U; done
  for g in gl vulkan; do run native_res $g $U AIVERSE_RENDER_SCALE=1; done
  for g in gl vulkan; do run scale_050 $g $U AIVERSE_RENDER_SCALE=0.5; done
}
cases_features() {
  cases_quick
  for c in "shadows_off AIVERSE_SHADOWS=off" "shadows_high AIVERSE_SHADOWS=high" "postfx_off AIVERSE_POSTFX=off" "postfx_high AIVERSE_POSTFX=high" \
           "hdr_off AIVERSE_HDR=off" "particles_off AIVERSE_PARTICLES=off" "lights_off AIVERSE_LIGHTS=off" "materials_low AIVERSE_MATERIALS=low" \
           "groundfx_low AIVERSE_GROUNDFX=low" "fields_low AIVERSE_FIELDS=low" "all_low AIVERSE_SHADOWS=off AIVERSE_POSTFX=off AIVERSE_LIGHTS=low AIVERSE_MATERIALS=low AIVERSE_PARTICLES=low"; do
    set -- $c; local n="$1"; shift
    for g in gl vulkan; do run "$n" $g $U "$@"; done
  done
}
# --profile gl|vulkan|gl,vulkan: simpleperf (cpu-clock) for --seconds while the match runs; reports by thread,
# library and address (symbolize the libmain.so addresses with addr2line on the unstripped library)
profile() {
  local gfx="$1" P="$OUT/profile_${NAME}_$gfx"; mkdir -p "$P"
  A shell am force-stop "$PKG"; clearprops
  A shell setprop debug.aiverse.args "'--map $MAP --spectate --gfx $gfx'"
  local i=0
  for kv in AIVERSE_PERF_LOG=2 AIVERSE_SEED=3 AIVERSE_GFX=$gfx $XENV; do A shell setprop debug.aiverse.env$i "'$kv'"; i=$((i+1)); done
  A logcat -c; A shell am start -W -n "$ACT" > /dev/null
  say "profile $gfx: waiting for the match"
  local t=0; while [ $t -lt 120 ] && ! A logcat -d -s AIVERSE:I | grep -q '\[perf\] window'; do sleep 2; t=$((t+2)); done
  sleep 4
  say "profile $gfx: simpleperf ${PSEC}s"
  A shell rm -f /data/local/tmp/perf.data
  A shell simpleperf record --app "$PKG" -e cpu-clock -f 2000 --call-graph fp --duration "$PSEC" -o /data/local/tmp/perf.data > "$P/record.txt" 2>&1
  A logcat -d > "$P/logcat.txt"
  A shell am force-stop "$PKG"
  A shell simpleperf report -i /data/local/tmp/perf.data --sort comm,tid -n > "$P/threads.txt" 2>&1
  A shell simpleperf report -i /data/local/tmp/perf.data --sort comm,dso -n > "$P/dso.txt" 2>&1
  A shell simpleperf report -i /data/local/tmp/perf.data --sort comm,dso,symbol -n --percent-limit 0.2 > "$P/symbols.txt" 2>&1
  A shell simpleperf report -i /data/local/tmp/perf.data --sort comm,dso,vaddr_in_file -n --percent-limit 0.01 > "$P/vaddr.txt" 2>&1
  A shell simpleperf report -i /data/local/tmp/perf.data -g --children --sort comm,dso,symbol -n --percent-limit 1 > "$P/children.txt" 2>&1
  A pull /data/local/tmp/perf.data "$P/perf.data" > /dev/null 2>&1
  say "profile $gfx: done -> $P"
}
if [ -n "$PROFILE" ]; then
  for g in ${PROFILE//,/ }; do profile "$g"; done
  clearprops; say "DONE profile"; exit 0
fi
say "bench $NAME map=$MAP frames=$FRAMES cases=$CASES env=$XENV"
cases_$CASES
clearprops
say "DONE results: $OUT/results.tsv"
column -t -s $'\t' "$OUT/results.tsv" | cut -c1-160
