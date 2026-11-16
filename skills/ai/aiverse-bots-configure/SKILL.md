---
name: aiverse-bots-configure
description: Configure the computer player of an AIVERSE game through MCP - rules.ai knobs, bot play styles, a custom Lua or native director (controller scripted), strategic-map bots.
---

# Configure bots

Game-data work through the engine MCP; no engine code. Native host integration of `RtsBot` presets is `skills/ai/rts-bots-rts`.

## 0. Pick the controller

`open_game` -> `rules_get` block `ai`; read `docs/game-format.md` "Computer player (`RtsAI`)" and `docs/ai-controllers.md`.
- `builtin` (default): shared `RtsAI` - economy, construction, research, waves, terrain analysis, garrisons, planets.
- `scripted`: `RtsAI` submits no commands (client, editor, headless). The game must ship its own director. Combat, production and victory keep working.
- `bw`: Bunker Wars bots; not for new games.
Player `waves: true` is a different thing (wave players have other defeat semantics).

## 1. Built-in knobs (`rules.ai`)

All optional; `RtsData.cpp` range-checks them and fails loading with `ai.<key>`.

| key | default | range | meaning |
|---|---|---|---|
| `workersPerBase` | 14 | 0..200 | workers per base |
| `firstAttackArmy` | 8 | 1..500 | first wave size |
| `armyGrowth` | 4 | 0..100 | wave growth |
| `maxAttackArmy` | 16 | 1..1000 | wave cap |
| `towers` | 1 | 0..16 | chokepoint towers after a raid |
| `scouts` | 1 | 0..8 | scouts on high ground along the route |
| `invasionSize` | 6 | 1..200 | fleet size before moving to another body |
| `escalateSeconds` | 300 | 30..36000 | no structure destroyed this long -> stalemate level rises |
| `climbWeight` / `exposureWeight` | 24 / 12 | 0..1000 | route cost per level climbed / overlooked (10 = one cell) |
| `terrain` | true | bool | false = no terrain analysis, direct attack-moves |

Edit with `rules_merge {"ai": {...}}`, read the `rules_lint` part of the answer. Never invent keys (`rules_schema` with `part: "keys"`).

## 2. Play styles

`rules.ai` is one block per game: JSON cannot give slots different personalities. A style is a knob set (starting points, verify with `skills/ai/aiverse-bots-tune`):
- rush: `firstAttackArmy` 3-5, `armyGrowth` 2, `workersPerBase` 8-10, `towers` 0;
- defender: `towers` 3-4, `firstAttackArmy` 12+, larger `escalateSeconds`;
- economist: `workersPerBase` 18-24, `firstAttackArmy` 12, `maxAttackArmy` 30+.
Per-slot styles/difficulties exist only in a native host (`RtsBot` presets `easy normal hard rush defender economist`, `docs/bots.md`); `rules.ai` still overrides them. Difficulty changes decisions only, never stats.

## 3. Custom director (`controller: scripted`)

1. `script_api`: what a director can query, order and listen to.
2. `script_write {path: "scripts/ai/<name>.lua", register: true}`. Send ordinary player commands; never write health or positions.
3. Integers and `fx()` only; engine RNG; gameplay state in saved `state`.
4. Budget: ~0.7-1.0 us per unit read; stay within 3-5 ms per tick. Think every N ticks.
5. `script_check` -> `match_run` -> same seed twice = same checksum.
6. Heavy AI: the game's native module (`skills/core/aiverse-native-scaffold`), never game-specific code in the engine.

## 4. Strategic-map bots

`strategic_state` -> `strategic_bots` -> `strategic_order` for manual checks -> advance turns -> `strategic_save`; battles via `strategic_battle_map` + `match_run`. Format: `docs/game-format.md` "Strategic map".

## Done

`rules_lint` clean, `map_validate` clean; `match_run` on every map: commands issued, winner before the limit (or an honest draw by limit); same seed -> same checksum; save/load (`--save`/`--load`, `.ai` file) plays on identically. Report knobs, seeds, match count, what is unverified.

Runtime library: none. Workflow over the engine MCP and existing `RtsAI`.
