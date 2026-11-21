# No genre split — stage 05 design: C1, direct control out of `modules/rts` (2026-11-21)

Branch `feature/aiv20261121_no-genre` after 4aac6d009 (merge of origin/main). Plan: `docs/no-genre-split-audit.md`
stage C, first system. Recipe: `skills/workflow/aiverse-shared-capability/SKILL.md`; template: stage B (`engine/vehicles`).

## What exists
`modules/rts/RtsTopDown.{h,cpp}` (1.4k lines) + view `modules/rts/view/TopDownControls.{h,cpp}`,
`TopDownPrediction.h` (1k lines). One hero per player steered by sticky quantized input (`net::kCmdHeroInput` /
`kCmdHeroAbility`, already engine wire format), aim snap / auto-fire targeting, ability hold / windup, squad ring,
escape, computer pilot steering. State: `TopDownStore` world resource = `vector<TopDownPilot>` by player slot.
Coupling to the RTS host: 26 distinct `RtsModule` calls (state.grid / colliders / spatial hash, rules().types[...]
radius / weapons / pathLayer / structure, stat(Speed|Range|MaxHp|Damage), contactDistance, hostile, isVisible,
statusFlags, canCast / castAbility / abilityFor, dealDamage, initProjectileProgram, spawn, moveTo, stopMoving, lift,
progression, encounters, driving door vars) and RTS components `Unit`, `Combat`, `Mobile`, `Buffs`, `Orders`,
`Footprint` (+ shared `Transform`, `Owner`). Users: client (`ClientTopDown`), editor SimTools, battle / city apps,
city net, commerce, driving API, script API, view, Kingston police policy, tests (topdown, raid, battle, city).
Templates with the block: boss_arena, perk_arena, road_race, topdown_arena.

## Target
- `engine/control/` (`AIVERSE::control`, namespace `aiverse::control`) - the capability "direct control" of one
  character per player by any project.
- **ECS state, no store vector.** A pilot entity per player: `control::Pilot` (player, hero, heroType, home,
  respawnAt, input, inputTick, vel, target, retargetAt), `control::PilotCast` (held / casting / escapeReadyAt, only
  when the rules need it), `control::Squad` (member entities). The controlled character carries
  `control::Controlled{pilot}`; RTS code that skipped `kUnitPossessed` units queries this component and the flag is
  deleted. Parsed rules live in the `control::ControlRules` world resource.
- **Host world query** `control::ControlHost` (as `vehicles::VehicleHost`): body (pos, radius, speed, facing, path
  layer, alive, owner, type, mobile, structure), `walkable` / next path waypoint, nearby bodies, hostile / visible /
  line-of-fire, weapon range + stats, ability can / cast / hold, damage + shot program, spawn / respawn, move / stop /
  lift, bot goals (progression orbs, encounter circles, pickups), seat check (driving). The step functions
  (`stepControl`, `controlCommand`, `botSteer`, `aimCandidates`, `escape*`, `squadSlot`) live in `engine/control` and
  only talk to the host.
- RTS keeps a thin adapter `RtsControl` (the `ControlHost` over grid map, RTS components and rules). Deleted:
  `RtsTopDown.{h,cpp}`, `TopDownStore`, `TopDownPilot`, `kUnitPossessed`, the module's `topDown_` member.
- View: `TopDownControls` / `TopDownPrediction` move to `engine/control/view` (client input, aim assist, prediction);
  `RtsView` and `ClientTopDown` call them through the same host.
- Save / hash: pilot entities are hashed in entity order by `hashControl`; the store resource layout is gone, so
  saves of games with the block break and template checksums of the four templates are re-recorded on purpose
  (A/B check: behaviour tests `topdown_*`, `raid_*`, battle and city tests must pass unchanged).

## Data block name (owner decision)
The rules / map block is called `"topDown"` (a camera style, also the old host name). Capability name would be
`"control"`. Renaming breaks the owner's external games (DroneFall `main-game`, GTA2) until their data is changed;
no alias may be kept (no compatibility). Default in this stage: **keep `"topDown"`** and rename in stage F with the
package ids, unless the owner asks otherwise.

## Steps (one build + CTest + checksums at the end)
1. `engine/control`: rules + parse + requantize + angle math (pure, unit-tested), components, `ControlHost`.
2. Port tick / command / casts / squad / escape / free fire / aim / bot steering onto components + host.
3. `RtsControl` adapter; switch `RtsModule` and every user listed above; delete `RtsTopDown.*`, the store, the flag.
4. View move; client / battle / city / editor users.
5. Docs (`docs/topdown.md` -> capability doc, game-format), skills (aiverse-topdown*), AGENTS area table, audit row.
