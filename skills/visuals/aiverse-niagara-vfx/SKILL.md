---
name: aiverse-niagara-vfx
description: Universal Niagara-style VFX graph asset authoring, CPU compilation, ECS bindings and future GPU migration for all AIVERSE game genres.
---

# Universal VFX graph: AIVERSE Niagara-style pipeline

**Scope:** RTS, FPS, racing, RPG, space, native projects and hybrid games. Read `AGENTS.md` and `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` before code changes. After EVERY bounded implementation stage, commit a new handoff entry before starting the next stage.

## Current implementation (2026-11-18, unverified until CTest runs)

- `engine/fx/VfxGraph.h`: pure, header-only versioned graph JSON -> `fx::EffectDef` compiler. Nodes: `Spawn`, `Initialize`, `Update`, `SpriteOutput`, `MeshOutput`, `RibbonOutput`. Single-parent chains and fan-out; property parameter references, nested overlays; existing EffectDef parser validates output.
- `engine/fx/VfxGraphAssets.h`: load `fx/graphs/<name>.vfx.json` via game-supplied file reader, compile to `fx::ParticleSystem::define()`, cache source hashes, expose per-parameter variants, safe hot reload fallback.
- `libraries/include/aiverselib/genre/EcsVfxPresentation.h`: opt-in `VfxSourceComponent` and read-only `VfxPresentationSystem`, plus `VfxEventBinding` for one-shot `SimEvent` IDs; native project supplies entity pose resolver. No genre-specific includes. `reset()` on rollback or new match.
- CPU renderer: `engine/fx/{ParticleSystem,FxEmit,FxRender,FxInternal}.*`, draw batches and quality tiers. Legacy RTS mapping remains `modules/rts/fx/RtsFx.*`.
- **NOT YET**: GPU simulation, programmable dataflow per particle, Niagara renderer parity, node canvas editor, integrated MCP graph CRUD, FPS/RTS host migration. Do not describe those as implemented.

## Create a graph asset

Store `fx/graphs/engine_fire.vfx.json` in a game project's native content folder:

```json
{
  "version": 1,
  "parameters": {"color": [[0, "#ffcc66ff"], [1, "#ff220000"]]},
  "nodes": [
    {"id": "spawn", "type": "Spawn", "properties": {"rate": 24, "loop": true, "duration": 1}},
    {"id": "init", "type": "Initialize", "properties": {"life": 0.6, "speed": [0.2, 1.0], "size": 0.3}},
    {"id": "update", "type": "Update", "properties": {"color": {"$param": "color"}}},
    {"id": "out", "type": "SpriteOutput", "properties": {"texture": "builtin:smoke"}}
  ],
  "links": [
    {"from":"spawn","to":"init"},
    {"from":"init","to":"update"},
    {"from":"update","to":"out"}
  ],
  "outputs": ["out"]
}
```

Use `fx::VfxGraphAssets library(particles, readContentFile)`, then `library.load("engine_fire")`. The returned `effect` is an integer for `particles.play(effect, position)`; `runtimeName` is valid for `VfxSourceComponent.effect` after load. Override graph parameters with a JSON object in `load(name, overrides)`.

## Integration rules

1. Gameplay emits **typed canonical events**, weapon actions, impacts or status changes; it NEVER reads particle positions for authoritative gameplay. Read `docs/ecs-mechanic-composition.md`.
2. Present continuous, attached effects through `VfxSourceComponent` plus `VfxPresentationSystem::sync(world, hostPoseResolver)`. The native game provides render positions and directions; no shared library may hardcode player/unit/vehicle model behavior.
3. Present discrete muzzle flashes, projectile impacts and explosions through `VfxPresentationSystem::consumeTickEvents(tick, sim.events(), bindings)` **once per game tick**, not per graphics frame. Multiple render frames for one tick must not spawn duplicates. `reset()` after rollback or session restart.
4. Runtime VFX GPU/CPU scheduling and LOD are **presentation-side**; keep locks, queues and float time OUT of World snapshots/checksums.
5. Existing `fx/<name>.json` and `fx/fx.json` remain compatible; keep the old RTS bridge until parity tests for old versus new binding path pass.
6. Never silently claim the graph is a full Niagara-compatible importer. The new frontend does not parse Unreal uassets.

## Required validation

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -R '^vfx_(graph_compiler|ecs_presentation|graph_assets)$'
```

Once these pass, verify real macOS Metal, GLES3 Android and future GPU backend separately, plus FPS and RTS entity attach/detach and save/rollback events. A code inspection or committed test source is not a successful test run.

**Next agent:** review the handoff for exact commits, known issues, missing integration and test status; implement the editor graph preview/MCP CRUD before adding GPU compute nodes. Write a separate handoff commit after implementation.
