---
name: aiverse-city-planner
description: Generate an AIVERSE city map for top-down games with the map generator preset `city` / `metropolis` - street hierarchy, river and bridges, roundabouts, zoned blocks, parks, car parks, curb parking, props, parked cars, people and the hero.
---

# City planner (generator preset `city`)

Use when a top-down / GTA-like project needs a whole city laid out procedurally. Hand-made spline roads are
`skills/world/aiverse-roads`; spline water `skills/world/aiverse-water`; city systems on top (commerce, careers,
arsenal) `skills/gameplay/aiverse-city-economy`; traffic and heroes `skills/gameplay/aiverse-special-modes`.

Authority: `docs/city-planner.md`. Code: `modules/rts/gen/GenCity.cpp`.

1. The planner takes building, tree, prop and car types from the game's own rules (see the doc's "What it takes
   from the rules"); add types first, then generate.
2. Run it as a preset: at match start (map `"generate": {"preset": "city", "seed": "match"}`), in the editor's
   «Генерация» window, through MCP `map_generate` (list presets with `gen_presets`) or
   `aiverse_headless --generate city`. `"layout": "organic"` / preset `metropolis` gives the organic layout.
3. The result writes a `"city"` plan into the map (junctions, `parkingAccess`, `curbParking`) that traffic and
   game scripts read; do not recompute it elsewhere.
4. A full project from the user's art: `tools/citygen/make_city.py --assets <art> --out <project>`.
5. Check: `aiverse_tests city_plan`, `city_streets`, `city_tiles`; same seed gives the same map on every platform;
   `view_screenshot` and a headless run of the project.

Limits (from the doc): one driving level only (no overpasses, multi-level interchanges, tunnels); buildings are not
rotated and block their bounding rectangle; curved roads have no lane markings; no public transport or railways.

Workflow recipe over existing engine code and MCP tools; no runtime library.
