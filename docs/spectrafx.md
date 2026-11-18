# AIVERSE SpectraFX

**SpectraFX** is the official name of the universal, genre-independent visual-effects system in AIVERSE. Its design is inspired by the authoring and event-driven workflow of Unreal Niagara, but it is **not** a copy of Unreal code, asset formats, or editor. `aiverse::spectra` is the public C++ API namespace.

> **Current scope (2026-11-18):** Graph v1 frontend, reusable CPU particle backend, graph-asset variants/hot reload and optional read-only ECS presentation adapter have been **committed**. A node-canvas editor, GPU particle simulation and automatic RTS/FPS production integration are **not finished**. Tests were added but have **not yet been run** in the authoring environment. Refer to `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` for exact SHA and verification status.

## Architecture

```text
Gameplay ECS (authoritative, deterministic)
  ├─ typed gameplay events ────────────────┐
  └─ data-only SpectraSourceComponent ──┐  │
                                       ▼  ▼
                               SpectraFX presentation
                               (read-only World + events)
                                       │
                          exposed graph parameters
                                       │
                graph assets fx/graphs/*.vfx.json
                                       │
                     compileGraph (versioned IR)
                                       │
                   existing fx::EffectDef schema
                                       │
                    CPU fx::ParticleSystem/RHI
                                       │
                         renderer + quality tiers

Future opt-in path: GPU simulation/backend + visual node editor
```

**No genre-dependent FX engines.** An RTS turret, FPS gun, spaceship engine or racing car all use the same source component and event presenter. The host game supplies only the pose resolver and maps its event IDs to effect assets. Graphics state and particle handles never enter deterministic gameplay state/checksums.

## Public C++ entry points

- `engine/fx/SpectraFX.h`: `aiverse::spectra::Runtime`, `Effect`, `Handle`, `GraphAssets`, `GraphLoadResult`, `compileGraph`.
- `libraries/include/aiverselib/genre/SpectraFX.h`: `aiverse::spectra::SourceComponent`, `EventBinding`, `PresentationSystem`.
- `AIVERSE::spectrafx`: CMake link target, alias of existing `aiverse_fx`; the legacy target remains valid for other agents and projects.

### Author a graph

Put this file at `fx/graphs/engine_fire.vfx.json` inside a project:

```json
{
  "version": 1,
  "parameters": {
    "color": [[0, "#ffe8a0ff"], [1, "#ff330000"]]
  },
  "nodes": [
    {"id": "spawn", "type": "Spawn", "properties": {"rate": 24, "duration": 1, "loop": true}},
    {"id": "initialize", "type": "Initialize", "properties": {"life": 0.6, "speed": 1.0, "size": 0.4}},
    {"id": "update", "type": "Update", "properties": {"color": {"$param": "color"}}},
    {"id": "output", "type": "SpriteOutput", "properties": {"texture": "builtin:smoke"}}
  ],
  "links": [
    {"from": "spawn", "to": "initialize"},
    {"from": "initialize", "to": "update"},
    {"from": "update", "to": "output"}
  ],
  "outputs": ["output"]
}
```

The v1 compiler accepts `Spawn → Initialize → Update* → SpriteOutput|MeshOutput|RibbonOutput` chains, with output fan-out but no multiple-input node merges. Every node has a `properties` object whose keys are validated against `engine/fx/FxEffect.h`. The module chain compiles to the existing `fx::EffectDef` — it does **not** execute arbitrary per-particle graph instructions.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::Runtime particles;
particles.init(device); // The native host owns gfx::Device.
particles.setFileReader(readGameContent);
aiverse::spectra::GraphAssets assets(particles, readGameContent);

// Converts a versioned graph asset into a CPU particle effect.
auto flame = assets.load("engine_fire");
if (flame.effect >= 0)
    particles.play(flame.effect, {0.0f, 1.0f, 0.0f});

// Overrides produce independent graph variants:
auto cold = assets.load("engine_fire",
    aiverse::Json::parse(R"({"color":[[0,"#aaddffff"],[1,"#0044ff00"]]})"));
```

Runtime loop: call `setView(camera, viewProj, width, height)`, `update(dt)`, then `render(fog)` after scene rendering while its pass remains open, as described in `engine/fx/ParticleSystem.h`.

### Opt-in native ECS presentation

```cpp
#include <aiverselib/genre/SpectraFX.h>
using namespace aiverse;

auto entity = world.create();
spectra::SourceComponent source;
source.effect = "graph:engine_fire:<hash>"; // Better: use GraphLoadResult::runtimeName.
source.killOnDetach = false;
world.add<spectra::SourceComponent>(entity, source);

spectra::PresentationSystem presenter(particles);
presenter.sync(world, [](const World& w, Entity e,
                         fx::float3& position, fx::float3& direction) {
    // Resolve the render transform using YOUR game's component types.
    // Do not hardcode FPS/RTS into SpectraFX.
    return gameResolveEffectPose(w, e, position, direction);
});

presenter.consumeTickEvents(tick, sim.events(),
    {{hitEventId, spectra::EventBinding{"impact_flash", 1_fx}}});
```

Use the actual `GraphLoadResult.runtimeName` for continuous graph assets after `GraphAssets::load`; the `fx/graphs` registry does not automatically initialize all graphs when the game starts. After rollback/new match, call `presenter.reset()` to discard outstanding presentation handles and reset the presented-tick cursor; GPU/CPU simulation state still belongs to the host.

### Compatibility

The new branded `Spectra*` types were introduced without changing the identity of the old `Vfx*` aliases. Existing `fx/<effect>.json`, `fx/fx.json`, `aiverse_fx` and `compileVfxGraph` remain available. Engine runtime files stay under `engine/fx` during migration. Do not remove compatibility aliases or change persistent binary save identifiers until native project adoption and regression tests pass.

## Validation checklist

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

The current tests focus on graph compilation, asset caching/validation and read-only cross-genre ECS presentation. Visual regression screenshots, Android/Metal GPU performance, editor/MCP graph UX and FPS/RTS production event parity are still mandatory gates for a full Niagara-like release.

## Planned work

1. Graph asset CRUD via the existing editor/MCP workspace, graph preview, visual node canvas and exposed-parameter inspector.
2. A GPU particle backend for supported RHI devices (compute, spawn/update/compact, culling, sorting, ribbons, material data), with an explicit CPU fallback for GLES3/mobile/WebGL.
3. Rich graph dataflow, event handlers, function modules, simulation stages, user parameters, renderer plugins, soft particles, depth collisions, advanced quality/scalability.
4. Migration of existing RTS/FPS presentation adapters into a single SpectraFX path, native project sample games, performance and platform QA.

**Mandatory for agents:** after each bounded stage, append a separate handoff commit containing code SHA, affected files, API/serialization contracts, tests actually run, limitations and exact next task. See `AGENTS.md`.
