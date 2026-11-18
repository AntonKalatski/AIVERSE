---
name: aiverse-ecs-locomotion
description: Assemble shared deterministic character movement using ECS components over the existing stepMotion policy in any game genre.
---

# Universal ECS character locomotion

Read `AGENTS.md`, `docs/ecs-all-features-migration-plan.md`,
`docs/ecs-mechanic-composition.md` and
`skills/gameplay/aiverse-character-locomotion/SKILL.md` when present.

## State and systems
Include `<aiverselib/genre/EcsLocomotionSystems.h>` and link
`AIVERSE::lib_ecs_locomotion`. Attach these ECS components to any
controllable Entity:
- `LocomotionParametersComponent` (validated fixed-point MotionConfig)
- `LocomotionStateComponent` (authoritative MotionState)
- `LocomotionInputComponent` (from Command, bot or other input)
- `LocomotionOutputComponent` (velocity, jump event, validity, tick)

Register `addLocomotionPolicySystem(sim, "locomotion")` in the
deterministic schedule after input and ground/ceiling sampling. The system
calls the **existing** `lib::genre::stepMotion` algorithm and publishes
velocity and jump state. It never teleports or moves positions:
collision/environment sampling and position integration are separate shared
systems or project-supplied interfaces. Jump presses are consumed as edges;
movement input can be held. State, including buffered jumps and coyote timers,
lives only in ECS and must be serialized/hashed.

For climbing, mantle, ledges, ladders, swimming and sliding, extend this
adapter around `LocomotionModes.h` with additional optional components;
do not create genre-specific motors or duplicate its equations.

## Tests and host adoption
- `ecs_cross_genre_locomotion_test` checks unrelated shooter/strategy hosts,
  shared velocity math, jump edge, no hidden state, COW and rollback.
- Test collision and transform interactions, slope/ledge feedback,
  authoritative server reconciliation, network save/replay and native linkage
  before adopting the system in built-in games.
- **NOT MIGRATED YET:** the FPS `moveActor` and RTS `sysMovement` still
  own their legacy behavior. Replace each in small, guarded, exact-parity
  stages, preserving velocity/position/tick results and content schemas.
