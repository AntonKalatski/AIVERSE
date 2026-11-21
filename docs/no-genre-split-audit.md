# No genre split: audit of features and skills (2026-11-21)

Owner rule (AGENTS.md "NO GENRE SPLIT — EVER"): no feature depends on a genre. A genre exists only as a
template / wizard label (`templates/*`, `game.json` `genre`, `libraries/include/aiverselib/app/Genre.h` kits).
Engine, libraries, editor, tools and skills describe capabilities enabled by data.

Audited: `origin/main` 7f3a9fbb4 (2026-11-21), all 236 skills, `engine/`, `libraries/`, `modules/`, `apps/`,
`tools/`. Debt ids G1-G9 below are referenced from skills; close a debt item by deleting the split, not by
renaming it.

## Fixed in this stage (text only, no behaviour change)

- Skills category `skills/genres/` removed; its 11 skills moved to `skills/gameplay/` (`CATEGORIES` in
  `tools/skills/catalog.py` updated).
- Genre / module-named skill ids renamed (package ids and CMake targets unchanged, see G7):
  `rts-generation` -> `aiverse-map-generation`, `rts-navigation` -> `aiverse-pathfinding`,
  `rts-spatial` -> `aiverse-spatial-queries`, `rts-flow-war` -> `aiverse-flow-economy-warfare`,
  `rts-bunkerwars-skills` -> `aiverse-bunkerwars-skills`, `rts-bots-rts` -> `aiverse-bots-commander`,
  `aiverse-bots-fps` -> `aiverse-bots-combatant`, `aiverse-genre-bots` -> `aiverse-action-bots`,
  `aiverse-genre-kits` -> `aiverse-template-kits`.
- 36 skills rewritten from "use for top-down / GTA-like / RTS / FPS projects", "needs a top-down hero game",
  "FPS projects get it from `fps.*`", "stays authoritative in RTS games" to: the capability, the data block that
  enables it, the one-owner-per-state rule, and the current legacy host as debt (G-id).
- AGENTS.md: skill naming rule (`rts-*` only for RTS-specific behaviour) replaced by "never name, group or
  scope a skill by genre or legacy module"; §9 "genre packages" renamed to mechanics packages (`genre/` is a
  legacy folder name).
- `docs/genre-implementation.json`: area titles named after genres (RTS/TD, FPS, MOBA, bullet heaven,
  metroidvania, roguelite, "жанровые инструменты") renamed to capability names; ids and statuses unchanged.

## Debt: genre dependencies still in code

### G1. Simulation host chosen by genre: `game.json` `module`
`module` = `rts` | `fps` | `voxel` | `genres` (`MotionModule`, `BoardModule`) | `strategic` picks one of five
separate simulations; each project gets only that host's features.
- `apps/server/GameData.cpp:31` runs only `rts`; `engine/net/server/ServerMatch.cpp:452` defaults `module` to `rts`.
- `apps/headless/main.cpp:66,453,723` — static cast to `RtsModule` by name, separate `playFps`.
- `modules/fps/Project.h:14`, `modules/voxel/Project.h:13` reject other modules;
  `modules/rts/view/RtsViewScript.cpp:838` view scripts only when `module().name()=="rts"`.
- `modules/genres/MotionModule.cpp:38` — `motion.kind` must be `platformer` or `racing` (genre names as a mode).
- 36 `dynamic_cast<RtsModule*>` (campaign, scenario, AI, audio, fx, overlays, view).

### G2. Two map documents and editor gates
Flat grid map (`MapDoc.data.extra`) vs world-space map (`MapDoc.data.fps`, 62 references). The editor and the
native Studio branch on `Workspace::isFps()` (`apps/editor/core/Workspace.h:37`, `module == "fps"`): 78 uses in
28 files. Disabled or forked on one side: Play, Hierarchy/Inspector, light entities (Y semantics), NEREID water
authoring, TerraWeave and earthworks, overlays, FX preview, map creation, rules window and `rules_prune`
(`PruneOps.cpp:181`), geometry scene, `FpsWindow`/`FpsOps` "open an FPS project first",
`StudioGameLaunch.h:13` "FPS Play requires the FPS host".

