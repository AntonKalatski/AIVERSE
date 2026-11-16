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

## D. Sweep all maps (`tools/bots/sweep.py`)

1. Build `aiverse_headless`, then
   `python3 tools/bots/sweep.py --headless <build>/aiverse_headless --project <game> --seeds 1,2,3 --max-seconds 1800 --json sweep.json`
   (`--maps a,b` to narrow, `--jobs N` in parallel). It plays every map of `game.json` with bots in all slots.
2. Classes per run: `win`, `draw`, `timeout` (no winner by the limit), `idle` (a slot that is not `waves`/`monsters` issued 0 commands - read from the runner's `Player:` lines), `error` (runner failed; last log lines shown), `nondeterministic` (the first seed played twice gave different checksums). Exit 1 on idle, error or nondeterministic.
3. Campaign/tutorial maps that time out usually lack portable victory conditions - not a bot fault, mark them separately.
4. `nondeterministic` = stop and use `tools/determinism/diff.py` (`skills/verification/aiverse-determinism-triage`).
5. Paste the Markdown table into the report; problem maps go to `skills/ai/aiverse-bots-debug`.

## Reporting

Sample size and seeds, no confidence beyond them; a match at the limit is not a win; bot-vs-bot wins do not prove strength against humans; knobs with before/after numbers.

Code: `tools/bots/sweep.py` (tests `tools/bots/test_sweep.py`, CMake test `tool_bot_sweep`), per-slot data in map `players[i].ai`. No library target.
