---
name: aiverse-traversal-moves
description: Wall-run, wall-slide, wall jump, dash with air charges, grapple hook, glide and one-way platforms with drop-through over LocomotionModes.
---

# Traversal moves

Header: `<aiverselib/mechanics/TraversalMoves.h>`. Library catalog id `traversal-moves`, CMake target `AIVERSE::lib_traversal_moves`.
Capability area F1 P1 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Owner reports TraversalEnv each tick (grounded, wall contact/normal/id, wallRunnable tag, grapple ray hit, onOneWay) and the body velocity.
2. Call `stepTraversal(config, state, env, input, tick, dt)` after LocomotionModes; when `result.overrides` use `result.velocity` instead of the locomotion velocity.
3. One-way platforms: the collision owner asks `oneWaySolid(top, previousFeet, feet, vy, result.ignoreOneWay)` before resolving contacts.
4. An expired wall-run blocks the same wall until the body touches ground; a wall jump blocks it for `wallReattachTicks`.
5. Persist TraversalState with genre::save/load (kind 200) and bind `traversalConfigHash(config)`.

## Limits

No camera tilt or animation; prediction/reconciliation of these modes in engine/actor is not wired yet.
