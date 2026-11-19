---
name: aiverse-spectrafx
description: Author and integrate AIVERSE SpectraFX visual effects: versioned graphs, asset variants, ECS source components, event bindings, CPU fallback, editor previews and eventual GPU backends.
---

# SpectraFX — universal AIVERSE visual effects

**Canonical documentation:** `docs/spectrafx.md`.  
**Exact stage history:** `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`.  
**Engineering rules:** `AGENTS.md`.
**Latest handoff:** `docs/agent-handoffs/spectrafx-stage113-visual-gpu-profile-2026-11-20.md`.

## Fast path (current supported capabilities)

1. Create `fx/graphs/<name>.vfx.json` with `version:1`, `nodes`, `links`, `outputs`, and optional `parameters`. Supported chain: `Spawn → Initialize → Update* → SpriteOutput|MeshOutput|RibbonOutput`. Node `properties` reuse `engine/fx/FxEffect.h` emitter fields. Example: `docs/spectrafx.md`.
2. In native C++ include `engine/fx/SpectraFX.h` and call `aiverse::spectra::GraphAssets(particles, reader).load(name, overrides)`. Play the returned effect ID with the existing CPU particle runtime; check the `errors` and `fallback` fields. Parameter overrides create independent variants.
3. For continuous ECS-attached effects include `<aiverselib/genre/SpectraFX.h>`. Add `spectra::SourceComponent` to any Entity (unit, actor, spell, vehicle, world prop). The renderer-side `spectra::PresentationSystem::sync` reads it and invokes the native project's render-pose resolver.
4. One-shot impact, fire, ability and death effects use `PresentationSystem::consumeTickEvents(tick, events, bindings)`, exactly once per committed tick. Call `reset()` when the client rolls back or starts a new session.
5. Preserve `aiverse_fx`, `fx/<name>.json`, `fx/fx.json`, legacy `RtsFx` and older `Vfx*` aliases until production parity tests pass.

## Boundaries and tests

VFX are **presentation-only**, never deterministic damage, collision authority, or gameplay rules. The v1 graph compiler produces `fx::EffectDef`, while the separate opt-in Metal GPU runtime already has evolution, sorting and compaction kernels. Full graph-programmable GPU simulation, renderer/editor integration, and complete cross-platform GPU QA remain unfinished. Do not duplicate SpectraFX by genre.

**Hard dependency rule:** engine/fx must not include any FPS, RTS, Editor or app module and must not accept a concrete genre camera. Use `SpectraFxView` and the generic `SpectraFxSceneHost` API instead; old camera APIs are source-compatible template adapters. The `spectrafx_architecture` CTest enforces direct source boundaries. Host builds are optional integration regressions, not requirements for using core.

Use CMake target `AIVERSE::spectrafx` in a native project. The optional
Metal `spectrafx_metal_rhi` test (Stage 98) compiles production
`GfxMetal.mm` and validates two offscreen views and per-frame resource
retirement. Do not claim this new test PASS before a hardware log exists.
`gfx::Device::readColorRGBA8` is **test/debug only**, never per-frame
FX runtime readback. Validate with:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

**Verification status:** these tests are committed, not proven passing. Never mark them successful based on source presence.

**Handoff requirement:** every stage needs a dated file in `docs/agent-handoffs/` and an update to `spectrafx-current.md` in the SAME branch, including code SHA, exact tests run/results, API/format contracts, risks and the next step. Do not claim a test PASS until the log exists.


## Multiple views (Stage 99)

Update a `SpectraFxSceneHost` only once per frame through
`prepareGpu(dt,firstView)` and `prepareCpu(dt,firstView)`.
Before **each** sequential camera render pass, call
`prepareRenderView(view)` *outside* the encoder; render the effect
inside that pass with `render(lighting,view)`. Each new camera may
replace the alpha sort index map: do not prepare multiple cameras up
front and then render them out of order. New API is committed, still
awaiting hardware acceptance; see Stage 99 handoff.


## Stage 100: delayed GPU feedback after live scene reset

GPU impact notifications are presentation-only. `HybridPlayback`
automatically invalidates emitter callback epochs on GPU retirement
(Stop/Kill/key replacement/rollback) without prematurely destroying
Metal storage referenced by in-flight commands. Never call teardown
`reset()` in the middle of a live GPU frame.
The third MetalGPU test `spectrafx_metal_rhi` covers deferred callback
invalidations and particle-generation rejection; run it before claiming
native acceptance. Stage 98–100 tests have NOT RUN as of this handoff.


## Stage 101: deterministic playback retirement

The desktop `spectrafx_retirement` CTest exercises the real
`SpectraFxHybridPlayback` resource accounting and deferred stop/reset
through a controlled fake Metal RHI, while the separate
`spectrafx_metal_rhi` checks actual Apple Metal GPU execution.
These newly authored tests have not been run. Keep tested SHAs separate
from Stage 97's verified 8/8 acceptance.


## Stage 102 — strict GPU evidence before release

When validating on a real Apple GPU, use
`AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 bash tools/spectrafx/acceptance.sh build/spectrafx-validation`.
Check the `metal_evidence` stage: `spectrafx_metal_gpu`,
`spectrafx_metal_billboard`, `spectrafx_metal_rhi` must each be
`PASS`; CTest skip code is not evidence of execution.
The Python parser tests are portable, but C++/GPU acceptance on
Stage 98–102 remains unverified until the Mac Studio run is supplied.


## Stage 104 — real Renderer3D integration QA

The macOS `spectrafx_metal_scene` test links production Renderer3D,
SpectraFxSceneHost and a real Metal RHI. It probes two-camera RT output,
foreground depth occlusion and live reset/retirement. Optional PPM frames:
`AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-scene`.
Strict `AIVERSE_ACCEPTANCE_REQUIRE_METAL=1` now requires 4/4 real
MetalGPU tests to PASS; the old 3/3 evidence applies to Stage 103 only.
Do not claim Stage 104 native acceptance before fresh Mac Studio logs.
