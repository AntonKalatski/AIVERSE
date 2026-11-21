# No genre split — stage A design: the FPS host on ECS only (2026-11-21)

Owner rule (AGENTS.md "NO GENRE SPLIT"): all simulation state is ECS entities + components, no `Actor` objects, old
paths deleted, no compatibility modes. This file is the agreed design for stage A of
`docs/no-genre-split-audit.md`; implement it, then build and test on KINGSTON in one pass.

## What exists today (origin/main 2d4a3960e)
- `fps::State` (a World resource) owns `std::vector<Actor> actors`, `projectiles`, `loot`, `vehicles`,
  `vehicleRuns`, `gear`, `botStates`, `history` (rewind frames of whole Actors), `round`, `winner`, `nextObject`,
  `match`, `royale`.
- Two storage modes: default (vectors canonical, ECS `ActorEntity*` / `ProjectileEntity` / `LootEntity` /
  `VehicleEntity` read-only projections) and `fps.ecsAuthority` (`ActorRuntime{Actor}`, `ProjectileRuntime`,
  `LootRuntime`, `VehicleRuntime` canonical; vectors hydrated as tick workspace, `fps.ecsMirror` verifies).
- Consumers of the vectors: `modules/fps/*.cpp` (~270 uses), `engine/net/authority/Session.{h,cpp}` (snapshots,
  prediction, admission), `apps/fps/client.cpp` + `RoyaleHud.h`, `apps/editor/EditorFps.cpp` +
  `EditorRuntimeEntityProvider.h`, `libraries/include/aiverselib/bots/Fps.h`, `tools/libraries/navmesh_main.cpp`,
  `examples/libraries/bots/main.cpp`, `examples/native/strategy/SessionProbe.cpp`, tests `tests/fps/*`,
  `tests/libraries/bots/fps_tests.cpp`, `tests/world/async.cpp`.

## Target
**Characters** (one entity per player slot, created at setup, never destroyed; `CharacterIndex{bySlot}` resource):

| Component | Fields (old `Actor` names kept) |
|---|---|
| `CharacterId` | `slot, team, life, kills` |
| `CharacterBody` | `pos, velocity, yaw, pitch, grounded, crouched, prone, climbing, frozen, vehicle` |
| `CharacterHealth` | `health, armor, downed, respawnAt, bleedUntil, blindUntil, lastDamage` |
| `CharacterWeapons` | `weaponIndex, ammo, reserve, storedAmmo[4], storedReserve[4], shots, hits, switchUntil, fireHeld, reloadUntil, nextFire, recoil, aiming, owned, attachments[4]` |
| `CharacterInventory` | `bag, money` |
| `CharacterControl` | `input, lastInputTick, lastSequence` |
| `CharacterAction` | `activeAction, actionUntil, actionTarget, actionOrigin` |
| `CharacterMelee` | `melee, parryUntil, staggerUntil, lockTarget, blockHeld, meleeHasTip, meleeTip` |
| `lib::systems::GearLoadout` | only when `gear` is configured |
| `BotMemory{std::string bytes}` | only when library bots are configured |

- `CharacterRef` — a non-owning view (reference members with the old field names) built from World + Entity;
  gameplay functions take `CharacterRef` instead of `Actor&`, so their bodies change only where they copied or
  reset a whole Actor. `forEachCharacter(sim, fn)` iterates in slot order (the old vector order).
- `CharacterSnapshot` — a plain value record of the same fields, used only outside the simulation: network
  snapshots and client prediction (`Session`, client, HUD), rewind frames, save files. Built by
  `snapshot(CharacterRef)`. Not stored in the World.
- `moveActor`, `stanceHeight`, `actorBounds` become templates over any type with the body fields (works for
  `CharacterRef` and `CharacterSnapshot`); internal "copy the actor and change the stance" probes use explicit
  pose arguments instead.
- Respawn / round reset (`a=Actor{}`) become `resetCharacter(world, entity, keep...)` that resets each component.

**Dynamic objects**: one entity per projectile / loot item / vehicle with the existing `Projectile`, `Loot`, `Vehicle`
structs as components (they are plain data), plus `VehicleDrive` (old `VehicleRun`) on typed vehicles.
Deterministic iteration = sort by the existing object `id` (monotonic `nextObject`, equal to the old vector order).
Spawns create entities; removals use `destroyLater` + flush at the end of the stage. The 256 / 32 limits stay.

**State resource** keeps only match-level data: `round, winner, nextObject, match, royale, history`
(`history` = frames of `CharacterSnapshot` poses for lag compensation).

**Delete**: `Actor`, `ActorRuntime`, `ActorEntityIdentity/Transform/Vitals` (editor reads the real components),
`ProjectileEntity`, `LootEntity`, `VehicleEntity`, `ProjectileRuntime`, `LootRuntime`, `VehicleRuntime`,
`DynamicEntityIndex`, `syncActorEntities`, `hydrate*`, `syncDynamicEntities`, `authoritativeStateCopy`,
`moveActorEcs`, `fps.ecsAuthority`, `fps.ecsMirror`, `fps.ecsHydrate`, `fps.compat.*` stage names (keep the stage
order under plain names), the parity fixtures that compare the two modes
(`tests/fps/projectiles.inc` branch-authority parity, `projectile_spawn.inc` legacy cases), save versions < 10.

**Save / hash**: save version 10 written from components in slot / id order; versions 3-9 are rejected (owner
allows breaking old saves). Checksums of the four `templates/fps_*` change only if iteration order changes;
record the new values in the stage handoff.

## Order of work (one build at the end)
1. `modules/fps/FpsComponents.h` (components, `CharacterRef`, `CharacterSnapshot`, helpers).
2. `FpsModule.h/.cpp`, `FpsMotor`, `FpsSystems`, `FpsProjectiles`, `FpsMelee`, `FpsRoyale`, `FpsVehicles`,
   `FpsBots`, `FpsBuildings`, `FpsStreaming`.
3. `Session.{h,cpp}` (`Snapshot::characters` of `CharacterSnapshot`), client, `RoyaleHud.h`, editor,
   `bots/Fps.h`, navmesh tool, examples.
4. Tests: replace `state(*sim).actors[i]` with `module.character(sim, i+1)` (same field names), delete
   mode-parity tests, keep every gameplay assertion.
5. Build on KINGSTON, CTest, template checksums, handoff `docs/handoffs/no-genre-split-stage-03.md`, push.
