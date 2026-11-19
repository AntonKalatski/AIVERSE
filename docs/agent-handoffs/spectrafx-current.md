# SpectraFX — handoff for the next implementation agent

Updated: 2026-11-19. Branch: `feature/spectrafx-generic-cue-queue`.
Repository: `chromealex/AIVERSE`.
Status: **feature implementation/source review only**. No native C++ builds,
CTest, editor execution, Metal GPU rendering, shader compilation or CPU/GPU
fallback verification were actually run for stages 1–84 in this branch.
Do not claim release readiness or merge to `main` without validation.

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

## Next implementation/validation

The next source review should prioritize remaining metal readback
callback/resource completion assumptions, the editor viewport frame
sequence and compatibility with current `gfx::Device` API.

Only **after feature scope has been closed**, validate:
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
