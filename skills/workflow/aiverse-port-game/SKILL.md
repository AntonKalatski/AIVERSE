---
name: aiverse-port-game
description: Port a Unity or Godot project into an AIVERSE game - port_scan, port_import, report items resolved through rules, Lua or native modules, verified with scenario_run and screenshots; the game keeps its own repository.
---

# Port a game

Reference: `docs/engine-import.md` (coverage and limits) and `docs/engine-import-spec.md`.
Tool: `tools/importers/port/aiverse_port.py` (Python 3.8+, no packages; every command prints JSON, exit 2 on failure).
The MCP port tools (port scan / import / report / item / resolve) come from the engine-import branch; on `main`
use the CLI below, which is the same importer.

1. `python3 tools/importers/port/aiverse_port.py scan <project>` - read-only inventory.
2. `python3 tools/importers/port/aiverse_port.py <project> --out <dir> --scale 1 --mode auto` into a folder outside the
   engine repo (`--scale` = cells per metre; `--mode auto|2d|3d`). Input and output must not overlap; no concurrent
   imports into one destination.
3. `... report <dir>` -> `... item <dir> --id ITEM_ID` per item. For each: data block (`rules_schema`) first, then Lua
   (`skills/workflow/aiverse-lua-gameplay`), then a native module of the game.
4. Mark only verified work: `... resolve <dir> --id ITEM_ID --how rules|lua|native|dropped --reason '...'` naming the
   scenario or screenshot that proves it.
5. Verify against the source: `open_game` on the result, `scenario_run`, bot matches (`tools/bots/sweep.py`),
   screenshots next to source screenshots (`tools/visual/compare.py` for same-camera pairs).
6. Ownership (AGENTS.md section 3): the game has its own git; game-specific code lives in the game (`native/<area>/`,
   `tools/<game>/`); the engine gets only generic mechanics and hooks without game names.
7. Imported numbers: cooldown and build times are seconds; `fireRate` is approximate; check source player numbering.

Report coverage honestly: items resolved / approximated / dropped, art and platforms not done.

Runtime library: none. The importer lives in `tools/importers/port`.
