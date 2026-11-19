# SpectraFX — handoff for the next implementation agent

## Stage 105 — full Renderer3D native acceptance **PASS** (M3 Ultra)

User-provided hardware run at validated SHA
`c4fc948535557f1cc1a00459d1a3cd48257ad6ca`:
**126/126 full CTests PASS**, **11/11 SpectraFX PASS**, **4/4 actual MetalGPU
PASS**, strict metal evidence `SKIP=0 MISSING=0 FAIL=0`.
All **9/9 full acceptance stages exit 0**; no failed/skipped stages.
Real RHI: A pixels left/right 229/0; B 0/228, 24 churn frames,
retire_remaining=0, gpu_healthy=1, impact_queued=1 discarded=1 delivered=0.
Renderer3D real scene: FX-delta pixels A=168 centroid=63.50,
B=304 centroid=43.07, depth_center_delta=0, failures=0.
Five PPM files were saved on user Mac but **not visually reviewed here**.
Full evidence and next limitations:
`docs/agent-handoffs/spectrafx-stage105-full-renderer3d-acceptance-2026-11-20.md`.


## Stage 104 — real Renderer3D two-camera Metal scene acceptance (NOT RUN)

Code commits `4e7539d298076a3944bf837e570e0cd420e26275`,
`4518f775451c32af36a8db8fdd21e95a2fbf433a`. New production
`Renderer3D` + `SpectraFxSceneHost` test `spectrafx_metal_scene`
renders baseline/FX in two independent camera RTs, verifies opaque
depth occlusion, checks camera-dependent screen displacement and
post-`resetScene()` removal + deferred GPU retirement.
Optional five PPM captures for real-device visual QA.
Strict Metal evidence now requires **4/4 hardware PASS**, not old 3/3.
**Native compilation, screenshots and GPU test results are NOT yet known
for Stage 104**. Last proven hardware acceptance is Stage 103's older SHA
`fceb8caef1bf26085063b55c224631a3c53a9407`.
Handoff: `docs/agent-handoffs/spectrafx-stage104-renderer3d-metal-2026-11-19.md`.


## Stage 103 — strict full native acceptance 9/9 PASS on Apple M3 Ultra

User supplied the acceptance report for validated SHA
`fceb8caef1bf26085063b55c224631a3c53a9407`:
**configure, SpectraFX core, architecture, host integration, full build,
full CTest, focused SpectraFX, Metal CTest and Metal evidence all exit 0**.
Failed stages 0, skipped stages 0. All 3 real Metal GPU tests PASS:
`spectrafx_metal_gpu`, `spectrafx_metal_billboard`,
`spectrafx_metal_rhi`; `PASS=3/3 SKIP=0 MISSING=0 FAIL=0`.
The exact full-CTest count is not in this excerpt.

The next work is Renderer3D/Editor live visual QA, real multi-RT camera parity
and high-volume resource/fault/performance validation, **not rerunning
the already-green Stage 98–102 acceptance without new changes**.

Handoff: `docs/agent-handoffs/spectrafx-stage103-full-native-acceptance-2026-11-19.md`.


## Stage 102 — distinguish 3/3 native Metal PASS from SKIPPED (parser PASS)

**Code:** `a6b8617d472b104e660a254d2ed228abc4c81905`,
`e711c2a955e54a12c6b3aa8136724744cc99726a`.
New `metal_evidence` acceptance stage parses verbose CTest for
`spectrafx_metal_gpu`, `spectrafx_metal_billboard` and
`spectrafx_metal_rhi`. `AIVERSE_ACCEPTANCE_REQUIRE_METAL=1`
fails the acceptance if any device test is SKIPPED, FAILED or MISSING.
Hermetic Python parser unit tests: **4/4 PASS in local container**.
**Not yet run:** Stage 98–102 native C++/Metal/CTest on the current SHA.
See `docs/agent-handoffs/spectrafx-stage102-metal-evidence-2026-11-19.md`.


## Stage 101 — deterministic production HybridPlayback retire/reset test (NOT RUN)

