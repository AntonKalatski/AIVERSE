---
name: aiverse-unit-balance
description: Balance units and factions of an AIVERSE game through MCP - stat comparison, equal-cost duels and counter matrix, rules edits, bot batches; also keep rules small with abstract/extends and include.
---

# Unit balance

1. `rules_schema` (format) -> `rules_list` / `rules_get` for the units in question.
2. Numbers: `balance_stats` - DPS per armor class, value per cost, side by side. Spot outliers before fighting.
3. Fights: `balance_duel` (two types, equal cost) and `balance_matrix` (all pairs: who counters whom). A healthy roster has counters, no type that wins everything at equal cost.
4. Edit with `rules_set` / `rules_merge`; one change at a time, note before/after.
   - Shared values go to an `"abstract": true` base that entries `extends`; levels contain only what changes.
   - Big games split rules with `"include": ["rules/units.json", ...]`; rules ops write each entry back to its file.
   - Read `rules_lint` in every answer.
5. Re-run the same duels/matrix, then `balance_batch` (many bot matches, `mirror` on) for win rate per faction, match length and never-built items.
6. Never-built items: too expensive, locked behind tech the bot never takes, or dominated by another unit - fix the data, then re-check.

Report changed keys with before/after numbers, sample sizes and seeds. Bot behaviour itself is `skills/ai/aiverse-bots-tune`.

Runtime library: none. Workflow over the engine MCP balance tools.
