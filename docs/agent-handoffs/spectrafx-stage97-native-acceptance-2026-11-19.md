# SpectraFX — Stage 97: Apple M3 Ultra full native acceptance PASS (2026-11-19)

## Source / handoff context
- **Validated source SHA:** `4e363db3a6fc0d1631a574165febb7d26e08c3b8`.
- **Branch:** `feature/spectrafx-generic-cue-queue`, validated from the separate detached `~/Projects/AIVERSE-spectrafx-validation` worktree; do not merge other agents' worktrees.
- **Verification evidence:** user-provided terminal output from `tools/spectrafx/acceptance.sh` on **Darwin / Apple M3 Ultra**.
- **This stage's change:** documentation only; no C++/shader/tool logic changes.

## Exactly reported acceptance results

The user provided the generated acceptance summary for build directory
`/Users/aleksandrfeer/Projects/AIVERSE-spectrafx-validation/build/spectrafx-validation`:

| Step | Exit |
| --- | ---: |
| configure | 0 |
| spectrafx_core_build | 0 |
| architecture | 0 |
| host_integration_build | 0 |
| full_build | 0 |
| ctest_full | 0 |
| ctest_spectrafx | 0 |
| ctest_metal | 0 |

**Failed stages: 0; skipped stages: 0.** The complete CTest stage exited 0
according to the acceptance script. Its individual test count is **not shown**
in the pasted excerpt (earlier failed attempt mentioned 122; don't assume
the exact passing count without `ctest_full.log`). The targeted SpectraFX
CTest stage also exited 0. This is strong native evidence for the Stage
94/95 genre-independent camera change plus Stage 96 Metal half4 fix,
at the exact validated SHA above.

### Real GPU checks in the same run

```
[spectrafx-metal-gpu] device=Apple M3 Ultra
PASS [spectrafx-metal-gpu] compile evolution
PASS [spectrafx-metal-gpu] compile alpha-sort
PASS [spectrafx-metal-gpu] compile compaction
PASS [spectrafx-metal-gpu] evolution, alpha sorting, compaction
1/2 spectrafx_metal_gpu Passed 0.10 sec

[spectrafx-metal-billboard] device=Apple M3 Ultra
PASS [spectrafx-metal-billboard] MSL + alpha/additive pipelines
[spectrafx-metal-billboard] alpha direct/remap=163/163 additive red 1x/2x=76/152
PASS [spectrafx-metal-billboard] alpha/additive draw, GPU index remap and offscreen readback
2/2 spectrafx_metal_billboard Passed 0.12 sec

100% tests passed, 0 tests failed out of 2
MetalGPU = 0.22 sec*proc (2 tests)
SpectraFX = 0.22 sec*proc (2 tests)
Total Test time (real) = 0.22 sec
[spectrafx] PASS ctest_metal
```

The exact runtime MSL string compiled and the dedicated offscreen
raster/alpha/additive validation passed. This is **raw Metal shader
acceptance**, not proof of end-to-end Renderer3D or Editor viewport visuals.

## Status / boundaries

- **Confirmed PASS:** full native configure/build/CTest gates and 2/2
  actual Apple M3 Ultra MetalGPU tests on validated SHA, with no skipped
  acceptance stages. The automated dependency-boundary gate passed.
- **Not yet verified by this output:** Editor GPU Preview screenshots,
  live game FPS/RTS event-to-effects visuals, offscreen render-to-texture
  integration, multi-camera and camera-roll rendering parity,
  Metal frame retirement under repeated emitter churn, async callback
  faults, resource leaks, frame pacing/throughput, Vulkan/GLES visuals.
- Preserve genre-independent core (`SpectraFxView`,
  `SpectraFxSceneHost`) and keep PRISMA / PHYSIS / NEREID / TerraWeave
  separate. Do not merge to `main` without conflict review and the
  remaining intended visual/stress gates.

## Next agent action

Implement/run a focused **real RHI scene-pass integration and lifecycle
stress** harness to exercise the actual SpectraFX `gfx::Device` draw,
`Renderer3D` render-to-texture, multi-camera views, scene reset and 3
in-flight Metal frames; capture pixel assertions and long-lived repeated
spawn/remove pressure. Validate Editor Preview and native hosts visually.
Record tests and exact SHA after each stage. Do not reopen already-passing
raw MSL work unless a new regression is observed.

For the existing validated commands, from a clean worktree:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue
bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

Logs from the reported run:
`build/spectrafx-validation/spectrafx-acceptance/{ctest_full,ctest_spectrafx,ctest_metal,architecture,full_build,host_integration_build}.log`
and `summary.md`.
