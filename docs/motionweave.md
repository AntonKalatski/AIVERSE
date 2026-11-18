# AIVERSE MotionWeave

**MotionWeave** is the canonical name for AIVERSE's modular, genre-independent
animation subsystem: AnimationGraph, clips, blend trees, IK, Motion Matching,
Montages, root motion, ragdoll poses, inertialization and animation LOD/GPU crowds.

## Public API

Use the new CMake target and the branded public header in native projects:

```cmake
target_link_libraries(my_game PRIVATE AIVERSE::MotionWeave)
```

```cpp
#include <aiverselib/motionweave/MotionWeave.h>

// Separate client presentation ECS World; never Simulation::world().
aiverse::World presentation;
// Configure animation asset, player and output pose components first.
aiverse::motionweave::MotionWeave::update(presentation, frameDt);
```

This is the *same* implementation as the existing library, not a second engine:

| Recommended new symbol | Compatible existing symbol |
| --- | --- |
| `AIVERSE::MotionWeave` | `AIVERSE::animation_ecs` |
| `AIVERSE::motionweave` | `AIVERSE::animation_ecs` |
| `aiverse::motionweave` | `aiverse::lib::animation` |
| `MotionWeave::update(world,dt)` | `stepAnimationPresentation(world,dt)` |
| `<aiverselib/motionweave/MotionWeave.h>` | `<aiverselib/animation/EcsAnimationSystems.h>` |

All specialist libraries still have their historical
`<aiverselib/animation/...>` paths. Existing `render::AnimationGraph`,
`render::Animator` and `Pose` names are deliberately unchanged.

## Runtime design

**Presentation:** floats, AnimationGraph instances, IK and skinning reside in
a dedicated visual ECS World. FPS opts into `fps.visual.model`, configurable
Graph or baked crowd animation, preserving cube fallback. Existing RTS view
continues using its original animation path until migrated deliberately.

**Gameplay:** fixed-point, authored root motion enters the authoritative
simulation only through a collision-aware motor. Cosmetic AnimationGraph
and Montage notifies cannot trigger authoritative damage, reloads or hits.

**Authoring:** visual editor windows, Humanoid Avatar profiles, Ragdoll
descriptors, Montage and root-motion baking operations share Workspace/MCP.
Persisted asset schemas and editor IDs do not change with branding.

## Status and verification

New standalone test `tests/animation/motionweave_api.cpp` is registered as
`motionweave_api` (target `aiverse_motionweave_api_tests`).
**NOT RUN**: the user requested to defer tests, CMake builds and visual QA.
Naming changes do not imply the large animation PR is feature-complete.

Before further implementation read `AGENTS.md` and the latest entry in
`docs/agent-handoffs/animation-universal.md`. Every implementation stage
must be followed by its **own committed, append-only handoff**.
