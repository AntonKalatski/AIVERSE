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


## Visual acceptance repairs — 2026-11-20

Implementation: `a16fbb965ad88e6d120592f6c0db5840d12d675b` on
`integration/motionweave-studio-facial-stage184`; starting HEAD
`9340e8c7ffa79db9d47e6a550653f3925a76bc73`.

**Correction of earlier status:** old Creator smoke PASS was insufficient visual
acceptance. Inspection of its actual screenshot showed only hair: CPU actor
borrowed a sparse library from a candidate subsequently moved into the live asset.
Floor/hair draw counts let that failure pass. This entry supersedes earlier claims
of visible Creator head acceptance, but not independent renderer parity fixtures.

Changes: CpuActorStream owns an immutable sparse-library snapshot, bounded with
its fixed vertex payload; source replacement is covered by the CPU actor test.
Creator can select compute before frame 1; `--cpu-morph` explicitly exercises the
fallback on Metal. Smoke requires head attachment AND actual tracked head commands,
logs real draw counts. Procedural facial parenting preserves world TRS rather than
applying head scale twice. Iris is ahead of sclera, inside cornea. High/compact iris
graphs use XYZ length: UV W=1 previously made all pupil/iris masks empty. Camera,
floor and a frontal authoring key reveal the bust and eye glints. Existing MSL
source-contract tests now match vertexIndex/threadIndex from the earlier compiler
fix. Validation script builds CPU actor tests and captures CPU fallback.

Paths: engine/character/vivid/{CpuActorStream.h,HumanPreview.cpp,HumanMaterials.cpp},
tests/vivid/{cpu_actor.cpp,human_appearance.cpp,human_creator.cpp},
tools/vivid-creator/{main.cpp,README.md}, tools/vivid-validate.sh, docs/vivid.md.
Public call signatures, DNA/save format, 52 semantic channels and 64 Model palette
unchanged; CPU actor now owns its morph snapshot (initialization-only copy). No
second scheduler, authoritative ECS edits, renderer shader changes or default
experimental batching/ICB changes.

