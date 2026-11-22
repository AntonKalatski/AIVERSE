---
name: aiverse-planets
description: Make AIVERSE planet and orbit battle maps - world bodies, planet generation presets, shared or separate layouts, orbit fleets of bots, checksum safety for flat maps.
---

# Planets and orbits

Contract: `docs/planets.md`. A map without `world` / `generate` behaves exactly as before.

1. Bodies: the map's `world.bodies` (star, planet, moon: id, kind, radius, orbit, visual) via `map_set`; `planet_info` reports what the engine sees.
2. Generate: `gen_presets` -> `map_generate {preset: rocky|ocean|moon, players, params}`; `params.layout` `shared` (all players on one body, polar rotational symmetry) or `separate` (an identical planet each); moons get `params.moonResources`. Read the answer: starts, path lengths, connectivity.
3. Heights come from the generator, not from hand-made formulas: a height function built from bands
   (`abs(z)`, latitude rings) gives straight-edged lowlands that the sea fills as squares on the cube faces.
   Bake a static map with `aiverse_headless --project <dir> --generate rocky --seed N --players 2 --gen-out <map>`
   (the project's first map supplies the bodies; `templates/planets/maps/big_planet.json` is made this way).
   The ocean is cut per pixel where the ground is above `seaLevel`; coasts follow the heights at any LOD.
4. Cube-sphere terrain: relief and materials with the heightfield tools; check seams between faces on screenshots (`view_focus` on edges).
5. Bots: `rules.ai.invasionSize` sets the orbit fleet; bots build orbital yards when the enemy is reachable only through orbit. `match_run` must show transfers between bodies and a winner (`skills/ai/aiverse-bots-debug` otherwise).
6. Determinism: same seed -> same checksum; `templates/` checksums unchanged when engine code is touched.

Runtime library: none. Workflow over the engine MCP map and planet tools.
