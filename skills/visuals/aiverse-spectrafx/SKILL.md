---
name: aiverse-spectrafx
description: Author and integrate AIVERSE SpectraFX visual effects: versioned graphs, asset variants, ECS source components, event bindings, CPU fallback, editor previews and eventual GPU backends.
---

# SpectraFX — universal AIVERSE visual effects

**Canonical documentation:** `docs/spectrafx.md`.  
**Exact stage history:** `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`.  
**Engineering rules:** `AGENTS.md`.

## Fast path (current supported capabilities)

1. Create `fx/graphs/<name>.vfx.json` with `version:1`, `nodes`, `links`, `outputs`, and optional `parameters`. Supported chain: `Spawn → Initialize → Update* → SpriteOutput|MeshOutput|RibbonOutput`. Node `properties` reuse `engine/fx/FxEffect.h` emitter fields. Example: `docs/spectrafx.md`.
2. In native C++ include `engine/fx/SpectraFX.h` and call `aiverse::spectra::GraphAssets(particles, reader).load(name, overrides)`. Play the returned effect ID with the existing CPU particle runtime; check the `errors` and `fallback` fields. Parameter overrides create independent variants.
3. For continuous ECS-attached effects include `<aiverselib/genre/SpectraFX.h>`. Add `spectra::SourceComponent` to any Entity (unit, actor, spell, vehicle, world prop). The renderer-side `spectra::PresentationSystem::sync` reads it and invokes the native project's render-pose resolver.
4. One-shot impact, fire, ability and death effects use `PresentationSystem::consumeTickEvents(tick, events, bindings)`, exactly once per committed tick. Call `reset()` when the client rolls back or starts a new session.
5. Preserve `aiverse_fx`, `fx/<name>.json`, `fx/fx.json`, legacy `RtsFx` and older `Vfx*` aliases until production parity tests pass.

## Boundaries and tests

VFX are **presentation-only**, never deterministic damage, collision authority, or gameplay rules. The current graph compiler produces `fx::EffectDef`; it is not yet a GPU compute graph. Node canvas, graph MCP CRUD, GPU simulation, GPU collision and automatic FPS/RTS production wiring are **unfinished**. Avoid separate genre implementations.

Use CMake target `AIVERSE::spectrafx` in a native project. Validate with:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

**Verification status:** these tests are committed, not proven passing. Never mark them successful based on source presence.

**Handoff requirement:** every bounded implementation stage must end with a separate dated append to `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` in the SAME branch, including implementation/test commit SHA, exact tests executed and their outputs, API/format changes, risks and the next step.
