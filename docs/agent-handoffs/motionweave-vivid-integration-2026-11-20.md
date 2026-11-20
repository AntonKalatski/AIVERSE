# MotionWeave + VIVID integration — 2026-11-20

Branch: `integration/motionweave-studio-facial-stage184`.
Merge implementation: `1d8c76e99c924f0373067ba413fac7b5d4803018`.
Parents: Studio/MotionWeave `04b4b3dc3` and VIVID `b5bbd3b319f67758ee0bb61c9061a78f99c00c25`.
User explicitly requested this merge and further joint completion. ALL subsequent builds, logs and artifacts MUST live on KINGSTON: `/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/`. Isolated source worktree: `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/motionweave-completion`. Main user Studio worktree not modified.

## Reconciliation
15 conflicted files reconciled, retaining both histories and independent features: Model upload keeps 64-channel source morph bounds/aliases and transactional shared-texture publication; Renderer3D keeps Studio cache/batching and VIVID dynamic vertex override/provenance. Dynamic overrides bypass static caches, legacy morph blending and LOD. Tracked submission frames bypass packet replay to avoid stale invocation IDs; pure cache policy test added. Metal completed serial and retirement token APIs both retained; Vulkan failed fence does not collect garbage. WorldScene joins CPU workers and fences GPU before release, retaining Terra/NEREID cleanup. FPS timing/router and animation resource retirement both retained. Studio renderer receives the same GPU epoch as facial preview and polls retirements at frame boundary. Shared ModelVertex stays 60 bytes, MotionWeave remains pose owner; VIVID 52 semantic channel order and gameplay saves unchanged. MotionWeave Studio 64-channel shader ABI retained (requires hardware validation).

Fixed old web fixtures for facial_rig tab, current dock defaults and folders in assets envelope. No layout/runtime mock behavior changed.

## Actual verification
- `npm --prefix apps/studio-web run build`: PASS before merge (TypeScript + Vite).
- `npm --prefix apps/studio-web run test:facial`: PASS 4/4 before merge.
- `TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/tmp npm --prefix apps/studio-web run test:web`: PASS 73/73 after fixture fixes, log `web-tests.log` on KINGSTON.
- CMake Debug configure with Studio HTML5/tests/tools/runtime AutoLOD ON: PASS, native directory on KINGSTON.
- `cmake --build /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/native --target aiverse_studio aiverse_studio_facial_runtime_tests aiverse_studio_facial_preview_binding_tests aiverse_auto_lod_tests aiverse_vivid_creator aiverse_vivid_renderer_preview -j 8`: INCOMPLETE/FAIL detected in RtsView.cpp:49 (void init used as bool) and :498 (ScenePacketBatch has no empty). Common models and runtime compiled; full build not successful. See `build.log`.
- Native CTest, Metal pixels, GPU validation, Vulkan/GLES, performance: NOT RUN. CI not dispatched/queried. Imported historical Markdown trailing spaces/EOF blanks reported by cached diff-check; no conflict markers remain.

## Next
Fix RtsView API mismatches without changing animation ownership, finish native build, then focused combined animation/VIVID/Studio CTests and actual Metal preview. VIVID remote advanced during work; reconcile its portable fixture fix only after this checkpoint. Studio shared ModelAssetProvider/read lease/async decode adoption remains unfinished (Stage 186). No main merge, push, release or production-ready claim.

| Subsystem | Status | Remaining |
|---|---|---|
| Combined MotionWeave + VIVID source | implemented in source | native build fixes |
| Studio frontend | partial | native WKWebView screenshot |
| Shared provider in Studio facial preview | not implemented | adopt existing provider safely |
| GPU/backend acceptance | not tested | Metal pixel suite; Vulkan/GLES |
