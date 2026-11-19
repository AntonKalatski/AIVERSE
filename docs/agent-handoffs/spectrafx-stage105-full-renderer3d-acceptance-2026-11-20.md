# SpectraFX Stage 105 — full native Renderer3D acceptance evidence

Date recorded: 2026-11-20  
Branch: `feature/spectrafx-generic-cue-queue`  
**Validated code SHA:** `c4fc948535557f1cc1a00459d1a3cd48257ad6ca`  
Source changes this stage: **none** (documentation of real user-provided M3 Ultra execution).  
Previous stage: `docs/agent-handoffs/spectrafx-stage104-renderer3d-metal-2026-11-19.md`.

## Verified native hardware evidence

The user provided actual command output from Mac Studio (Darwin, Metal Apple M3 Ultra), working tree `/Users/aleksandrfeer/Projects/AIVERSE-spectrafx-validation`, build `build/spectrafx-validation`.

- `ctest_full`: **126/126 PASS**, 0 failed, 860.06 s wall time.
- `ctest_spectrafx`: **11/11 PASS**, 0 failed, 3.73 s wall time. Includes architecture, graph compiler/assets, ECS presentation, contract, Metal compute/billboard/RHI/Renderer3D scene, evidence parser and production HybridPlayback retirement test.
- `ctest_metal`: **4/4 PASS**, 0 failed, 1.00 s wall time.
- `metal_evidence`: **PASS=4/4, SKIP=0, MISSING=0, FAIL/UNKNOWN=0**.
- Full acceptance summary: `configure`, `spectrafx_core_build`, `architecture`, `host_integration_build`, `full_build`, `ctest_full`, `ctest_spectrafx`, `ctest_metal`, `metal_evidence` **all exit 0**. Failed stages 0; skipped stages 0.
- The summary explicitly records commit `c4fc948535557f1cc1a00459d1a3cd48257ad6ca` and platform Darwin. Do not transfer these results to any future untested source SHA.

### Metal RHI observations

```
[spectrafx-metal-rhi] device=Metal Apple M3 Ultra
[spectrafx-metal-rhi] A left/right=229/0 B left/right=0/228
[spectrafx-metal-rhi] churn_frames=24 retire_remaining=0 gpu_healthy=1
[spectrafx-metal-rhi] impact_queued=1 impact_discarded=1 delivered=0
PASS ... production RHI offscreen cameras, pixel parity, compute->draw and 3-frame resource retirement
```

This verifies actual gfx::Device offscreen GPU drawing, two viewports, bounded resource churn and generation-safe async impact callback rejection. The zero deliveries are expected for a deliberately invalidated/recycled impact.

### Production Renderer3D + SceneHost acceptance

```
[spectrafx-metal-scene] device=Metal Apple M3 Ultra
[spectrafx-metal-scene] A pixels=168 center=63.50 B pixels=304 center=43.07 depth_center_delta=0
[spectrafx-metal-scene] failures=0
PASS ... production Renderer3D depth, two camera RTs, SceneHost and reset retirement
```

The user-run test also wrote **five local screenshot evidence files**:
`/tmp/spectrafx-scene-baseline-a.ppm`,
`/tmp/spectrafx-scene-effect-a.ppm`,
`/tmp/spectrafx-scene-baseline-b.ppm`,
`/tmp/spectrafx-scene-effect-b.ppm`,
`/tmp/spectrafx-scene-after-reset-a.ppm`.

These PPMs were generated on the user's Mac Studio; the agent has **not received or inspected their pixels visually**. The native test's own automated pixel/depth assertions passed.

### Raw shader acceptance

```
[spectrafx-metal-gpu] compile evolution PASS
[spectrafx-metal-gpu] compile alpha-sort PASS
[spectrafx-metal-gpu] compile compaction PASS
[spectrafx-metal-billboard] MSL + alpha/additive pipelines PASS
[spectrafx-metal-billboard] alpha direct/remap=163/163 additive red 1x/2x=76/152
```

## Architecture/API and modified files

No engine source files or public API were modified in Stage 105; only this evidence document, the Stage 104 handoff addendum, and `spectrafx-current.md` were updated. Preserve the genre-independent presentation API and ownership:
- `SpectraFxView` and `SpectraFxSceneHost::prepareRenderView` for sequential multiview output without advancing simulation twice.
- `SpectraFxHybridPlayback::resetPresentation()` rather than immediate teardown `reset()` during a live scene.
- GPU readbacks are diagnostic-only except the bounded async impact queue.
- PRISMA owns postprocess; SpectraFX owns FX. No FPS/RTS or Editor logic belongs inside the generic FX runtime.

## Remaining limitations and next agent step

**Hardware acceptance is green for this SHA**, but do not declare the complete Niagara-equivalent production feature finished. Outstanding: visually inspect the five local PPMs; interactive Editor GPU Preview (orbit/pause/step/assets), PRISMA HDR/tonemapping/depth composition, multi-view GPU alpha-sort with distinct camera directions, extended production HybridPlayback stress/performance and memory/driver-resource measurements; Vulkan and GLES currently retain CPU fallback.

Next stage should prioritize a **real Editor Preview / PRISMA integrated visual fixture**, camera transitions and reproducible image comparisons without introducing game-specific code. Then evaluate high-load frame pacing and memory before merge review. The feature branch remains highly divergent from main; do not blindly merge main or overwrite the detached validation worktree.

Existing log path (user machine): `build/spectrafx-validation/spectrafx-acceptance/`; do not imply these logs are available to the agent unless attached.
