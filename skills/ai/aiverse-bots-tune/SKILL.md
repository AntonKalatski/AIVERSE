---
name: aiverse-bots-tune
description: Tune AIVERSE bot strength and sweep bots over every map of a game - win rate per slot and faction, difficulty ladder, matches without a winner, summary report.
---

# Tune bots and sweep maps

MCP: `balance_stats`, `balance_duel`, `balance_matrix`, `balance_batch`, `match_run`, `map_list`, `rules_merge`, `undo`. Knobs: `skills/ai/aiverse-bots-configure`.

## A. Measure first

1. `map_list` -> 3-5 representative maps (small, large, relief, 1v1, FFA).
2. `balance_batch` per map: >= 20 matches, fixed seed list, `mirror` on. Record win rate per slot and faction, mean length, draws by limit, never-built items.
3. Faction skew is unit balance, not the bot: `skills/workflow/aiverse-unit-balance`. Never tune the bot around bad balance.

## B. Find the weak spot

From `match_run` logs and `play_units` snapshots:
- few workers / idle resources -> `workersPerBase`;
- early waves die -> raise `firstAttackArmy`; enemy techs up first -> lower it;
- stalemates -> lower `escalateSeconds`, raise `maxAttackArmy`;
- waves climb into fire -> raise `climbWeight` / `exposureWeight`;
- base falls to the first raid -> raise `towers`.
One or two knobs per iteration.

## C. Loop

edit -> same `balance_batch`, same seeds -> compare -> keep or `undo`. Stop at the target or after 3 iterations without gain, then report the built-in AI's limit.
Difficulty ladder (native `RtsBot` presets only): each level beats the previous > 60% on the same seeds.

## D. Sweep all maps

1. `match_run` on every map, bots in all slots, the game's limit (usually 1800 s), 2-3 seeds.
2. Classify: win / draw by limit / slot issued 0 commands / load error or crash / campaign or tutorial without portable victory (not a bot fault).
3. One seed twice: different checksums = determinism bug, stop and investigate.
4. Table: map | result per seed | length | commands per slot | cause. Problem maps -> `skills/ai/aiverse-bots-debug`.

## Reporting

Sample size and seeds, no confidence beyond them; a match at the limit is not a win; bot-vs-bot wins do not prove strength against humans; knobs with before/after numbers.

Runtime library: none. Workflow over the engine MCP balance and match tools.
