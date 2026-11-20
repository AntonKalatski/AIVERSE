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

## Native build checkpoint — 2026-11-20

Implementation: `4f7d6afd60c962b7a6594aea97f8eef01acc0ce0`, same integration branch. Fixed RTS retirement result forwarding and ScenePacketBatch size query; qualified shared render::findNodePath and made retarget/loader headers self-contained; corrected VIVID groom subtraction, Creator slider helper pointer type, and portable gfx fixture namespace (also present in upstream 6064542).

Actual KINGSTON builds: Studio, facial runtime/binding, AutoLOD, VIVID Creator PASS. Remaining acceptance target build still running in `build-fix4.log`. Earlier failed attempts retained as build.log/build-fix1..3.log. No animation save schema, 52 channel ordering, UI layout or gameplay ownership changes.

Unified Debug runner (tests-first.log): 12/17 PASS, 5 FAIL: auto_lod_runtime (malformed envelope throws), vivid_human_appearance (irisRadius fixture out of schema bounds), vivid_human_import (invalid fixture accepted), vivid_metal_sculpt_id (record rejected), vivid_actor_retirement (initialization rejected). Real Metal GPU and RHI tests PASS. Studio facial runtime and binding PASS. Python Studio contracts: 24/29 PASS; stale CMake/asset catalog/doc assertions and Python 3.8 Path.is_relative_to compatibility need repair. Do not report full suite passing. CI NOT RUN; GUI/pixel preview NOT RUN yet.

Next: finish acceptance target build, diagnose the five failures without relaxing runtime validation, update stale contracts, run combined tests and real Renderer3D preview. Studio async provider adoption remains open. Correction to previous entry: RTS error was void collectRetired used as bool (not init).

## Combined Metal acceptance — 2026-11-20

Code: `647b7bc2e07ab1f99ca567022ac94cfed0d80dec` on this branch. Fixed reserved MSL identifiers `vertex` and `thread` in both actual-stream expansion kernels without changing CPU/GPU struct layout. GPU actor initialization now works before beginFrame; dispatch still rejects zero frame serial, covered by the original zero-frame fake RHI plus new rejection assertion. Updated malformed AutoLOD rejection, valid iris bound, real newline rejection and portable namespace fixtures. Studio source-contract assertions follow current asset catalog and documentation; bundle check supports system Python 3.8.

Actual verification (KINGSTON):
- Native selected Debug tests: 24/24 PASS (`tests-second.log`). Additional owner/Metal ID/actor regression: 3/3 PASS (`tests-gpu-fixed.log`), actor overlaps previous set: 26 distinct passing tests.
- Real Metal sculpt ID/depth/readback: PASS, `metal-id-fixed.log`.
- Real single-actor Renderer3D + MotionWeave preview: PASS (`preview-actor-fixed.log`); 81,989 morph-changed pixels, 63,241 MotionWeave-changed pixels. Both same-frame colour/ID depth probes PASS with zero reported delta. PPM files: preview-baseline, preview-morph, preview-motionweave on KINGSTON. These verify a procedural fixture, not photoreal human quality.
- Studio Python: 29/29 PASS (`studio-python-final.log`); TypeScript typecheck PASS (`web-typecheck.log`). Web suite remains 73/73 PASS from merge.
- VIVID Creator, preview, Studio and focused targets compile. CI, Vulkan/GLES, complete regression, Studio visual parity, imported Creator and mask/crowd GUI cases NOT RUN yet.

Next: validate mask and crowd scenes; close 64-slot VIVID adapter coverage; continue shared provider/lease adoption in Studio. No performance or full production parity claim.

## Full palette and masked scene checkpoint — 2026-11-20

Code `1cd6b732a100b8a01c302f2749b1d0bce2b4ee5e` on the same branch. MorphBinding documentation/diagnostic now follows shared kMaxMorphTargets, with regression covering all 52 fixed semantic channels, last palette slot and rejection just past the limit. No ABI/order changes. Renamed another reserved MSL `vertex` identifier in the preview-only mask producer.

KINGSTON validation: VIVID morph binding + real MotionWeave bridge 2/2 PASS (`tests-palette.log`); both single-mask and two-mask real GPU scenes PASS (`preview-mask-fixed.log`, `preview-multi-mask-fixed.log`), alpha-hole and solid-occlusion probes report identical scene/ID depths. 12 independent actors PASS (`preview-crowd.log`: 179676 morph pixels, 183285 animated pixels). Actual Creator skin/eye/groom smoke PASS (`creator.log`, `creator.ppm`). Inspected preview-motionweave.png: procedural deformation fixture visible on shadowed floor, NOT photoreal face quality. Imported Creator/optical sweeps/full backend parity NOT RUN. Build log build-palette-mask.log. CI NOT RUN.

Next: finish native Studio facial preview ownership using shared ModelAssetProvider, async CPU decode, frame read leases and fenced retirement; imported Creator and broader animation tests remain.

