---
name: aiverse-lua-gameplay
description: Add AIVERSE gameplay the data blocks cannot express with simulation Lua scripts - script_api, script_write, script_check, determinism and per-tick budget rules.
---

# Lua gameplay

Order of preference: data blocks (`rules_schema`) -> Lua -> native module. Use Lua when no data block fits.

1. `script_api` - the reference: queries, orders, events, `state`, effect hook `script` (weapon `onHit`, status `tick`, traps, zones). Read `docs/scripting.md`.
2. `script_write {path: "scripts/<name>.lua", register: true}`; small focused files.
3. Determinism (simulation scripts): integers and `fx()` numbers only, no floats, no wall clock, no `math.random` (engine RNG). Persistent values live in `state` (saved and hashed every tick - keep it small).
4. Budget: Lua reads cost ~0.7-1.0 us per unit; keep a script within 3-5 ms per tick on a weak phone. Work every N ticks, cache lists, avoid per-cell work over big grids every tick.
5. `script_check` - compile + short bot match, errors as file:line. Fix every runtime error.
6. Prove behaviour: `scenario_run` (named objects, commands per second, pass/fail checks) or `play_start` / `play_command`.
7. Determinism: same seed twice -> same checksum; save/load mid-match continues identically.
8. Built-in bots ignore script-only mechanics unless taught (`skills/ai/aiverse-bots-configure`, scripted director, or engine AI work).

Runtime library: none. Workflow over the engine MCP script tools.