Actually executed (all build/log/image artifacts under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid`):
- CMake builds Creator, CPU actor, human appearance and human creator tests: PASS.
  Final logs build-iris-radius.log, build-creator-contract.log, build-creator-light.log.
- Rebuilt aiverse_vivid_renderer_preview and aiverse_studio: PASS,
  build-visual-deliverables.log.
- `TMPDIR=.../tmp bash run-tests.sh --no-build --build-dir .../native --filter
  '^vivid_(cpu_actor|human_appearance|human_rig|human_creator|motionweave_bridge)$'
  --ctest-jobs 1`: **5/5 PASS**, tests-visual-accepted.log,
  native/test-reports/20261120-145600-39306. CPU lifetime test uses fake GLES device;
  real fallback capture below runs on Metal, not native GLES hardware.
- Final Creator `--smoke`: GPU, --cpu-morph, imported OBJ manifest, seven frozen
  optical cases, mobile low config and medium materials: **12/12 PASS**.
  Actual head command counts are 24 procedural / 1 imported. Files/logs in
  visual-acceptance-final/. creator-gpu.png visually inspected: head, facial parts,
  coloured iris and pupils, torso and shadow present; crude procedural test art.
- optical_ab.py with original ROIs, threshold=4, min-changed=1: **3/3 PASS**.
  Skin .10/4.0: 56233 changed pixels; eye IOR 1.25/1.45: 44; hair .08/.90: 2313.
  JSON/logs in visual-acceptance-final/{skin-scatter,eye-ior,hair-rough}.*.
  These prove parameter response in this scene, not physical accuracy.
- Real Metal MotionWeave/VIVID preview: capture-pass=1; morph 81989 changed pixels,
  MotionWeave 63241; both same-frame colour/ID/native depth probes PASS, delta=0.
  visual-acceptance-final/motionweave.log and three PPM captures; animated PNG
  inspected (deliberately simple geometry fixture, not a realistic head).
- bash -n tools/vivid-validate.sh and git diff --check: PASS. Entire branch-fetching
  vivid-validate.sh NOT RUN because it switches to the separate upstream branch.

Recorded failed attempts: build-face-transforms.log initially lacked Animation.h
in appearance test, then fixed. tests-visual-fixes.log and
 tests-visual-fixes-final.log each failed human_creator's stale MSL string checks;
all corrected and rerun above. Original top-lit eye IOR A/B failed (max RGB delta1,
no pixels above threshold), including full viewport diagnostic; final frontal key
passes WITHOUT lowering threshold. Older images remain under visual-acceptance/
for provenance; use visual-acceptance-final/ for final lighting/material captures.

CI NOT DISPATCHED (no URL); no push or main checkout edits. Full engine suite,
Vulkan/GLES hardware, production artwork and Studio WKWebView interaction/screens
NOT VERIFIED. Native Mac UI was locked; user was asked to unlock, no reply received.
CLI framebuffer captures succeeded independently. No synthetic UI screenshot.
Next action: after Mac unlock, inspect Studio Facial/animation windows via CUA,
exercise controls and take UI screenshots; then cross-backend acceptance on real
hosts and production character assets. Do not label the whole VIVID roadmap done.


## Real MetaHuman DNA sample import — 2026-11-20

User explicitly requested a ready internet DNA character and screenshots, then
selected **actual MetaHuman .dna, add compatible import first**, rather than a
substitute glTF character. Code: `af940c85dbfa7967a836e6b300522e34f261b9c3` on
`integration/motionweave-studio-facial-stage184`; parent
`b6c17725d3fd97d24e005c960a5e60977f2bd8f1`.

Source: official Epic MetaHuman-DNA-Calibration revision
`8297ff1bd6f1692a6e221aa8395a66486e5a53c9`, data/dna_files/Ada.dna.
SHA256 `e764cfab938283231bcb3e5af1c7bcbec5154bab4804aafa1b5b7dc6e400ca5e`.
Internal DNA descriptor is "Archtype" (filename is Ada). SDK, original sample,
converted assets, builds and captures live ONLY under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/metahuman/`; not vendored.
SOURCE.json records source URL/revision/hash and local SDK license path.
The official repository supports legacy UE <=5.5 DNA, not 5.6+; current general
MetaHuman licensing and the older Calibration license are separate documents.
No rights to redistribute the SDK or sample with AIVERSE are asserted.

Changes: tools/vivid-dna-import/{CMakeLists.txt,decode.cpp,package.py,README.md,
test_package.py} implement a separate authoring adapter linked to an external
user-supplied official DNA reader. Decoder exports selected LOD geometry, all
source target names and optionally one exact target's deltas. Package creates
glTF + VIVID JSON/manifest, expands sparse deltas across UV/normal seams, records
preview center/scale and optional baked weight. No SDK/game runtime coupling.
Creator `--smoke --still` freezes MotionWeave and snaps facial weights for controlled
captures. docs/vivid.md and Creator README document partial support.

Actual native/sample checks:
- External SDK CMake + Apple Silicon Release reader/adapter: PASS,
  import-configure.log, import-build.log, import-build-final.log. SDK has ordinary
  compiler warnings and ranlib empty platform translation units; build succeeded.
- Creator rebuild: PASS, build-still.log. No new Studio build required; adapter is
  offline and --still is standalone Creator-only.
- Real Ada decode LOD0: **9 meshes, 34625 layout vertices**, head lists 681 source
  blendshape targets. Selected exact source target `brow_raiseIn_L` exported.
  Final rebuilt decoder output byte-compares equal to prior successful output.
- `PYTHONDONTWRITEBYTECODE=1 TMPDIR=.../tmp python3 -m unittest discover
  -s tools/vivid-dna-import -p 'test_*.py'`: **4/4 PASS**, package-tests-final.log.
  Cases: UV seam morph expansion, invalid layout rejected without publishing,
  baked+live double deformation rejected, existing output preserved.
- Reader negative tests: invalid binary signature, out-of-range LOD 99 and `0junk`
  all rejected without successful output files (bad-signature.log, bad-lod.log,
  partial-lod.log). Input capped at 512MiB and LOD layout vertices at 1M; this does
  not claim hardened adversarial parsing of the external SDK.
- Real Metal `aiverse_vivid_creator --smoke --still --asset <manifest>`:
  ada-neutral-final, ada-three-quarter, ada-brow-live and ada-brow-cpu
  (`--cpu-morph`) **4/4 capture PASS**; each has .log/.ppm, PNGs of first three.
  Actual accepted head provenance commands=9, opaque/shadow counts=5 after hiding
  untextured helper surfaces. Front and 3/4 PNGs visually inspected: recognizable
  human source geometry, not procedural spheres, no generated replacement wig.
- Explicit diagnostic live route: VIVID browInnerUp -> source brow_raiseIn_L, gain1;
  expressionRest[2]=1. This maps ONE left source target for test, not a faithful
  bilateral ARKit/RigLogic mapping. Neutral vs live: 200 changed viewport pixels
  at RGB-total threshold4. GPU vs CPU: one changed pixel of 586500 (max RGB sum8).
  **Strict bit-exact image equality FAILED**; bounded visual similarity only.
  Both actor paths rendered correctly. morph-parity.json preserves all counts.
- git diff --check PASS. Full engine/Studio/web suites NOT RUN (unchanged runtime
  systems outside Creator capture option); no CI/push, no main checkout changes.

Failed discovery commands: initial unquoted GitHub tree URL hit zsh glob expansion;
quoted retry succeeded. media.githubusercontent.com Ada URL returned 404 (file is
ordinary Git blob); raw.githubusercontent.com pinned URL succeeded. No fabricated
sample was substituted. Earlier first Ada render had opaque helper eye shells and
Creator-generated hair; final package keeps helper geometry but explicitly hides
untextured eyelash/eye-edge/tear/saliva surfaces and tags authored eyelashes so
Creator does not add its procedural wig. Final screenshots are untextured/clay
materials; they do not show original MetaHuman skin/iris textures or groom.

| Subsystem | Status | Remaining |
|---|---|---|
| Legacy binary DNA reader + geometry package | implemented in source; Ada Metal tested | Other DNA versions/axes, broader assets |
| One exact source morph, GPU/CPU routes | partial; Ada tested | Full corrective combinations, normals/tangents, semantic mapping |
| RigLogic, original skeleton and skin weights | not implemented in this adapter | Behavior/joint solver and full expression parity |
| Original textures, eye shading and groom | not implemented in this adapter | Obtain companion licensed assets; DNA alone lacks them |
| Newer UE DNA, Vulkan/GLES, full Studio UI | not tested | Matching SDK and backend/UI hosts |

No VIVID save schema, canonical 52-channel ordering, authoritative ECS state,
shared animation scheduler or GPU ABI changed. Next actionable step: expand the
explicit adapter to original skeleton/skin weights and RigLogic behavior with
real reference-pose tests; obtain companion texture/groom assets separately.
Do not call this complete MetaHuman runtime support or a production character.


## Full Ada companion-character capture — 2026-11-20

User requested a full character because the preceding DNA sample showed only the
head. Code `3699a445d1383fe7048f9fa8afe6443575b77272` on
`integration/motionweave-studio-facial-stage184`, parent
`25d60660c4f17089d8b5c68e57f8338fb7ca29fa`.

Discovery: Epic's old Ada.dna contains no full-body mesh. Its public data/body
folder contains skeleton Maya files only. Found separately published full Ada
export by CreateGameCharacters:
https://creategamecharacters.github.io/metahuman-to-glb/characters/ada/ada.glb
Repository https://github.com/creategamecharacters/metahuman-to-glb
SHA256 `70ad5d190e3cdfb54e88d405b75d7b4731547f2bd93d5cbad0ce4720946d99de`.
This is a separate UE 5.6 companion GLB, NOT a body generated from the previous
legacy DNA file. The user was told this distinction before and after import.

Changes: new tools/vivid-dna-import/prepare_full_character.cjs decodes Draco via
external pinned tooling (@gltf-transform/core + extensions 4.2.1, draco3dgltf 1.5.7),
preserves mesh/skin/texture data, assigns unique duplicate garment bone names and
selects the actual face skin head. A parent frame normalizes the whole figure,
with optional yaw. Published mh_materials.json provides uniform cloth colours,
roughness/metallic fallback because the GLB base-colour input is a UE RGB region
mask. This is not complete original UE cloth-detail shader reconstruction.
Authored hair tags prevent Creator from adding procedural hair. Documentation
updated in adapter README and docs/vivid.md. Creator's demo idle translation now
uses bind + bob instead of absolute zero, preserving nonzero imported head offsets.
No new scheduler, authoritative gameplay, DNA schema or shared renderer edits.

Actual checks (all artifacts in
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/):
- `cmake --build .../native --target aiverse_vivid_creator -j 8`: PASS, build.log.
- Node preparation front/quarter: PASS, prepare-final.log, prepare-quarter.log.
  node --check script and git diff --check PASS.
- Input/output asset structural checks: preserved **7 meshes, 15 primitives,
  5 skins with unchanged joint-count multiset, 16 texture images**, removed Draco
  requirement. Source has 119916 vertices; native skin-palette splitting gives
  125302 displayed vertices. Garments include shirt, slacks, flats, full body,
  face, eyebrow cards and hair cards.
- `aiverse_vivid_creator --smoke --still --asset <manifest>` final front and ¾
  GPU captures PASS; front with --cpu-morph PASS. Logs/images:
  ada-front-final.{log,ppm,png}, ada-quarter-final.{log,ppm,png},
  ada-cpu-final.{log,ppm}. Actual tracked head/model commands=47,
  shadow=41, colour=41, pass=1, pipelineFailures=0.
- Both final GPU PNGs visually inspected: full head-to-shoe character framed,
  hands/legs/garments/hair visible, body/face remain attached in rest pose.
  Earlier ada-front.png showed RGB clothing masks as red/green/purple colours;
  final uniform author-colour fallback fixes those diagnostic mask colours.
- SOURCE.json records URL, hash and companion-export distinction. SDK/sample and
  npm package-lock/dependencies remain external on KINGSTON, none vendored.

A read attempt at sparse checkout UnrealFest.zip failed because sparse checkout
excluded it; no asset depended on that archive. Full source and final snapshots
are retained externally for reproducibility. No source code from the external
pipeline scripts was executed; only downloaded data and standard pinned decoder
libraries were used by our preparation script.

| Subsystem | Status | Remaining |
|---|---|---|
| Full Ada companion export in VIVID | implemented in source; real Metal captures passed | Broader asset/backend parity |
| Imported head bind translation | implemented in source; full rest-pose capture verified | Comprehensive moving full-rig tests |
| Original UE materials / RigLogic | partial / not implemented | Cloth mask-detail, exact eye/hair shader and facial solver parity |
| Full body animation / other backends | not tested | Actual clips, skinning/retarget reference tests |

Full engine tests, Studio UI screenshots, Vulkan/GLES and complete MetaHuman
animation parity NOT RUN. No CI/push/main checkout changes. Next step: bring
actual body clips and validated facial routing into this full asset, then check
pose/retarget and material fidelity independently. Do not conflate this successful
full-body rest-pose import with full binary body-DNA/RigLogic support.


## Ada companion textures and hair alpha — 2026-11-20

User: "И текстуры". Implementation `e1dc2a63fe65f8519505ff62a4a3163492b24f86`,
branch `integration/motionweave-studio-facial-stage184`, parent
`6aa5aff8037a5a6e231eb2c6aca9346bb56d20ee`.

New bake_materials.cjs + test_bake_materials.cjs under tools/vivid-dna-import;
prepare_full_character.cjs accepts optional source-map directory after material
JSON. Companion textures were obtained from the same published Ada directory:
https://creategamecharacters.github.io/metahuman-to-glb/characters/ada/textures/
Read the published assets/viewer.js as a material-channel reference, did not
execute it. Existing 16 embedded images were copied directly where applicable;
extra maps downloaded via curl with normal certificate checking. Python urllib
attempts failed with local CERTIFICATE_VERIFY_FAILED; no TLS verification bypass.
External sharp@0.34.5 added to the KINGSTON tooling/package-lock.

Offline conversion: cloth RGB region masks now blend authored colours, available
micro/macro detail and AO, with linear-to-sRGB output and a mip-like prefilter for
highly tiled micro detail. Hair/brow/lash source red coverage becomes alpha in
neutral RGB images. Eye R/B iris masks + UV pole/radial ramps become ordinary
colour textures; original atlas transform is removed to avoid double zoom. Skin
cavity and companion teeth colour/normal/mouth-occlusion are attached. Final
package has 29 images and 13 conversion/attachment provenance records. All 16
original image payloads verified byte-identical in output. Source-map SHA256s in
full-character/texture-source-hashes.json. No image/model asset vendored.

HumanMaterials.cpp high/compact hair graphs now use sampled input alpha via
setAlpha rather than overwriting coverage with procedural RGB's constant alpha.
This applies to existing colour and shadow graph evaluation; no renderer edits.
Creator adds --portrait for close inspection of the normalized full-body asset.
README and docs/vivid.md updated. No DNA/save/API/channel ordering, gameplay,
shared scheduler, renderer GPU ABI or default experimental settings changed.

Actually executed, all under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid:
- Native Creator + human appearance target build PASS: full-character/build-textures.log.
- Studio rebuild PASS: full-character/build-studio-textures.log.
- Unified run-tests.sh --no-build --build-dir .../native --filter
  '^vivid_human_appearance$' --ctest-jobs 1: 1/1 PASS, including material quality
  budgets. Log full-character/test-textures.log; report
  native/test-reports/20261120-152153-72698.
- node test_bake_materials.cjs <tooling> <KINGSTON tmp>: PASS,
  full-character/test-bake-final.log. Synthetic fixtures deliberately separate R
  coverage from source A; check correct alpha, authored cloth colour/sRGB values,
  colour roundtrip and path traversal rejection. Syntax checks and diff --check PASS.
- Final prepare-textured-filtered.log PASS. Final asset is
  full-character/ada-textured-filtered/manifest.json. Earlier ada-textured-final
  has unfiltered microdetail and is NOT the preferred delivery.
- Real Metal --smoke --still --asset ... with default and --portrait:
  **2/2 PASS**, head/model commands=47, shadow=44, colour=44, pipelineFailures=0.
  full-character/ada-textures-final-{full,portrait}.{log,ppm,png}.
  Both final PNGs viewed: whole figure and close skin/eyes/hair visible, cloth
  moire removed. Intermediate unfiltered captures also succeeded technically but
  showed an aliased large check pattern; prefilter was added and recaptured.

Limitations: offline baked cloth at finite resolution, nearest source sampling;
no view-dependent iris refraction, procedural sclera veins, tangent-atlas hair
anisotropy or full UE skin/scattering parity. Specular glints/card edges remain
visible and are not presented as final production fidelity. Full engine suite,
Vulkan/GLES, complete facial/body rig animation and Studio UI visual parity NOT
RUN. No CI/push/main mutation. Source worktree only intentional untracked web
symlinks remain after commits.

Next: validate original UE reference lighting/materials against native results,
then integrate actual full-body clips and RigLogic-compatible facial routing.
Texture wiring is verified in native Metal captures; photoreal/UE parity is partial.


## Soft hair/lash coverage correction — 2026-11-20

User reported missing alpha on hair/eyelashes after the texture checkpoint.
Implementation: `1f8b8b2852328864aafe8f1a453c3be5defa0be4`, branch
`integration/motionweave-studio-facial-stage184`, parent `f7e932bfb2860c2ad2a7e9f7a6d05a04733c9421`.
Affected: tools/vivid-dna-import/{bake_materials.cjs,prepare_full_character.cjs,
test_bake_materials.cjs,README.md}, docs/vivid.md.

Correction to prior acceptance: PNG coverage packing and successful render
submission did NOT establish correct soft edges. The previous baker forced
MASK at .35 on both hair and lashes, discarding fractional coverage. Re-read
external viewer-reference.js: original uses blended lashes and blended hair
fringe with a depth-writing masked core. Baker now emits BLEND lashes (coverage
texture preferred over albedo), BLEND hair/brows and cloned MASK .5 core
primitives sharing glTF accessors/textures. Invalid coverage channels reject.
Preview groom roughness .55 follows reference viewer floor and reduces pin glints.
No native renderer, DNA schema, API/save/channel ordering, gameplay or scheduler
changes. Additional primitives increase native imported vertices 125302 ->159396;
interchange sharing does NOT currently imply zero additional native GPU memory.

All assets/logs/captures remain external under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/`.
Preferred asset supersedes ada-textured-filtered:
`ada-soft-alpha/manifest.json` (diagnostic-*.json are opaque controls, NOT delivery).
Actual verification:
- node --check baker and test: PASS; git diff --check: PASS.
- node tools/vivid-dna-import/test_bake_materials.cjs <external tooling> <KINGSTON tmp>:
  PASS, test-alpha.log. Fractional R=128 versus A=0 preserved, transparent zero,
  BLEND lash path, shared core accessors/textures + MASK .5, cloth conversion,
  unsafe path rejection. Synthetic test updated to exercise fractional coverage.
- prepare_full_character.cjs <tooling> ada-source.glb ada-soft-alpha 0
  mh_materials.json source-maps: PASS, prepare-soft-alpha.log.
- Existing native Creator --smoke --still --asset ada-soft-alpha/manifest.json,
  with/without --portrait: 2/2 PASS, commands49 shadow43 colour43,
  pipelineFailures0. ada-soft-alpha-{portrait,full}.{log,ppm,png}; both PNGs viewed.
- Separate all-hair/lash OPAQUE control and lash-only OPAQUE control: 2/2 PASS,
  ada-{alpha,lashes}-control-opaque.{log,ppm,png}. Both viewed. Fixed-camera PPM
  RGB comparison (>3/255 max-channel difference): all-opaque hair region
  [620,300,880,480) 24851 changed pixels; lash-only eye region
  [660,475,825,520) 335. Demonstrates coverage affects actual rendered pixels,
  NOT a UE visual parity metric. Opaque control exposes solid card strips.
- Native rebuild/CTest NOT RUN this stage: only offline JS/asset preparation
  changed, existing e1dc2a63 native binary used for actual Metal captures.
  Full suite, moving camera/animation sorting, Vulkan/GLES and Studio UI NOT RUN.
  No CI URL/run, push or main mutation.

Limits: primitive-sorted alpha blending is not order-independent transparency;
intersecting strand/pose sorting still needs multi-angle/animated validation.
Hair tangent-atlas anisotropy and original UE skin/eye lighting parity remain
unfinished; this is a soft-coverage correction, not a photorealism completion.
Next action: validate full-body clips and moving-camera/animated hair layers,
then refine original skin/eye and strand-lighting fidelity. Keep all builds and
artifacts on KINGSTON. Untracked web dist/node_modules symlinks left unchanged.
