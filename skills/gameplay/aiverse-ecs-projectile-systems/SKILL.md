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
7. The example tests shared ballistic flight only; it does **not** establish
   that the entire legacy RTS/FPS fire/damage stack has been migrated.
   Document remaining adapters and native test status honestly.

When implementing another mechanic, repeat the same decomposition:
**components + reusable systems + pure policies + thin project adapters**.
