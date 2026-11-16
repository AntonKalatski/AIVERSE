---
name: aiverse-water
description: Add or change spline water in an AIVERSE map - straits, rivers, the open sea, with embankments, cliffs, beaches and natural banks chosen automatically or by hand - through the water_* MCP ops or the editor window «Вода»; a game supplies only models and textures.
---

# water

Behaviour and format: `docs/water.md` (read it fully). Code: `modules/rts/view/RtsWater.*` (data, built-in shore types, auto shores, cells; float, editor and view only), `modules/rts/view/RtsShoreView.*` (meshes, props, foam), `apps/editor/core/WaterOps.*` (ops, baking), `apps/editor/windows/WaterWindow.cpp`. Tests: `tests/tests_water.inc` (`aiverse_tests water_`).

1. The simulation never reads `waterNetwork`: every op bakes it into the map's cells (`~` water, `#` under cliffs, the map's tile chars when `tiles` names them) and keeps the old cells in `baked`. Paths, naval units, bridges and checksums see ordinary cells.
2. Bodies: `strait` / `river` with `points: [[x, z, width_m], ...]` (Catmull-Rom, may leave the map), `sea` with `side` and a `shore` line edge to edge. `shores`: `"auto"` or `{side, from, to, type}` in metres along the bank.
3. Shore types (rules `shoreTypes`, like `roadTypes`): built-in `embankment`, `embankment_stone`, `cliff`, `beach`, `bank`; a game retunes by id or adds its own (`kind` / `extends`), and gives `textures` and prop `model`s. Nothing else belongs in the game: no water generators in its scripts.
4. Ops: `water_types`, `water_network`, `water_set`, `water_delete`, `water_shores`, `water_bake` (after editing roads or rules). Read `landParts` / `separatedLand` in the answer: land split without a bridge is a warning.
5. Auto shores: straits get embankments both sides; rivers get embankments only where the city comes within 40 m; the sea mixes embankments, cliffs (headlands, map edges) and beaches. Pieces shorter than 30 m merge into a neighbour.
6. Check: `aiverse_tests water_`, then screenshots (`view_screenshot` with `window: scene`) of both banks of a strait, a river mouth, cliffs, a beach; fix seams before reporting. Model packs follow AGENTS.md "Model packs: every texture once".

Not done: wall modules from game models along the bank, waves, water at several heights, drawing a body with the mouse. Runtime library: none (engine module).
