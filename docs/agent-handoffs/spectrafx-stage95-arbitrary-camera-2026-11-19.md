# SpectraFX Stage 95 — arbitrary camera orientation (2026-11-19)

**Branch:** `feature/spectrafx-generic-cue-queue`
**Implementation SHA:** `2415e08bb0ed3cc2290afd4a0f19cf9d3288797e`
**Previous architecture stage:** `docs/agent-handoffs/spectrafx-stage94-genre-neutral-camera-2026-11-19.md`

## Implemented

- `engine/fx/ParticleSystem.cpp`: CPU particle billboard orientation now uses the supplied `SpectraFxView.up`, instead of assuming world-Y as camera-up. This matters for arbitrary camera roll, top-down orthographic views, cutscenes and a camera looking straight down. Camera forward/up are transformed into the optional particle-local coordinate space before deriving the normalized right/up basis. The direct camera-neutral path and old template camera adapter remain intact.
- `tests/spectrafx_contract_test.cpp`: source-level assertions were added for a straight-down camera packet with non-world-Y up, right/up orientation via `mat4::lookAt`, and invalid viewport/projection input rejection. These checks are part of the existing `spectrafx_contract` CTest; they are not a genre fixture.
- `docs/spectrafx-gpu-runtime.md`: corrected the outdated blanket "not native-tested" statement: the Mac Studio M3 Ultra Metal compute kernel test passed on older SHA, while actual Metal billboard raster, RHI integration and lifetime visual QA remain unverified.

## Compatibility and ownership

No FPS/RTS/Editor includes or game-specific VFX were added. Generic `SpectraFxView` remains the primary input. Existing engine scene cameras still work through `SpectraFxView::fromCamera` / templated overloads. No JSON, save format or gameplay ECS layout changed. This is purely render-side presentation basis math.

## Verification performed

GitHub source was fetched, exact anchor substitutions and branch writes were successful. Native compilation, CTest, `spectrafx_architecture`, Metal framebuffer probes and visual comparison were **NOT RUN for Stage 95**. The prior older-SHA 4/4 CPU SpectraFX tests and 1/1 Metal compute PASS must not be attributed to these new changes.

## Acceptance and next step

After feature work is complete, run `bash tools/spectrafx/acceptance.sh build/spectrafx-validation` from the detached SpectraFX validation worktree after `git fetch origin && git switch --detach origin/feature/spectrafx-generic-cue-queue` (only with a clean worktree). Check `spectrafx_contract`, `spectrafx_architecture`, both Metal GPU tests, all-host builds, entire CTest, warnings-as-errors and acceptance logs. Visually compare rolled, vertical and orthographic cameras with sprite/camera-stretch effects; this unit test does not prove billboard visual parity. Keep the branch separate from concurrently changing `main` until real acceptance and merge/conflict review.

**Resume order:** Stage 94 and 95 handoffs → inspect actual fresh acceptance failures → fix native/Metal issues without genre coupling → validate RHI/Editor preview and resource retirement → update handoff with observed commands and outcomes.
