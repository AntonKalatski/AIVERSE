# AIVERSE — universal ECS mechanic composition

**Normative engineering specification (2026-11-18).** The engine is not a bundle
of isolated RTS, FPS, RPG and racing implementations. A game is a configurable
assembly of ECS **components** (state) and **systems** (behavior). A "genre" is
a preset of systems, settings and presentation, never an eligibility gate for a
mechanic.

Read with `AGENTS.md` and
`skills/workflow/aiverse-new-mechanic/SKILL.md`. This document overrides
older instructions saying every new mechanic belongs in
`modules/rts/Rts<Mechanic>.cpp` or requires `RtsModule&`.

## Required shape

| Artifact | Responsibility | Contract |
| --- | --- | --- |
| Component(s) | Authoritative, persistent data | Data-only; fixed-point and integer values; stable Entity references; deterministic serialization/checksum; no world or renderer pointers |
| Policy/algorithm | Reusable calculation | Pure (or explicitly provided RNG/collision inputs); no dependency on genre modules, graphics, OS, Lua, or global state |
| ECS system(s) | Feature/stage execution | Explicit read/write/resource and structural access; deterministic order; stages and barriers; skip Entities missing required components |
| Configuration | Feature enablement and tuning | Optional validated rules; stable defaults; game can combine the feature with anything else |
| Event/command adapter | Effects on other mechanics | Typed events or deterministic deferred commands, never hidden cross-system mutations |
| Host adapter | Backward compatibility | Small bridge from legacy RTS/FPS save/net/editor APIs; no duplicated core algorithm |
| Tests and docs | Proof of reuse | Two independent GameModules, combined-systems fixture, stable checksums, rollback, save/reload, editor/native consumer and performance |

Component definitions and systems must live in a shared
`engine/` or `libraries/include/aiverselib/` API (depending on ownership).
Projects can register systems through public targets, without linking
`aiverse_game` or `aiverse_fps` merely to call a common mechanic.
`AIVERSE::libraries` and `AIVERSE::gameplay_native` are the existing
public CMake entry points. For a simple generic adapter, see
`libraries/include/aiverselib/genre/EcsMechanicSystems.h`.

A system must **not** assume an Entity is a soldier or a player. It should
depend on `Position`, `Velocity`, `Health`, `WeaponState`, etc., and on
capabilities expressed by the presence of those components. Components such
as `Parent/LocalTransform/WorldTransform` are also available to every genre.

## Reference pattern: shooting in any game

The same shared flight system must serve a first-person rifle, an RTS artillery
piece, a turret on a racing car, a homing missile, and a magic projectile:

```text
Input / AI / trigger
  -> WeaponLaunchSystem
      Reads: WeaponConfig, WeaponState, Transform, Team, Target
      Writes: WeaponState
      Outputs: deferred SpawnProjectile(command)
  -> ProjectileFlightSystem
      Reads: ProjectileFlight, Target, Position, CollisionWorld
      Writes: ProjectileMotion, Position
      Outputs: ordered ImpactEvent
  -> ImpactResolutionSystem
      Reads: ImpactEvent, DamagePayload, Armor, Health, Team
      Writes: Health and status effects
      Outputs: ordered Hit/Kill/Explosion events
  -> DespawnAndFxSystem
      Consumes: hit/expiry events
      Writes: deferred entity destruction; presentation events only
```

**Components:** `ProjectileFlight` (speed, gravity, turn rate, lifespan,
bounce/penetration, curve/behavior), `ProjectileMotion` (position, velocity,
age, traveled distance), `DamagePayload` (amount, type, radius, status),
`ProjectileOwner` (`Entity` + team/faction), `ProjectileTarget` (optional
generation-safe `Entity` or position). Use data flags/policies for
ballistic, hitscan, rocket/homing, splash and magic effects. Never
`if (genre == FPS)` inside the shared motion algorithm.

