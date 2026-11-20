# SpectraFX Stage 96 — fix Metal half4 vertex output compile (2026-11-19)

**Branch:** `feature/spectrafx-generic-cue-queue`
**Code commit:** `7f77b26fa7e6092ed970d3cf8d2d08c764527761`
**Changed code:** `engine/fx/SpectraFxGpuBillboardRenderer.h`

## Failure evidence

User Mac Studio native full suite reported:

```
112/122 Test #113: spectrafx_metal_billboard ... ***Failed 1.24 sec
FAIL [spectrafx-metal-billboard] MSL:
program_source:28:13: error: no matching constructor for
initialization of 'half4' (vector of 4 'half' values)
o.color=half4(u.colorSize.rgb,p.w);
```

The runtime MSL vertex shader attempted to construct `half4` from
`float3` and `float`. Metal on the tested toolchain did not implicitly
narrow this combination, so the exact runtime shader failed compilation
before the existing offscreen alpha/additive pixel assertions could execute.

## Change and compatibility

Replaced that expression with explicit per-component narrowing:

```metal
o.color=half4(half(u.colorSize.r),half(u.colorSize.g),
              half(u.colorSize.b),half(p.w));
```

This uses the exact runtime MSL string also consumed by
`tests/spectrafx_metal_billboard.mm`. No test-only shader substitute.
No simulation logic, camera contracts, FPS/RTS/Editor dependency,
resource lifetime policy, JSON, save-state or C++ public API changed.
GPU interpolation remains `half4` as before; intended shader semantics
are unchanged apart from explicit numeric conversion.

## Actual verification

- Re-read latest branch file and exact failing MSL source.
- Changed one uniquely matched shader expression.
- Created and fast-forward-pushed the code commit via GitHub.
- **NOT YET RUN after this patch:** MSL compilation, native Clang build,
  Metal offscreen framebuffer draw, any CTest or full acceptance.
- The above **FAIL** is a user-provided log from *before* the code fix.
  Do not convert it into PASS merely because the source was patched.

## Next validation (first focused, then full suite when ready)

In a **clean** detached validation worktree:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue
cmake -S . -B build/spectrafx-validation -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/spectrafx-validation --parallel 4 --target aiverse_spectrafx_metal_billboard_test
ctest --test-dir build/spectrafx-validation -R '^spectrafx_metal_billboard$' -V --output-on-failure
```

If focused Metal billboard compiles and passes, run the existing full
`bash tools/spectrafx/acceptance.sh build/spectrafx-validation`
without editing tests to mask failures. Record source SHA, command logs,
pixel assertions and summary; subsequent RHI scene-pass, Editor Preview,
GPU lifetime and other stage 94/95 tests remain unverified.

**Next agent:** examine any second Metal compiler error (including
fragment `half4` if the toolchain rejects its all-`half` constructor),
fix against native diagnostics, then commit a new handoff.

## User-reported post-fix verification — PASS

User subsequently supplied the native focused CTest result on Mac Studio:

```
The following tests passed:
    spectrafx_metal_billboard

100% tests passed, 0 tests failed out of 1

Label Time Summary:
MetalGPU = 1.47 sec*proc (1 test)
SpectraFX = 1.47 sec*proc (1 test)
```

**Status change:** the focused `spectrafx_metal_billboard` test has now
**PASSED 1/1** (0 failures, 1.47 sec) after the explicit component-narrowing
source fix was pushed. The provided excerpt does not contain the checkout
HEAD SHA, compilation command output or per-pixel checks, so the tested
binary's exact source revision and detailed assertions cannot be
independently reconstructed from this excerpt. Do not call the entire
122-test suite passed on this basis.

**Outstanding:** one-shot full acceptance on latest source with
`bash tools/spectrafx/acceptance.sh build/spectrafx-validation`, including
genre-independent Stage 94/95 changes, combined Metal tests, all host builds,
and all native CTests; later on-device Renderer3D/Editor Preview, cue playback,
resource-lifetime and three-frames-in-flight stress.