### G3. Capabilities hosted only by `modules/rts` (data-enabled, unavailable to other hosts)
Direct-control heroes (`RtsTopDown`, rules `topDown`), driving (`RtsDriving`), road traffic and population
(`RtsTraffic*`), spline roads model + view (`RtsRoadNet`, `RtsRoadTypes`, `view/RtsRoadView`), raids
(`RtsRaid*`), commerce, careers, arsenal, encounters, in-match progression, campaigns (`RtsCampaign`),
side commander bot (`RtsAI*`, `RtsBwAI*`), sensors / flow economy (`RtsSensors`, `RtsFieldOps`), grid fields,
skill objects, settlers, dungeon, planets, fog/vision (`RtsVision`), map generator incl. city planner
(`modules/rts/gen`), terrain surface view, script HUD and entity action controls (`view/ScriptHud.h`,
`view/EntityActionControls.h`), legacy water view adapter, Lua `rts` global (`RtsScriptStd.cpp:886`).
Game-specific code inside the engine host: `modules/rts/Kingston*` (move to the Kingston game's `native/`).

### G4. Capabilities hosted only by `modules/fps`
Typed vehicles (`FpsVehicles`, `fps.vehicleTypes`), gear (`fps.gear`), battle royale (`fps.royale`,
`FpsRoyale`), generated buildings wiring (`FpsBuildings`), melee (`FpsMelee`), combatant bots (`FpsBots`),
streaming (`FpsStreaming`), animation modes (`fps.visual.*`), ECS authority switches (`fps.ecsAuthority`).
`tools/libraries/navmesh_main.cpp:10` refuses non-FPS projects.

### G5. One capability implemented twice
- Vehicles: `RtsDriving` and `FpsVehicles` (both over `vehicle-controllers`).
- Bots: `RtsAI` (side commander), `FpsBots` (combatant), `bots/` action bots.
- Match end: RTS victory, FPS rounds (`fps.mode`), `match-rules` objectives.
- Movement / combat: FPS `moveActor` and RTS `sysMovement`, RTS `sysProjectiles` vs FPS projectiles — partly
  moved onto shared ECS policies (see `aiverse-ecs-*` skills), not finished.

### G6. Genre-named configuration
Rules blocks `fps.*` (`mode`, `royale`, `gear`, `vehicleTypes`, `visual`, `ecsAuthority`, ...), `topDown`
(means direct control, not a camera), `motion.kind: platformer|racing`. Branch
`feature/aiv20261121_racing` (5fb186599, not on main) adds `fps.mode "race"` and `skills/genres/aiverse-racing` —
both violate the rule and must be reworked before merge (race rules as a shared session usable by any vehicle
host; the skill goes to `skills/gameplay/` because the `genres` category no longer exists).

### G7. Genre names in shared code and docs (no behaviour, rename with compatibility)
`libraries/include/aiverselib/mechanics/` (135 headers), namespace `aiverse::lib::genre`, target `AIVERSE::genre`,
CMake `aiverse_native_use_genre`, `aiverse_native_use_bots(... RTS FPS)`, packages `bots-rts`, `bots-fps`,
`genre-bots`, `genre-kits`, `lib_bots_rts`/`lib_bots_fps`, `docs/genre-packages.md`,
`docs/genre-implementation.json`, test names `genre_*`, `ecs_cross_genre_*`.

### G8. Template kits (checked: no host coupling)
`app/Genre.h` kits carry a `family` that the header documents as a user-facing classification, not a module
name; they are read only by the template wizard and app tools. Nothing to remove; keep it that way.

### G9. Tools and defaults
`apps/editor/core/Projects.cpp:92` falls back from `genre` to `module` (`"rts"`); `module` default `"rts"` in
editor, server, headless; `tools/citygen` writes only the flat grid format.

## Progress (branch feature/aiv20261121_no-genre)

| Stage | State |
|---|---|
| 1 Naming (G7) | code written: `aiverselib/mechanics`, namespace `aiverse::lib::mechanics` (alias `genre`), forwarders, CMake aliases, docs renamed. Package ids `bots-rts`/`bots-fps`/`genre-bots`/`genre-kits` and test names still old |
| 2a Capability queries (G2) | code written: `Project::isFps()` removed, `worldSpaceMaps()` / `actorSessionPlay()` / `actorSessionRules()` / `hostId()` |
| 2b One scene document (G2) | not started |
| 3 One vehicle system (G5) | not started - needs build-driven work |
| 4 Extract `modules/rts`-only capabilities (G3) | not started |
| 5 Shared blocks (G6) | partly: actor-session host reads `royale`, `gear`, `crate`, `vehicleTypes`, `bots`, `weapons`, `weapon` from the top level of rules |
| 6 One simulation (G1) | not started |
| 7 Template kits (G8) | nothing to do |

Every stage above is unverified until the Mac build and tests in the latest `docs/handoffs/no-genre-split-stage-NN.md` pass.

## Removal plan (each stage: own branch, merge main first, handoff, full tests)

1. **Naming (G7).** `git mv genre/ mechanics/` with one-line forwarding headers, namespace alias
   `lib::genre = lib::mechanics`, CMake alias targets, package ids `bots-rts` -> `bots-commander`,
   `bots-fps` -> `bots-combatant`, `genre-bots` -> `action-bots` with old ids as aliases; docs renamed. No
   behaviour change, checksums unchanged.
2. **Capability queries instead of `isFps()` (G2).** `Workspace` exposes what the open document has
   (`hasGridMap`, `hasWorldSpaceMap`, `hasHostPlay`...); every gate asks for the capability it needs. Then one
   scene document: world-space entities with components for both formats, adapters reading old files.
3. **One vehicle system (G5, G4).** Extract `RtsDriving` + `FpsVehicles` into `engine/` systems over
   `vehicle-controllers`; both hosts become adapters; rules `driving` is the one block (`fps.vehicleTypes`
   read as an alias). Rework the racing branch on top of it.
4. **Extract `modules/rts`-only capabilities (G3)** one per stage, in dependency order: roads model+view ->
   direct-control characters -> traffic -> raids / commerce / careers / arsenal -> encounters / progression ->
   map generator and city planner -> campaign -> side commander bot. Each gets an engine system, a data block
   without a genre name, adapters in old hosts, and a test on at least two hosts.
5. **Shared rules blocks (G6).** `fps.royale`, `fps.gear`, `fps.mode` etc. become top-level blocks read by
   shared systems; old keys load as aliases with a lint warning.
6. **One simulation host (G1).** `game.json` lists systems / blocks instead of `module`; `module` stays as a
   compatibility preset that expands to a system list. Server, headless, editor, navmesh tool and Studio Play run
   any project.
7. **Template kits (G8)**: verified, the kit carries only a label (`family`); no change needed.

## Rule for agents meanwhile

A skill or plan that needs a capability from one legacy host includes its extraction (AGENTS.md). Do not add
new `isFps()`, `module ==`, `dynamic_cast<RtsModule*>`, `fps.*` blocks or genre-named modes.