Implemented cross-genre ECS flight adapter:
`libraries/include/aiverselib/genre/EcsProjectileSystems.h`. It exposes
`ProjectileFlightComponent`, a pluggable `BallisticFlightPolicy`, the
`addProjectileFlightSystem` ECS system and canonical `ProjectileImpactEvent`
records. Register it through the public
`AIVERSE::lib_ecs_projectile_systems` catalog target. Test:
`tests/ecs_projectile_composition_test.cpp` (one system in a shooter host
and a strategy host with matching COW rollback checksums). **This is a flight
stage; legacy RTS/FPS shooting/damage adapters are not yet migrated.**

### Shared ECS impact and damage stage

`libraries/include/aiverselib/genre/EcsDamageSystems.h` builds on the same
`ProjectileFlightComponent` and `ProjectileImpactEvent` rather than making
separate shooter/artillery damage solvers.

- `HitPointsComponent`: authoritative health; any vehicle, character,
  structure, boss or destructible can opt in.
- `ProjectileDamageComponent`: direct damage associated with a shot Entity.
- `ColliderEntityIndex`: stable collision ID → generation-safe ECS target.
- `DamageQueue`: ordered damage requests, serialized with the ECS World.
- `addDamageResolutionSystem`: deterministic apply stage, with an optional
  policy for project-specific armor, resistance, factions and immunities.

Native host registration, for either RTS-like or FPS-like gameplay:

```cpp
#include <aiverselib/genre/EcsDamageSystems.h>
namespace lg = aiverse::lib::genre;

// In setup(): create projectile and target entities with their components,
// register the ColliderEntityIndex and DamageQueue world resources.
lg::addProjectileFlightSystem(sim, "projectile-flight",
    [&collision](aiverse::Simulation&) -> const aiverse::world::SpatialWorld& {
        return collision;
    },
    [](aiverse::Simulation& s, const lg::ProjectileImpactEvent& hit) {
        lg::enqueueProjectileDamage(s, hit);
    });

lg::addDamageResolutionSystem(sim, "apply-damage",
    [](aiverse::Simulation& s, const lg::DamageApplied& damage) {
        // Emit a deterministic gameplay event / score / effect command.
    });
```

The order is **flight → impact queue → damage resolution → deferred
despawn/presentation**. No genre checks, no floating-point gameplay arithmetic,
no mutable cross-thread event emission. New requests enqueued by damage
callbacks are processed at the next resolution barrier.

The public native target is `AIVERSE::lib_ecs_impact_damage`, which depends
on `AIVERSE::lib_ecs_projectile_systems`. Verify with
`ecs_cross_genre_projectile_test` (shooter/artillery hosts, identical damage,
COW and replay) and `ecs_projectile_composition_test` (flight-only).

**Limits:** only direct projectile damage is implemented by default. Status
effects, area explosions, penetration, armor and team policies are separate
composable systems or host-supplied policies. Existing `fps::Module::shoot`
and RTS combat still require explicit parity-checked migration before
switching to this shared pipeline; these classes are not automatically
replaced by merely registering the library.

Existing reusable building blocks:
`engine/sim/ProjectileData.h` (projectile programs),
`libraries/include/aiverselib/genre/Weapons.h` (trajectory, recoil, spread),
`engine/world/SpatialWorld.h` (a collision adapter). Extract adapters
incrementally; do not pretend that existing `fps::Module::shoot` and
`RtsModule` combat implementations are already one system.

**Ordering is part of behavior:** command intake; deterministic spawn IDs;
parallel read-only planning; canonical collision/hit ordering (tick, entity,
source ID); damage application; deferred structural changes; then effects.
Consume random samples on the deterministic commit phase, not in worker order.
Preserve old save versions and replay hashes through adapters until migration
parity is proven.

## Composition rules

- A weapon may be mounted on a vehicle, structure, actor or autonomous drone.
  A single Entity may have multiple optional components; it can simultaneously
  drive, shoot, carry inventory, craft, interact and use abilities.
- Avoid an ever-growing giant `Actor` or `State` with features guarded by
  genre flags. Put optional state in feature components. Host modules assemble
  enabled systems and bind project-specific configuration.
