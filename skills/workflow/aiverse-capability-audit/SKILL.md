---
name: aiverse-capability-audit
description: Answer "what is missing to make game X on AIVERSE", "can the engine do Y" or any estimate of engine limits from the current repository - genre status file, skill and library catalogs, module docs - never from memory or dated notes.
---

# Capability audit: what exists, what is missing

Use before any feasibility estimate, gap list, genre plan or claim such as "the engine cannot do X" or
"X is limited to N". The engine changes daily; dated notes and memory are wrong more often than not.

## Sources of truth, in this order

1. `docs/genre-implementation.json` - status of every cross-genre area (`planned` / partial / done) with
   `scope`, `acceptance` and `remaining`. Quote area ids (W1, W2, S2...) in the answer.
2. Skill catalog: `python3 tools/skills/catalog.py search <words>` (English and Russian keywords), then read
   the matching `skills/<category>/<id>/SKILL.md`. Library packages: `python3 tools/libraries/catalog.py search
   <words>`, `libraries/catalog.json`, `docs/genre-packages.md`, `docs/libraries.md`.
3. Module and subsystem docs: `docs/overview.md`, then the specific doc (table below). Each states its own
   limits ("Not added", "Limits (known)", "remaining") - copy those, do not invent them.
4. Code when a doc is silent: `git log -1 --format=%cd` on the main checkout, then grep the module.
5. Notes outside the repository (claude.ai project docs, chat summaries, old reports) are dated snapshots.
   Use them only for history; check each claim against 1-4 and say which ones turned out stale.

## Subsystem map (check each relevant row; do not stop at the first module that looks right)

| Area | Where | Typical wrong assumption |
|---|---|---|
| Genres / modules | `modules/rts`, `modules/fps`, `modules/genres`, `modules/strategic`, `docs/topdown.md` | "AIVERSE is only an RTS engine" |
| Large worlds | `docs/large-worlds.md`, `engine/world`, `examples/world/streamed`, `templates/fps_survival` | "maps are capped at 2048x2048" - streamed chunk worlds exist (static content; see W1/W2 for mutable) |
| Networking | `docs/battle-net.md` (lockstep + rollback relay, `engine/net/server`), `docs/fps.md` (authoritative server: prediction/reconciliation, snapshots, interest filtering, hitscan rewind, `engine/net/authority`), `docs/city-server.md` (persistent authoritative world, clients run no sim, `engine/net/city`) | "networking is lockstep only" |
| Physics | `docs/physics.md` (optional Box3D, `engine/physics`), `skills/gameplay/aiverse-physics` | "no rigid bodies" |
| Grid fields (fire, water, gas) | `docs/fields.md` | "no cellular simulation" (fields are 2D map layers) |
| Character movement | `skills/gameplay/aiverse-character-locomotion` (ladders, swim, climb, mantle), `docs/surface-locomotion.md` | "only RTS unit movement" |
| Inventory, crafting, building | `skills/gameplay/aiverse-inventory`, `skills/gameplay/aiverse-construction`, `skills/gameplay/aiverse-loot-tables` | "items are RTS hero slots only" |
| Survival, ecology, colony | `skills/genres/aiverse-survival-ecology`, `skills/genres/aiverse-settlement-production` | |
| Bots and navigation | `docs/bots.md`, `skills/world/aiverse-navmesh` (layered AABB navmesh, rebuilt on change, not streamed) | "only 2D A*/HPA*" |
| Geometry, LOD, models | `docs/geometry.md`, `docs/autolod.md`, `docs/models.md` (includes MagicaVoxel `.vox` models) | |
| Saves | `skills/core/aiverse-save-migrations`, `docs/native-modules.md` | "no state serialization" |
| Terrain, water, cities | `skills/world/aiverse-terrain`, `skills/world/aiverse-water`, `skills/world/aiverse-city-planner` | "terrain is fixed cells only" |
| Visual extras | `skills/visuals/aiverse-cloth`, `skills/visuals/aiverse-cables`, `skills/visuals/aiverse-modeling`, `skills/visuals/aiverse-paint-layers` | "needs an external DCC or physics engine" |

## Steps

1. Restate the target game as concrete capabilities (world, player, rules, AI, network, content, tools).
2. For each capability run step 1-3 lookups above; record `exists` (path), `partial` (path + its stated limit)
   or `missing` (genre-implementation id or "not tracked").
3. Pick the network model explicitly from the three above; justify it by player count, hidden information and
   persistence, not by habit.
4. Report: one table capability -> status -> source path; then the real gaps ordered by risk; then what was
   not verified (platforms, performance). State the repository commit you read.
5. If a gap is real and untracked, propose adding it to `docs/genre-implementation.json` (owner per AGENTS.md
   area table) instead of only mentioning it in chat.

## Worked example: voxel sandbox (Minecraft-like), main at 2026-11-16

Exists: streamed chunk worlds with budgets and async loading (`docs/large-worlds.md`), first-person
authoritative FPS runtime with prediction and interest filtering (`docs/fps.md`), persistent city server
(`docs/city-server.md`), locomotion modes, inventory/crafting, survival/ecology, Box3D, 2D grid fields,
navmesh, `.vox` model import. Missing (tracked): W2 mutable volumetric world (voxel chunk data, meshing,
mutable collision/navigation, light, saving edits) and W1 persistent large world (authoritative streaming of
mutable state, regional saves) - both `planned`; FPS chunk collision is immutable and hashed; snapshots have no
delta compression; fields and navmesh are not volumetric. Re-check these statuses before reusing the example.

Workflow recipe over existing docs and catalogs; no runtime library.
