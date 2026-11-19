# SpectraFX — handoff for the next implementation agent

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

**Next test step (Metal render):** now that the real M3 Ultra compute
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