- Each component has one authoritative owner. A cached view, Inspector
  projection, rendered transform or network packet may **mirror** but must not
  silently become a second writable source of truth.
- Systems declare component/resource reads and writes and their tick stage.
  Independent work may use ECS Jobs. Structural changes, shared RNG, events and
  order-sensitive damage must be applied in deterministic barriers.
- Missing components mean "not participating", not an invalid genre. All
  systems remain opt-in, and disabling a mechanic does not change old games.
- A library function is reusable only if it accepts neutral state, parameters
  and dependency callbacks or a neutral interface; an API requiring
  `game::RtsModule&` or `fps::Module&` is **an adapter**, not a reusable core.

## Native C++ usage

For project-owned components:

```cpp
struct TurretWeapon {
    Fixed cooldown;
    Entity target;
};

aiverse::lib::genre::addEcsMechanicSystem<TurretWeapon>(
    sim, "weapon.recharge",
    [](Simulation& sim, Entity entity, TurretWeapon& weapon) {
        (void)entity;
        weapon.cooldown=fxMax(Fixed{},weapon.cooldown-sim.dt());
    });
```

In a real feature, use shared `WeaponState` and launch policies rather than
creating a second turret-specific cooldown implementation. The example only
demonstrates generic system registration. The shared projectile-flight system must be bound to **both** FPS and RTS
host adapters; authoritative weapon launch and damage apply still need conversion.

## Acceptance gate for every new or migrated feature

1. Compile a public-header-only test/consumer, *without linking either
   `aiverse_game` or `aiverse_fps`* for the pure mechanic.
2. Run the **same ECS system** in at least two distinct GameModules and on
   different component sets. Include a mixed scenario (e.g. vehicle + weapon,
   strategy unit + projectile, inventory + crafting).
3. Test missing components, invalid/cyclic Entity references, insertion,
   destruction, cleanup and deterministic ordering across 0/1/N workers.
4. Verify save/load, snapshot rollback, replay hash, delayed network input
   correction (when relevant) and no new RNG-order dependency.
5. Confirm feature settings and native C++ registration work with old project
   formats. UI/Inspector should discover component metadata without
   hard-coded genre-specific field names.
6. Run normal CTest, ASan/UBSan, optional TSan, headless RTS/FPS examples and
   relevant render tests when applicable. Record actual results, not merely
   test code added to Git.
7. Update `AGENTS.md` if the invariant changes, the mechanic's skill,
   this document, and the active PR's handoff **after every implementation
   step**.

Current cross-genre proof fixture:
`tests/ecs_cross_genre_mechanics_test.cpp` (same motion, recoil and capture
policies in two independent hosts). This demonstrates the pattern only once
its test is compiled and passes; it does *not* establish that all current
RTS/FPS mechanics comply.

## Migration status, do not overclaim

| Area | Current reality | Work still needed |
| --- | --- | --- |
| ECS/Parent/job infrastructure | Shared engine components and opt-in scheduler exist | Validation and safe structural lifecycle integration |
| Generic locomotion, recoil, melee, vehicle, projectile policies | Public C++ policy headers exist | Wire every concrete gameplay consumer to the same systems and state |
| FPS authority | Opt-in full ECS runtime payloads at tick boundaries; direct motor partially migrated | Remove `State::actors` and dynamic workspace authority from all combat, AI, shooting, vehicles, loot, projectile, history and net paths |
| RTS mechanics | Many systems and native hooks refer to `RtsModule&` | Extract neutral feature components/policies/systems, retain adapters during parity |
| Editor/serialization | Generic runtime component views exist; many genre-specific cards remain | Component registration/schema and versioned, cross-genre save/rollback |
| Shader post-processing | Rendering feature, not simulation gameplay | Keep renderer capabilities separate; validate Metal/GLES independently |

Until the cross-genre test and actual RTS/FPS refactors pass, claim only that
**the architecture supports composition**, not that every existing mechanic
has already been converted.