## Studio async provider completion — 2026-11-20

Code `4a719af8c1c5a97b381c768155e28fd858877903`, branch `integration/motionweave-studio-facial-stage184`. Studio facial preview now owns one shared ModelAssetProvider instead of a direct unique Model/upload/release loop. CPU decoding/preparation uses its worker queue; takePrepared permits facial rig validation BEFORE publishPrepared performs the existing ModelStagingTransaction. Fresh publication keys retain last-good until success. Replacement invalidates renderer caches and uses the cache's fenced retirement; a ModelReadLease spans Animator evaluation through Renderer3D rendering/endFrame. CPU source cancellation is separate from generation retirement. Project changes drain the owner and texture cache. Failed epoch/fence cleanup quarantines the stopped provider, preserving GPU safety (exceptional memory retention remains intentional). One Animator remains pose owner; no browser file writes, animation schema or 52-channel order changes. Public provider additions are source-compatible; existing load/publishReady unchanged.

Actual checks, all outputs on KINGSTON:
- `cmake --build .../native --target aiverse_studio aiverse_studio_facial_gpu_tests -j 8`: PASS (`build-final-studio.log`). Also built model_asset_provider_api, async_model_cpu_queue, model_image_transaction targets. Existing unused-lambda-capture warning in ModelResourceCache.h remains.
- `TMPDIR=.../tmp bash run-tests.sh --no-build --build-dir .../native --filter '^(studio_facial_(gpu|runtime|preview_binding)|model_asset_provider_api|render_model_image_transaction|render_async_model_cpu_queue)$' --ctest-jobs 1`: 6/6 PASS (`tests-final-studio.log`, report `native/test-reports/20261120-143957-3541`). Real Metal Studio GPU test verifies asynchronous glTF import, render read lease, bad reimport retaining last-good, superseded same-source request and safe replacement/clear.
- `PYTHONDONTWRITEBYTECODE=1 TMPDIR=.../tmp python3 -m unittest discover -s tools/studio -p 'test_*.py'`: 29/29 PASS (`studio-python-provider.log`). git diff --check PASS (line-ending conversion notices only).
- Real Creator imported OBJ/manifest/morph Metal smoke: PASS (`creator-import.log`, `creator-import.ppm`).
- Earlier failed native GPU-test setup attempts: build-studio-gpu-test.log had missing new Make target until explicit CMake regenerate; build-studio-gpu-test2.log lacked aiverseMain. Corrected entrypoint, regenerated and verified subsequent builds. No hidden passing claim for those attempts.

Cumulative scoped coverage: 30 DISTINCT native tests PASS across recorded runs (some repeated), 73 web tests PASS, 29 Python Studio contracts PASS, single/crowd/mask/Creator Metal cases PASS. This is not the full engine suite. CI not dispatched; Vulkan/GLES, complete animation roadmap, optical sweeps, production human art and WKWebView screenshot parity remain NOT VERIFIED. Source worktree has only local untracked dist/node_modules symlinks used for external builds/reused dependencies. No push or main checkout edits.

Next actionable work: integrate the latest VIVID documentation/portable fixture ancestry, then cross-platform/Studio UI visual acceptance when those hosts are available. The prior shared Studio provider gap is closed and covered by native tests.

## Final VIVID ancestry reconciliation — 2026-11-20

Merge `ef11263a5afec830c4522c5ed7d719af5fad01aa` includes VIVID `2b7c4f967309726562afc84c0672fdbfca9a4c29`. Only additional documentation changed relative to tested parent; the upstream portable BufferHandle fix was already included. One append-only vivid.md conflict resolved by keeping BOTH upstream historical Stage 7BA and our later actual validation entries, without silently treating upstream source-only claims as test passes. No code rebuild required for this documentation-only delta. `git merge-base --is-ancestor 2b7c4f... HEAD`: PASS. Aggregated latest per-test results from actual JUnit logs: 30 distinct native tests, all PASS, no unresolved failures. Tracked worktree clean; local dist/node_modules symlinks are intentionally untracked. No push, CI or main checkout mutation.

| Area | Current readiness |
|---|---|
| MotionWeave + latest available VIVID ancestry | Implemented and merged |
| 52 semantic / 64 Model palette CPU routing | Implemented; focused regression PASS |
| Single/crowd/masked Metal rendering and Creator | Implemented; recorded procedural/import fixtures PASS |
| Studio async import, last-good and frame leases | Implemented; real Metal lifecycle PASS |
| Complete cross-platform/photoreal/Studio UI parity | Partial; Vulkan/GLES, production artwork and WKWebView screenshot parity NOT VERIFIED |

Deliverables: `/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/native/apps/studio-native/AiverseStudioHTML5.app`, native `aiverse_vivid_creator`, logs and captures in the same external build root. Source remains the isolated integration worktree. Next: Windows/Linux/Vulkan/GLES builds on corresponding hosts and visual Studio acceptance; use this joint handoff rather than restarting from earlier source-only stage notes.
