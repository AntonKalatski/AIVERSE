#!/bin/bash
# Copy of the 2026-11-22 STRATA acceptance driver (docs/strata.md, "Hardware acceptance").
# Paths are KINGSTON defaults; run from a .command on the Mac.
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
W=/Volumes/KINGSTON/AIVERSE-builds/strata-accept; B=$W/arm64; L=$W/logs
exec > $L/stage-bench4.log 2>&1
echo "== $(date) load=$(sysctl -n vm.loadavg)"
cmake --build "$B" --target aiverse_strata_stage_bench --parallel 4 2>&1 | tail -2
for a in /Volumes/KINGSTON/AIVERSE-builds/worktrees/agent-wind-masks/.wind-qa/castle/loarre.glb /Volumes/KINGSTON/AIVERSE-builds/assets/sponza2022/base/main_sponza/NewSponza_Main_glTF_003.gltf; do
  echo "== $a"; /usr/bin/time -l "$B/aiverse_strata_stage_bench" "$a" 1 2>&1 | grep -E "strata-stage|real|maximum resident" | grep -v "skipped primitive" | head -20
  "$B/aiverse_strata_stage_bench" "$a" 1 2>&1 | grep -c "skipped primitive"
done
echo "== DONE $(date)"
