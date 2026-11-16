---
name: aiverse-port-game
description: Port a Unity or Godot project into an AIVERSE game - port_scan, port_import, report items resolved through rules, Lua or native modules, verified with scenario_run and screenshots; the game keeps its own repository.
---

# Port a game

Reference: `docs/engine-import.md` (coverage and limits) and `docs/engine-import-spec.md`.

1. `port_scan {path}` - read-only inventory.
2. `port_import {path, out, scale, mode}` into a folder outside the engine repo (`scale` = cells per metre; `mode` auto/2d/3d). Re-imports keep edited files.
3. `port_report` -> work through items with `port_item {id}`. For each: data block (`rules_schema`) first, then Lua (`skills/workflow/aiverse-lua-gameplay`), then a native module of the game.
4. Mark only verified work: `port_resolve {id, how: rules|lua|native|dropped, reason}` with the scenario or screenshot that proves it.
5. Verify against the source: `scenario_run`, bot matches, screenshots next to source screenshots.
6. Ownership rules (AGENTS.md section 3): the game has its own git; game-specific code lives in the game (`native/<area>/`, `tools/<game>/`); the engine gets only generic mechanics and hooks without game names.
7. Imported numbers: cooldown and build times are seconds; `fireRate` is approximate; source player numbering must be checked.

Report coverage honestly: items resolved / approximated / dropped, art and platforms not done.

Runtime library: none. Workflow over the engine MCP port tools.
