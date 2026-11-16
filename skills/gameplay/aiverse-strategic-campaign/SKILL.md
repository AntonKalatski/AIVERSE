---
name: aiverse-strategic-campaign
description: Build an AIVERSE strategic layer like Total War or Company of Heroes 3 campaigns - regions, armies, turn orders, bot factions, battles spawned into RTS maps, saves.
---

# Strategic campaign map

Format: `docs/game-format.md` "Strategic map (Total War campaign, Company of Heroes 3, Empire at War)".

1. Data: the strategic block in the game data (regions, factions, armies, economy per turn) written with `rules_merge` / `game_file_write`; read `rules_schema` for exact keys.
2. Load and inspect: `strategic_load` -> `strategic_state`.
3. Orders: `strategic_order` for each manual check (move, attack, recruit); advance turns and re-read the state.
4. Bots: `strategic_bots` - which factions are bot-controlled and how; play several turns bot-only and confirm factions expand and fight.
5. Battles: `strategic_battle_map` shows which RTS map and armies a battle produces; play it with `match_run` and confirm the result returns to the strategic state.
6. Saves: `strategic_save` mid-campaign, reload, continue - same state.
7. Mission-style story campaigns without a strategic map are `skills/workflow/aiverse-campaign`.

Report: turns simulated, battles fought, bot behaviour seen, unverified parts.

Runtime library: none. Workflow over the engine MCP strategic tools.
