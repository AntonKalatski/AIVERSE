---
name: aiverse-spectrafx
description: Author and integrate AIVERSE SpectraFX visual effects: versioned graphs, asset variants, ECS source components, event bindings, CPU fallback, editor previews and eventual GPU backends.
---

# SpectraFX — universal AIVERSE visual effects

**Canonical documentation:** `docs/spectrafx.md`.  
**Exact stage history:** `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`.  
**Engineering rules:** `AGENTS.md`.
**Latest handoff:** `docs/agent-handoffs/spectrafx-stage94-genre-neutral-camera-2026-11-19.md`.

## Fast path (current supported capabilities)

1. Create `fx/graphs/<name>.vfx.json` with `version:1`, `nodes`, `links`, `outputs`, and optional `parameters`. Supported chain: `Spawn → Initialize → Update* → SpriteOutput|MeshOutput|RibbonOutput`. Node `properties` reuse `engine/fx/FxEffect.h` emitter fields. Example: `docs/spectrafx.md`.
2. In native C++ include `engine/fx/SpectraFX.h` and call `aiverse::spectra::GraphAssets(particles, reader).load(name, overrides)`. Play the returned effect ID with the existing CPU particle runtime; check the `errors` and `fallback` fields. Parameter overrides create independent variants.
3. For continuous ECS-attached effects include `<aiverselib/genre/SpectraFX.h>`. Add `spectra::SourceComponent` to any Entity (unit, actor, spell, vehicle, world prop). The renderer-side `spectra::PresentationSystem::sync` reads it and invokes the native project's render-pose resolver.
4. One-shot impact, fire, ability and death effects use `PresentationSystem::consumeTickEvents(tick, events, bindings)`, exactly once per committed tick. Call `reset()` when the client rolls back or starts a new session.
5. Preserve `aiverse_fx`, `fx/<name>.json`, `fx/fx.json`, legacy `RtsFx` and older `Vfx*` aliases until production parity tests pass.

## Boundaries and tests

VFX are **presentation-only**, never deterministic damage, collision authority, or gameplay rules. The v1 graph compiler produces `fx::EffectDef`, while the separate opt-in Metal GPU runtime already has evolution, sorting and compaction kernels. Full graph-programmable GPU simulation, renderer/editor integration, and complete cross-platform GPU QA remain unfinished. Do not duplicate SpectraFX by genre.

**Hard dependency rule:** engine/fx must not include any FPS, RTS, Editor or app module and must not accept a concrete genre camera. Use `SpectraFxView` and the generic `SpectraFxSceneHost` API instead; old camera APIs are source-compatible template adapters. The `spectrafx_architecture` CTest enforces direct source boundaries. Host builds are optional integration regressions, not requirements for using core.

Use CMake target `AIVERSE::spectrafx` in a native project. Validate with:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

**Verification status:** these tests are committed, not proven passing. Never mark them successful based on source presence.

**Handoff requirement:** every stage needs a dated file in `docs/agent-handoffs/` and an update to `spectrafx-current.md` in the SAME branch, including code SHA, exact tests run/results, API/format contracts, risks and the next step. Do not claim a test PASS until the log exists.
