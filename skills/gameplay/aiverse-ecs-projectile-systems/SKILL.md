---
name: aiverse-ecs-projectile-systems
description: Compose the same deterministic ECS projectile flight and impact pipeline in FPS, RTS, racing, RPG, vehicles or native projects. Genre-neutral components, systems, collision provider, ballistics policy, ordered events and rollback tests.
---

# ECS projectile flight and impact systems

Read `AGENTS.md`, `docs/ecs-mechanic-composition.md`,
`docs/genre-packages.md`, and
`skills/gameplay/aiverse-weapon-policies/SKILL.md`.

1. Include `<aiverselib/genre/EcsProjectileSystems.h>`. Use public
   `ProjectileFlightComponent` (`ProjectileConfig` + `ProjectileState`
   + generation-safe `owner` + stable `shotId`) on project Entities.
2. Register `addProjectileFlightSystem(sim,name,collisionProvider,onImpact)`
   from any GameModule. The collision provider returns a read-only
   `world::SpatialWorld`; no `RtsModule` / `fps::Module` dependency is
   allowed in the shared projectile stage.
3. The default `BallisticFlightPolicy` calls the existing
   `genre::stepProjectile`: fixed-point gravity, radius, lifetime and
   ricochet. A custom policy may add guided/rocket behavior, but it must
   return `ProjectileStep` and keep deterministic motion. Do not duplicate
   trajectory or contact math in genre adapters.
4. `ProjectileImpactEvent` is collected first, then delivered in canonical
   component dense order. Apply damage, status, explosions, material
   penetration and presentation events in separate ECS feature systems.
   Do not mutate the same projectile pool or allocate an Entity during the
   dense iteration; destruction is deferred by the flight system.
5. Keep launch/ammo/cooldown, flight, impact, damage and presentation as
   independent capability components and systems. Combine this flight with
   vehicle-mounted turrets, strategy artillery, shooter rifles, rockets or
   spells by providing settings and supporting components.
6. Verify native CMake linkage with `AIVERSE::lib_ecs_projectile_systems`
   (includes `AIVERSE::lib_weapon_policies` and `aiverse_world`), replay
   hash/rollback, old save formats, impact ordering and cross-genre reuse.
   `tests/ecs_projectile_composition_test.cpp` is the reference fixture.
7. This flight skill is **one part** of the complete common shooting pipeline.
   For weapon inputs/ammo/cooldown and deterministic deferred projectile creation,
   use `skills/gameplay/aiverse-ecs-weapon-launch/SKILL.md` and
   `<aiverselib/genre/EcsWeaponLaunchSystems.h>`. For health, impacts and
   ordered damage, use `skills/gameplay/aiverse-ecs-impact-damage/SKILL.md`
   and `<aiverselib/genre/EcsDamageSystems.h>`.
8. Run `ecs_projectile_composition_test` (flight-only),
   `ecs_cross_genre_projectile_test` (flight + health) and
   `ecs_cross_genre_weapon_pipeline_test` (full launch-to-damage)
   before considering genre-neutral mechanics reusable.
9. These tests compose the shared pipeline in independent host games; they
   do **not** imply the legacy RTS/FPS fire/damage stacks have been migrated.
   Document remaining adapters and native test status honestly.

## Physical versus kinematic projectiles

The public `EcsProjectileSystems.h` now also exports
`<aiverselib/genre/EcsSweepQuerySystems.h>`. Use
`selectSweptProjectileContact(candidates,origin,displacement,extent,bounds,preferTie)`
for dynamic-target collision of a moving projectile, with expanded AABB and
fixed-point nearest contact. A swept endpoint fraction of 1 is a valid hit;
tie policy is explicit (for example, smallest semantic Entity ID). Reuse this
primitive rather than reimplementing capsule/swept-box targeting in FPS,
RTS artillery, ships or ability systems.

The **real FPS programmed-projectile** update in
`modules/fps/FpsProjectiles.cpp` now calls this shared sweep selector. It
retains its existing terrain-wall blocking, penetration, on-hit effects and
spawn sequencing. The programmed projectile's core trajectory and damage
are still part of the compatibility code; do not claim full ownership
migration until save/rollback parity tests prove it.

For RTS shots that only need an abstract arrival or homing motion, use
`AIVERSE::lib_ecs_kinematic_projectiles` and
`EcsKinematicProjectileSystems.h` instead: no physical collision
world is required. For instant FPS or RTS beam hits, use the independent
`AIVERSE::lib_ecs_hitscan_resolution` system. All paths can share
impact/damage policies; **do not choose by genre name**.

The `ecs_projectile_composition_test` fixture now also exercises
the shared sweep selector with tie-breaking, expanded extents,
endpoint hits and no-hit cases. Actual native result remains unverified.

## Simple bouncing projectiles (physical, timed effects)

For grenades, thrown objects, physical mines and other projectiles whose fuse
and damage rules are managed elsewhere, reuse
`BounceMotionConfig` + `stepBouncingProjectile(position,velocity,collision,dt,config)`
in `EcsProjectileSystems.h`. The policy takes a read-only
`world::SpatialWorld`, applies fixed-point gravity before sweeping a
configurable box and handles reflection/contact inset/velocity divisor.
It **does not** expire or damage the entity. Its default (gravity=10,
half-extent=0.1, contact fraction inset=0.01, divisor=2) reproduces the
pre-ECS FPS grenade bounce arithmetic exactly.