Code `50df70bd39cf8059c54e75389c81a1c6eb40a306` adds
`spectrafx_retirement` CPU-side test with a controlled Metal-capable
fake RHI exercising **production** keyed replacement, Stop,
`resetPresentation` in-encoder rejection, three-serial deferred GPU
release, 32 cycles of churn and exact resource-handle teardown.
Standalone real MetalGPU fixture remains `spectrafx_metal_rhi`.
**Stages 98–101 not yet compiled/native-tested**; last known green
full acceptance Stage 97 only.
Handoff: `docs/agent-handoffs/spectrafx-stage101-hybrid-retirement-2026-11-19.md`.


## Stage 100 — GPU impact callback lifetime (NOT RUN)

**Code:** `dcd0d38481561c5e5fc85cb1e7881893e5d9ba69` and
`a232c4535d221ff684ca81435bedeb97f35d1b36`.
Real Metal RHI fixture now checks async impact readback invalidation after
scene reset and rejects impacts for a recycled particle generation.
Production `HybridPlayback` calls
`SpectraFxHybridEmitter::invalidateGpuImpacts()` immediately when GPU
resources retire, while retaining the buffers for the 3-slot Metal
retirement policy. No gameplay or genre dependencies.
**Stage 98–100 native tests have NOT RUN**. Do not reuse the Stage 97
8/8 all-green result for these changes.
See `docs/agent-handoffs/spectrafx-stage100-impact-retirement-2026-11-19.md`.


## Stage 99 — multi-camera rendering without multiple simulation steps

Added `SpectraFxSceneHost::prepareRenderView(const SpectraFxView&)` and
corresponding HybridPlayback/Emitter methods: host updates GPU and CPU
**once per frame**, then prepares/sorts each camera independently
between offscreen render passes. No FPS/RTS/Editor dependency. Code
commit `8f9777c17df9c4b22d2104927ef949a925ede6b3`.
**NOT RUN:** Stage 98/99 native tests, Metal offscreen, full acceptance.
Read `docs/agent-handoffs/spectrafx-stage99-multiview-2026-11-19.md`
for API ordering and remaining limits.


## Stage 98 — real Metal RHI multi-view + retirement test authored (NOT RUN)

**Code SHAs:** `59605eea446f0c26533ea839b6074b7729d3d73f`,
`96954451dd1bd317079098bfe2ca02b823fa89ed`,
`fe530ceb33fd0edcb1c7c2b52113c838178bbce8`.
Added `spectrafx_metal_rhi` CTest with actual `GfxMetal.mm`,
offscreen RGBA8 readback, two independent camera views, compute→draw
and 24 frames of 3-serial GPU lifetime pressure. New optional
`gfx::Device::readColorRGBA8` is **diagnostic-only**; other backends
remain unchanged. **Stage 98 has NOT been compiled or tested on Mac**.
The Stage 97 all-green run was for the earlier SHA.
Next step: focused `aiverse_spectrafx_metal_rhi_test` build/CTest,
then one full `tools/spectrafx/acceptance.sh` and record logs.
Read `docs/agent-handoffs/spectrafx-stage98-metal-rhi-2026-11-19.md`.


## Stage 97 — full native acceptance PASS on Apple M3 Ultra

**Validated source:** `4e363db3a6fc0d1631a574165febb7d26e08c3b8`.
User-provided `acceptance.sh` summary: configure, core, architecture,
Editor/FPS/RTS hosts, full build, full CTest, focused SpectraFX and real
Metal GPU acceptance all exit **0**; **failed stages 0, skipped stages 0**.
Real Metal compute and billboard tests are **2/2 PASS**, including
alpha direct/remap 163/163 and additive red 76/152. Full CTest count is
not in the excerpt. End-to-end Renderer3D / Editor viewport visuals and
GPU resource-lifetime stress still need validation.

Full evidence/next stage:
`docs/agent-handoffs/spectrafx-stage97-native-acceptance-2026-11-19.md`.


## Stage 96 — Metal half4 compile failure: focused 1/1 PASS

