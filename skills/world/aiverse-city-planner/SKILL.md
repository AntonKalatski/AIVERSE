---
name: aiverse-city-planner
description: Generate a city map for an AIVERSE top-down game with the map generator preset city / metropolis - street hierarchy, river and bridges, roundabouts, zoned blocks, parks, car parks, street life, organic suburbs, relief, exact road tiles - and use the map's city plan block for traffic and gameplay.
---

# city-planner

Behaviour and format: `docs/city-planner.md`. Code: `modules/rts/gen/GenCity.cpp` (integer maths; same request, same map). Tests: `aiverse_tests city_plan`, `city_streets`, `city_tiles`.

1. Rules first: buildings are structures tagged `building` (+ zone tags `residential` / `commercial` / `industrial`), `tree` / `bush`, `lamp` / `prop`, `pedestrian`, vehicles from `driving.vehicles`, the hero from `topDown.heroes`. Missing kinds are skipped and listed in `generated.warnings`.
2. Generate: `gen_presets`, then `map_generate` with preset `city` (grid) or `metropolis` (`"layout": "organic"`: downtown grid, avenues, ring roads, suburbs, countryside, `relief`); or at match start (`"generate": {"preset": "city", "seed": "match"}`); or bake with `aiverse_headless --generate city --gen-out map.json`.
3. Tiles: params `tileset` and `tileBases` for exact artwork (straights, crossings `x1`..`x15`, T-junction groups, street ends); a base can be a list of variants picked by hash.
4. The plan stays in the map as `"city"`: `roads`, `junctions`, `bridges`, `blocks`, `river`, `paths` (curved roads), `parkingAccess`, `curbParking`. Traffic (`skills/gameplay/aiverse-city-traffic`) and a game's own plot tools read it; do not re-derive roads from tiles.
5. For spline roads instead of tiles see `skills/world/aiverse-roads`; water bodies: `skills/world/aiverse-water`.
6. Check: `map_validate`, a headless match with `--seed 3`, screenshots of a junction, a bridge and a car park.

Limits: one level (no overpasses), unrotated buildings, no public transport lines. Runtime library: none (engine generator).
