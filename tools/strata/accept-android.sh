#!/bin/bash
# Copy of the 2026-11-22 STRATA acceptance driver (docs/strata.md, "Hardware acceptance").
# Paths are KINGSTON defaults; run from a .command on the Mac.
# STRATA on/off on an Android device over USB: GLES and Vulkan, frame times + screencaps.
set -u
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
SRC=/Volumes/KINGSTON/AIVERSE-builds/worktrees/agent-strata-accept
W=/Volumes/KINGSTON/AIVERSE-builds/strata-accept
B=$W/arm64; A=$W/android; L=$W/logs; mkdir -p "$A/${RUNDIR:-runs}" "$L" "$W/tmp"; export TMPDIR="$W/tmp"
ST=$L/android-status.txt
say(){ echo "$(date +%H:%M:%S) $*" | tee -a "$ST"; }
STEPS="${STEPS:-build run}"
mkdir "$W/android.lock" 2>/dev/null || { say "another Android pass holds $W/android.lock: skipped"; exit 0; }
trap 'rmdir "$W/android.lock"' EXIT
P=$A/strata-castle
ADB=~/Library/Android/sdk/platform-tools/adb; [[ -x $ADB ]] || ADB="$(command -v adb)"
SDK=~/Library/Android/sdk
if [[ " $STEPS " == *" build "* ]]; then
  say "host aiversebuild"
  cmake --build "$B" --target aiversebuild --parallel 8 > "$L/aiversebuild-build.log" 2>&1 || { say "FAIL aiversebuild"; exit 1; }
  AB="$(find "$B" -maxdepth 4 -name aiversebuild -type f -perm -u+x | head -1)"
  rm -rf "$P"; cp -R "$SRC/templates/skirmish" "$P"
  mkdir -p "$P/content/models/castle"
  cp /Volumes/KINGSTON/AIVERSE-builds/worktrees/agent-wind-masks/.wind-qa/castle/loarre.glb "$P/content/models/castle/"
  cat > "$P/strata-castle.json" <<'J'
{"version":1,
 "strata":{"enabled":true,"select":"auto","hysteresis":0.1,"reflections":"main","mergeClusters":true,"shadowErrorScale":8},
 "instances":[{"id":"castle","model":"content/models/castle/loarre.glb","position":[32,0,40],"rotation":[0,25,0],"scale":[0.002,0.002,0.002]}]}
J
  python3 - "$P/game.json" <<'PY'
import sys,re
p=sys.argv[1]; t=open(p).read()
t=t.replace('"rules": "rules.json",','"rules": "rules.json",\n  "geometryScene": "strata-castle.json",',1)
t=t.replace('"id": "com.aiverse.ironandgold"','"id": "com.aiverse.strataaccept"',1).replace('"name": "Iron and Gold"','"name": "STRATA Accept"',1)
open(p,'w').write(t)
PY
  grep -n "geometryScene\|\"id\"" "$P/game.json" | head -3 >> "$ST"
  say "android package (long)"
  "$AB" --project "$P" --engine "$SRC" --target android --config Release --out "$A/apk" --install-missing > "$L/android-build.log" 2>&1; rc=$?
  grep -E "error:|ОШИБКА|FAILURE|APK|размер" "$L/android-build.log" | sort -u | head -20 >> "$ST"
  [[ $rc == 0 ]] || { say "FAIL android build rc=$rc"; exit 1; }
fi
if [[ " $STEPS " == *" run "* ]]; then
  APK="$(ls -t "$A"/apk/android/*-release.apk "$A"/apk/.temp/build/android/*/app/build/outputs/apk/release/app-release.apk 2>/dev/null | head -1)"
  DEV="$("$ADB" devices | awk 'NR>1 && $2=="device"{print $1; exit}')"
  [[ -n "$DEV" && -n "$APK" ]] || { say "FAIL dev=$DEV apk=$APK"; exit 1; }
  D(){ "$ADB" -s "$DEV" "$@"; }
  PKG="$(ls -d $SDK/build-tools/*/aapt2 | tail -1 | xargs -I{} {} dump packagename "$APK" | tail -1)"
  [[ -n "$PKG" ]] || PKG=com.aiverse.strataaccept
  say "install $APK pkg=$PKG dev=$DEV $(D shell getprop ro.product.model)"
  D install -r "$APK" > "$L/android-install.log" 2>&1 || { say "FAIL install"; exit 1; }
  ACT="$(D shell cmd package resolve-activity --brief "$PKG" | tail -1 | tr -d '\r')"
  clearprops(){ D shell setprop debug.aiverse.args "''"; for i in $(seq 0 15); do D shell setprop debug.aiverse.env$i "''"; done; }
  FR=${FRAMES:-1200}
  run(){ # tag gfx strata(on|off)
    local tag=$1 gfx=$2 mode=$3 log="$A/${RUNDIR:-runs}/$1.log"
    D shell am force-stop "$PKG"; clearprops
    D shell setprop debug.aiverse.args "'--map 0 --spectate --gfx $gfx'"
    local i=0
    for kv in AIVERSE_PERF_LOG=5 AIVERSE_EXIT_AFTER_FRAMES=$FR AIVERSE_SEED=3 AIVERSE_GFX=$gfx AIVERSE_STRATA_LOG=1 "AIVERSE_CAM=${CAM:-32,40,26,38,200}" $([[ $mode == off ]] && echo AIVERSE_STRATA=off); do
      D shell setprop debug.aiverse.env$i "'$kv'"; i=$((i+1)); done
    D logcat -c; local t0=$(date +%s); D shell am start -W -n "$ACT" > /dev/null
    local t=0 res="" shot=0
    while [[ $t -lt ${RUNWAIT:-900} ]]; do
      sleep 1; t=$((t+1))
      if [[ $shot == 0 ]] && D logcat -d -s AIVERSE:I | grep -q "\[strata\]"; then D exec-out screencap -p > "$A/${RUNDIR:-runs}/$tag.png"; shot=1; fi
      res="$(D logcat -d -s AIVERSE:I | grep -m1 '\[perf\] summary')"; [[ -n "$res" ]] && break
      if [[ $t -gt 30 && -z "$(D shell pidof "$PKG" | tr -d '\r')" ]]; then res="CRASH"; break; fi
    done
    D logcat -d > "$log"; D shell am force-stop "$PKG"
    local thermal="$(D shell dumpsys thermalservice 2>/dev/null | grep -m1 -i 'Thermal Status' | grep -oE '[0-9]+$')"
    say "$tag: ${res:-TIMEOUT} | $(grep -m1 -oE '\[strata\].*' "$log" | head -c 400) | thermal=$thermal t=${t}s shot=$shot"
    grep -E "\[gfx\]|geometry scene|strata|STRATA" "$log" | grep -v "\[strata\]" | head -5 >> "$ST"
    sleep 10
  }
  for rep in ${REPS:-1 2}; do
    for gfx in gles vulkan; do
      run "${gfx}-on-$rep" $gfx on
      run "${gfx}-off-$rep" $gfx off
    done
  done
  clearprops
fi
say "DONE"