User's Mac Studio full CTest previously reported `spectrafx_metal_billboard` FAILED
(`112/122`, test #113) with `half4(float3,float)` MSL constructor error.
Code commit `7f77b26fa7e6092ed970d3cf8d2d08c764527761` explicitly
narrows each billboard vertex color component to `half`. This is the
runtime shader used by the exact Metal offscreen raster test.
**Post-fix user-reported result:** `spectrafx_metal_billboard` PASSED 1/1,
0 failed, 1.47 sec. The excerpt does not identify the exact tested HEAD SHA;
the full 122-test suite and visual RHI/Editor acceptance are not yet confirmed.
Full details and revalidation commands:
`docs/agent-handoffs/spectrafx-stage96-metal-half4-2026-11-19.md`.


## Stage 95 — rolled/overhead camera basis (unverified)

`ParticleSystem` now derives billboards from the caller's `SpectraFxView.up`
rather than a forced world-Y vector. `spectrafx_contract` includes a
genre-neutral vertical-camera and invalid-view fixture. The code is committed
on this branch; native compilation/CTest/GPU visual validation for this SHA
**HAVE NOT RUN**. See
`docs/agent-handoffs/spectrafx-stage95-arbitrary-camera-2026-11-19.md`.
The final one-command acceptance is still required.


## Stage 94 — no genre/editor camera dependency

The current branch now contains `SpectraFxView`: an explicit, camera-independent
eye/basis/projection/viewport packet for the FX core. `ParticleSystem` and
`SpectraFxSceneHost` no longer explicitly mention `RTSCamera`.
Previous camera-shaped APIs are preserved as templates.
Core compilation, host integration and full-build acceptance are distinct stages.
A new `spectrafx_architecture` CTest rejects reintroduced direct genre/Editor
source dependencies. **Native tests for Stage 94 have NOT RUN**.

See `docs/agent-handoffs/spectrafx-stage94-genre-neutral-camera-2026-11-19.md`
for exact implementation SHAs, API, test limitations, and next steps.


Updated: 2026-11-19. Branch: `feature/spectrafx-generic-cue-queue`.
Repository: `chromealex/AIVERSE`.
Status: **native validation in progress; CPU/header/host gates confirmed,
Metal GPU work pending**. User-provided Mac Studio logs confirm:
- `aiverse_spectrafx_header_smoke`, `aiverse_editor`,
  `aiverse_fps_client` and `aiverse_client` **built/linked** successfully
- `ctest -L SpectraFX` **4/4 PASS**, 0 failures, 1.51 seconds:
  `vfx_graph_compiler`, `vfx_ecs_presentation`,
  `spectrafx_contract`, `vfx_graph_assets`
- Metal Evolution, Alpha Sorting and Compaction compute **PASS** on
  actual Apple M3 Ultra; Editor GPU Preview, real GPU billboard draw,
  GPU resource lifetime and visual fallback have **not** been verified.

A new macOS-only **real Metal GPU compute test**
`aiverse_spectrafx_metal_gpu_test` was added in stage 90.
It compiles and executes the exact runtime evolution, alpha sorting
and compaction MSL shaders and verifies the GPU-generated data.
It **PASSED 1/1 on an Apple M3 Ultra** (user-provided Mac Studio log:
CMake configured, C++/Objective-C++ compiled, Metal libraries compiled,
compute dispatched, GPU results verified, 1.63 seconds test, 0 failures).
Together with the 4/4 earlier CPU/header contracts, this confirms five
separate native tests; the five have not yet been re-run as one suite
on the latest commit.
The new GitHub Actions run `44578278295` was queued at last observation:
https://github.com/chromealex/AIVERSE/actions/runs/44578278295

Important cross-agent warning: `main` is now >300 commits ahead of
the feature base while other agents work. Keep the validated detached
worktree isolated; do not merge `main` into it or into this feature
branch until runtime acceptance and conflict review.
Do not claim release readiness or merge to `main` without validation.
## One-shot acceptance gate (Stage 93)

Use `bash tools/spectrafx/acceptance.sh build/spectrafx-validation`
from the project's existing detached Mac Studio SpectraFX validation
worktree. It runs **configure, all required target builds, full native
build, complete CTest suite, focused SpectraFX CTests and real Metal
compute/billboard CTests in one invocation**, with per-stage logs and
`build/spectrafx-validation/spectrafx-acceptance/summary.md`.

The same script is now invoked by `.github/workflows/spectrafx-validation.yml`
on Linux and macOS, with errors-as-warnings and acceptance artifacts
uploaded from every run. The first full GitHub run was **44582216785**
(queued when last checked):
https://github.com/chromealex/AIVERSE/actions/runs/44582216785

**Do not claim full acceptance PASSED** without the new run's actual
results. Hosted macOS may skip Metal if no GPU is available. This
does not replace a live Editor Preview/FPS screenshot and frame-retire
stress on the user's Apple M3 Ultra.

## Latest GPU rendering gate (Stage 92)

The separate macOS `aiverse_spectrafx_metal_billboard_test` was added
with CMake and CI in Stage 92. It uses the actual SpectraFX MSL
billboard shaders and direct offscreen Metal rasterization to verify
alpha, additive blending, and sorted-index remapping. **NOT YET RUN**,
so only the earlier `spectrafx_metal_gpu` compute test is a proven
real GPU PASS. On the user's Apple M3 Ultra validation worktree:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue
cmake -S . -B build/spectrafx-validation -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/spectrafx-validation --parallel 4 --target aiverse_spectrafx_metal_gpu_test aiverse_spectrafx_metal_billboard_test
ctest --test-dir build/spectrafx-validation -L MetalGPU -V --output-on-failure
```

Expected registered MetalGPU test count after the refresh: **2**.
Still remaining after raw Metal billboard pass: actual RHI Renderer3D
integration, Editor Preview on-device, GPU callback/lifetime/stress.
Do not merge to main without those checks.

## Where to start

1. `docs/agent-handoffs/spectrafx-2026-11-19.md` is the canonical,
   per-stage chronological handoff (very long). The latest heading is the
   most recent stage. Append another stage **after every implementation
   commit**, with the actual commit SHA, branch, modified files, APIs,
   functional behavior, limitations, tests actually performed, and next
   actionable step.
2. `docs/spectrafx-gpu-runtime.md` is the project-facing guide to
   `fx/gpu/*.gpu.json` and `fx/gpu/routes.json`, current runtime
   limitations, genre host frame ordering and Editor preview.
3. For native source:
   - `engine/fx/SpectraFxHybridPlayback.h`: manages routed keyed and
     one-shot instances, CPU fallback, Metal GPU pools, total 65536 GPU
     slot budget, retained resources and presentation diagnostics.
   - `engine/fx/SpectraFxHybridEmitter.h`: CPU/GPU backend selection,
     quality/LOD/spawn scheduler, GPU recovery and retained buffer lifetime.
   - `engine/fx/SpectraFxGpuSharedArena.h`: one homogeneous additive
     material batch, up to 64 sources sharing GPU particle state.
   - `engine/fx/SpectraFxGpuEvolution.h` +
     `SpectraFxComputeShaders.h`: persistent GPU particle state,
     optional world-Y ground collision and impact feedback.
   - `SpectraFxGpuBillboardRenderer.h`, `SpectraFxGpuAlphaSort.h`,
     `SpectraFxGpuCompaction.h`: GPU billboards, Metal-only sorted alpha,
     Metal-only GPU-author-written indexed indirect draw. Alpha sorting
     and compaction both use three frame scratch slots. They cannot be
     used simultaneously on one emitter.
   - `SpectraFxGpuImpactFeedback.h`: Metal-only asynchronous GPU
     impact readback (<=4096 particle records per emitter);
     weak callback ownership, epoch invalidation and generation checks.
   - `SpectraFxGpuEmitterAsset.h`, `SpectraFxGpuEmitterLibrary.h`,
     `SpectraFxGpuRoutes.h`: strict project-owned JSON assets/routes.
   - `SpectraFxCueInbox.h`, `SpectraFxCueBridge.h`,
     `SpectraFxSceneHost.h`: genre-independent presentation API.
   - `apps/client/ClientFx.cpp`: existing opt-in RTS bridge.
   - `apps/fps/client.cpp`: opt-in local muzzle-fire cue ID 1, with
     floating-origin reset. `modules/fps/CMakeLists.txt` links
     `aiverse_fx` into FPS client only.
   - `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`: GPU route and
     asset visual authoring, safe save/backups, unsaved-JSON offscreen
     preview with pause/step/backend diagnostics.
   - `apps/editor/windows/SpectraFxGraphWindow.cpp`: separately owns
     existing canonical CPU graph editing; don't duplicate its solver.

## Must-preserve boundaries

- The name **SpectraFX** covers game-genre-neutral presentation only.
  PRISMA owns postprocessing. PHYSIS owns environmental phenomena;
  NEREID/TerraWeave own water/terrain. Do not write gameplay authority,
  networking or ECS state from GPU particle callbacks.
- All GPU commands and state-mutating buffer operations must be encoded
  on the RHI owner thread and outside a render/compute encoder;
  renderGpu must run in an already open matching scene pass.
- `gfx::Device` is borrowed; destroy all SpectraFX resources before
  Device teardown. Live scene/floating-origin reset must call
  `resetPresentation`, not teardown-only `reset`. Retired GPU
  allocations continue charging the per-playback slot budget.
- Metal is the only currently **enabled** GPU backend. The existing
  AIVERSE Vulkan RHI lacks implemented compute methods; Vulkan/GL
  deliberately use authored CPU fallback. Do not claim Vulkan GPU FX
  support or use `frameSerial` as a true portable GPU fence.
- A project without `fx/gpu/routes.json` must behave exactly as
  before. Default alpha requires CPU; GPU alpha sorting is opt-in
  (`gpuSortAlpha`, <=4096, Metal). Homogeneous pools require additive
  appearance, matching environment and explicit `sharedPool`.
- `FxQuality::Off` suppresses new GPU spawn but existing particles
  still expire. The adaptive governor uses **whole-frame** GPU time,
  not SpectraFX-exclusive GPU time.
- Source-only review has identified strict backend fallback semantics:
  Auto+allowCpuFallback=false must reject CPU (fixed in stage 82).
  Shared billboard init failure may use a private GPU renderer
  (fixed in stage 83).

Latest source correction: stage 85 prevents repeated Editor preview resets on invalid JSON; see the chronological handoff for its commit and limitations.

## Next implementation/validation

The next source review should prioritize remaining metal readback
callback/resource completion assumptions, the editor viewport frame
sequence and compatibility with current `gfx::Device` API.

**Hardware-only follow-up if CI lacks Metal:** now that the real M3 Ultra compute
kernel acceptance has PASSED, build and run the separate offscreen Metal
billboard raster/blend acceptance added after Stage 91.
Use `~/Projects/AIVERSE-spectrafx-validation` (detached worktree),
`git fetch origin && git switch --detach origin/feature/spectrafx-generic-cue-queue`,
`cmake -S . -B build/spectrafx-validation -DAIVERSE_BUILD_TESTS=ON`,
`cmake --build build/spectrafx-validation --parallel 4 --target aiverse_spectrafx_metal_gpu_test`,
then `ctest --test-dir build/spectrafx-validation -L MetalGPU -V --output-on-failure`.
The prior compute test has already passed. A future render test may report PASS,
FAIL with Metal compiler/GPU details, or SKIPPED (exit 77, no Metal device). This is a raw Metal compute acceptance
test **not** an integration test of Renderer3D/RHI draw or 3-frame
retirement. Save full logs and fix any errors before merging.

After this first validated native contract, continue with:
- CMake configure/build including `aiverse_editor`,
  `aiverse_fps_client`, and RTS native client
- C++ header smoke for all new `engine/fx/SpectraFx*.h`
- focused config/route/queue/pool/quality/strict fallback CTests
- Metal compile/actual render for GPU billboard, sort, compaction,
  collision, async feedback and preview
- CPU fallback on Vulkan/OpenGL, strict Auto and GPU policy
- FPS floating-origin transitions and RTS match reset
- delayed resource retirement and three frames in flight
- no unintended diff or duplicate feature ownership with
  PHYSIS/NEREID/TerraWeave/PRISMA
- warnings/ASan where available, and focused CPU/GPU perf measurements

Record real command outputs and failures in the chronological handoff;
do not merely state that tests were scheduled.

## Sync source

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
```

If your working tree is rebasing or has unmerged files, do not run
blind `git pull`: inspect `git status` and use a new worktree.
