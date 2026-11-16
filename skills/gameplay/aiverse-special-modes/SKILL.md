---
name: aiverse-special-modes
description: Route and verify AIVERSE match modes beyond classic RTS - top-down heroes (twin-stick), extraction raids, city traffic - their data blocks, samples, bots and checks.
---

# Special match modes

Pick the mode, read its document fully, start from its sample template, change data only.

| mode | document | sample | code |
|---|---|---|---|
| Top-down heroes: one directly controlled hero per player, sticks, aim, abilities, respawn | `docs/topdown.md` | `templates/topdown_arena` | `modules/rts/RtsTopDown.*` |
| Extraction raids: containers, loot, objectives, extraction points, receipts for meta | `docs/raid.md` | `templates/extraction` | `modules/rts/RtsRaid*.cpp` |
| City traffic: lane cars, signalised junctions, pedestrians | `docs/traffic.md` (+ `docs/driving.md`) | city games | `modules/rts/RtsTraffic.*` |

Steps:
1. `create_project {template}` from the sample or `rules_merge` the block (`topDown`, `raid`, `traffic`) into the game; `rules_lint` clean.
2. Maps: heroes need spawn points; raids need containers and extraction areas; traffic needs road characters in `tiles.rows` listed in `roads.tiles`.
3. Controls and HUD: game.json `topDown` block (joystick, buttons, keyboard, gamepad, camera); check on phone profiles (`device_set` + `view_screenshot {window: game}`).
4. Bots: heroes ignore RTS army code (`kUnitPossessed`); raids have their own raid bot - confirm raiders loot and extract in `match_run`.
5. Receipts (raids): deterministic JSON with a stable operation id - the meta layer applies it once (`skills/meta/aiverse-match-bridge`).
6. Determinism: same seed -> same checksum; games without the block hash as before.

Runtime library: none. Workflow over existing engine modes.
