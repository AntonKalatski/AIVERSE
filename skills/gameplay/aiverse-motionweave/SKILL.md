---
name: aiverse-motionweave
description: Modular ECS animation system for AIVERSE native projects across FPS, RTS, RPG and cinematics.
---
# AIVERSE MotionWeave

Canonical public API:
- Header: `<aiverselib/motionweave/MotionWeave.h>`
- CMake: `AIVERSE::MotionWeave`
- C++ namespace: `aiverse::motionweave`
- Scheduler: `motionweave::MotionWeave::update(presentationWorld, dt)`

The animation World is **presentation-only**. NEVER add float graph state,
pose caches, model pointers, visual IK or cosmetic notifies to the
authoritative `Simulation::world()`. Deterministic root motion uses the
fixed-point motor adapter, not sampled presentation floats.

Existing `AnimationGraph`, `Animator`, `Pose` and special-purpose headers under
`aiverselib/animation/` are part of MotionWeave. Older consumers may
continue using `aiverse::lib::animation` and `AIVERSE::animation_ecs`.
Do not fork/duplicate the runtime for another genre.

Read `docs/motionweave.md`, `docs/animation-graphs.md`,
`docs/agent-handoffs/animation-universal.md` and `AGENTS.md`.
**After each code stage**, commit a separate dated handoff entry listing
SHA, changed files, API contracts, actual tests/NOT RUN, risks and next task.