The live FPS ordinary projectile stage is now registered as
`fps.compat.ordinaryProjectiles`, after
`fps.compat.projectilePrograms` and before `fps` (round/actor).
In `fps.ecsAuthority` mode it advances actual `ProjectileRuntime` ECS
components in saved `order,id` order, emits the original immediate hit
effects and rebuilds `State::projectiles` only as a compatibility projection.
With authority OFF, the *same* per-projectile solver advances the old State
vector. The native FPS regressions
`ecsOrdinaryProjectileAuthorityParity` and
`ecsProjectileProgramStageParity` cover both storage modes, mixed projectiles,
deep/COW snapshots and v9 cross-loading; their native results are not yet
verified. The cross-genre `ecs_projectile_composition_test` also compares
bouncing physics against the exact former FPS formula on multiple contacts.

This policy is **physical collision**. RTS hitscan or kinematic missiles do not
need `SpatialWorld`; use the matching nonphysical system. Do not combine
the two into one unconditional genre-based simulation.

When implementing another mechanic, repeat the same decomposition:
**components + reusable systems + pure policies + thin project adapters**.


## Built-in FPS direct runtime ownership (2026-11-18)

The live `fps.compat.projectilePrograms` stage in
`modules/fps/FpsModule.cpp` now advances programmed projectile flight
**directly on canonical ECS `ProjectileRuntime` components** when
`fps.ecsAuthority=true`. The source `Module::stepProgrammedProjectile`
in `modules/fps/FpsProjectiles.cpp` is shared with legacy
`State::projectiles` mode, and composes the existing projectile modifier
policy with `selectSweptProjectileContact`. No second or forked physical
solver is allowed. Source order is taken from runtime `order` and legacy
projectile ID, not incidental ECS dense order. Collision, damage,
on-hit/loot side effects and branching still happen synchronously while
the source projectile is stepped. ECS Entity destroys/spawns happen only
after the ordered loop, with stable `DynamicEntityIndex` keys.

The EOS compatibility view `State::projectiles` is still rebuilt from
`ProjectileRuntime` because unrelated grenade, weapon spawn and round
logic in the **following** main FPS tick need it. Those consumers may
still modify the vector and are reconciled by `fps.ecsMirror` (an
explicitly temporary two-stage ownership handoff). Future code should
migrate those consumers to direct ECS updates, then remove the writeback.
Do NOT turn the independently queued impact resolver on for built-in
FPS without proving identical same-tick effects.

Regression entrypoints `ecsProjectileProgramStageParity` and
`ecsProjectileBranchAuthorityParity` in `tests/fps/projectiles.inc`
are registered in `aiverse_fps_tests`; they cover COW/deep rollback,
direct `ProjectileRuntime` edits overriding stale State projections,
branch allocation, passive nonprogrammed projectiles, stable ECS
identities/orders, v9 cross-save and per-tick legacy parity. All remain
**unverified by native execution**; do not claim passes until compiled
and run.


## Native FPS projectile producer and round-reset cutover

The production FPS engine now uses one private `Module::spawnProjectile`
adapter for accepted weapon bullets and `Action::Frag` / `Smoke` /
`Flash` throwable projectiles. When `fps.ecsAuthority=true`, the helper
creates a full canonical `ProjectileRuntime` ECS Entity *at the same tick
and code location as the original spawn*, inserts the stable
`DynamicEntityIndex` kind/ID key and publishes its editor
`ProjectileEntity` projection. It also updates `State::projectiles`
only as a same-tick compatibility view for consumers that have not yet
migrated. Default FPS mode keeps the unchanged legacy vector path.

At bomb-round reset, `Module::clearProjectiles` retires all existing
`ProjectileRuntime` entities in source-ID order and clears projectile
index keys at the actual reset barrier, instead of deferring all ECS
cleanup until `fps.ecsMirror`. Loot/vehicle ECS indices are preserved.
Gameplay Entity allocator order can be affected by this earlier
retirement, so verify rollback and native multiplayer determinism.

Tests: `tests/fps/projectile_spawn.inc` exposes
`ecsProjectileSpawnAuthorityParity` (real accepted bullet and
Frag/Smoke/Flash input Commands) and
`ecsProjectileRoundResetParity` (real bomb-mode reset after grenade
spawn). Both are run by `aiverse_fps_tests`, compare legacy and ECS
authority simulation checksums, generation-safe Runtime/index
presence/order, save v9 cross-load, deep and COW snapshot replay and
post-reset projectile creation. These are **source-level fixtures not
yet observed to pass natively**.

Do not add new FPS projectile insertion directly to
`State::projectiles` in the authoritative path. New weapon/missile/
throwable producers should call the canonical spawn adapter or a
future shared `ProjectileSpawnQueue` integration after order parity.
Keep impact callbacks synchronous where the existing FPS/RTS game uses
same-tick damage, and do not conflate nonphysical RTS kinematics with
FPS physical grenade motion.
