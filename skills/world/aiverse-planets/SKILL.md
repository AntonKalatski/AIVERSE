---
name: aiverse-planets
description: Make AIVERSE planet and orbit battle maps - world bodies, planet generation presets, shared or separate layouts, orbit fleets of bots, checksum safety for flat maps.
---

# Planets and orbits

Contract: `docs/planets.md`. A map without `world` / `generate` behaves exactly as before.

1. Bodies: the map's `world.bodies` (star, planet, moon: id, kind, radius, orbit, visual) via `map_set`; `planet_info` reports what the engine sees.
2. Generate: `gen_presets` -> `map_generate {preset: rocky|ocean|moon, players, params}`; `params.layout` `shared` (all players on one body, polar rotational symmetry) or `separate` (an identical planet each); moons get `params.moonResources`. Read the answer: starts, path lengths, connectivity.
3. Cube-sphere terrain: relief and materials with the heightfield tools; check seams between faces on screenshots (`view_focus` on edges).
4. Bots: `rules.ai.invasionSize` sets the orbit fleet; bots build orbital yards when the enemy is reachable only through orbit. `match_run` must show transfers between bodies and a winner (`skills/ai/aiverse-bots-debug` otherwise).
5. Determinism: same seed -> same checksum; `templates/` checksums unchanged when engine code is touched.

Runtime library: none. Workflow over the engine MCP map and planet tools.
