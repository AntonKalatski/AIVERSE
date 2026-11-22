# STRATA hardware acceptance — handoff (2026-11-22)

Branch `agent-strata-accept` (worktree `/Volumes/KINGSTON/AIVERSE-builds/worktrees/agent-strata-accept`).
Commits: c6d4e3ab2 (stage bench, `[strata]` device log, reflection fixture clock, MoltenVK loader path),
5d9504abf (merge agent-wind-masks 8f63db5c0: multi-million staging, compacted submission),
a81aa19b9 (merge origin/main), f12342c9c (tolerant bench, LOD_CONNECT_WAIT), dd4996e0d (docs + drivers).

Results and the default-on decision: `docs/strata.md`, section "Hardware acceptance and
default-on decision (2026-11-22)". STRATA stays OFF by default (no default change made).

Executed (native arm64 Release, Mac Studio M3 Ultra; logs in `/Volumes/KINGSTON/AIVERSE-builds/strata-accept/logs`):
- `ctest -R '^strata_'` 19/19 PASS; `strata_vulkan_kernel` runtime SKIP (no Vulkan loader on the Mac).
- 6 Metal fixtures PASS (reflection fixture after the frozen-clock fix).
- `aiverse_strata_stage_bench` on loarre.glb and Sponza 2022.
- Editor castle scene S/L ×2 via `tools/editor/lod_diag.py` (CASTLE_SHOT).
- Android A05s GLES/Vulkan on/off (v1 ×2 without compaction, v2 ×1 with compaction).
NOT RUN: WebGL (no emsdk), Vulkan on macOS (no loader/MoltenVK), spot shadow, lossy seam ROI,
residency pressure orbit; Linux/GCC build of the new Vulkan portability lines (Android clang built it).

Next: STRATA archives at import + runtime load (or worker staging with LOD fallback) to remove the
37 s / ~6.5 min load stall and the 10.6 GB Sponza peak; then re-run `tools/strata/accept-*.sh` and
apply the criteria; install emsdk and MoltenVK (owner consent) for the remaining backends.
