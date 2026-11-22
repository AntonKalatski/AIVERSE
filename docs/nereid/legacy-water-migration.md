# Migrating the legacy RTS map water to NEREID (2026-11-22)

## What "legacy water" is

| Layer | Source of truth | Drawn by | NEREID status |
|---|---|---|---|
| Map water cells `~` (`kCellWater`) in `terrain.rows` / heightfield maps | the map (baked cells; the simulation, paths, naval units and checksum read them) | `TerrainRenderer::buildWater` (`modules/rts/view/RtsTerrainRender.cpp`): one **sheet** under the whole map (levels maps, `water.mode` auto/sheet) or one surface per connected **body** at its lowest shore (heightfield maps, `mode: bodies`) | **converted** (this document) |
| `waterNetwork` splines (straits, rivers, sea shores) | map, editor-time | baked into the same `~` cells by `water_bake`; shores by `RtsShoreView` | cells are converted like any others; shore meshes/props stay as they are. No engine template uses `waterNetwork` today. |
| NEREID `nereidWater` block | map extra / scene manifest | `NereidWaterScene` (editor preview, renderbench, streamed worlds) | unchanged |
| 2D tile maps (`tiles2d`) | tile layer art | the 2D tile view draws the water tiles on top | not applicable (no visible change, see below) |

The simulation never reads any renderer: the conversion is presentation only and
never writes cells, `waterNetwork`, saves or the checksum.

## The switch and the converter

- `terrain.water.renderer`: `"legacy"` (default, the old mesh) or `"nereid"`
  (game.json `terrain` block or rules `terrain`, game.json wins per key).
  `AIVERSE_WATER_RENDERER=legacy|nereid` overrides it for A/B checks.
- `terrain.water.nereid`: optional overrides - NEREID surface keys (`lod`, `uvScale`,
  `rippleTileGrid`, ...) and a `style` object (`absorption`, `turbidity`, ...).
  Geometry keys (`shape`, `center`, `halfExtents`, `surfaceY`, `bottomY`, `id`) stay
  converter-owned.
- `TerrainRenderer::legacyWaterLayout` is the single computation of bodies, cells and
  levels; the legacy mesh is now built from it, so both renderers read identical data.
- `legacyWaterToNereidManifest` (`modules/rts/view/RtsWaterNereid.h`) writes a canonical
  `{"nereidWater": {"surfaces": [...]}}` and validates it with the ordinary NEREID parser:
  - sheet -> one `box` over the map plus its 12-cell apron at the sheet height;
  - body -> one `box` over its bounding cells when the terrain hides every other cell of
    that rectangle (ground at all four corners and the centre >= level + 2 cm), otherwise
    the exact disjoint rectangle cover of its cells (`rectangleWaterCells`);
  - style from the legacy tint: deepColor = tint x (0.8, 0.9, 0.95), opacity = tint alpha,
    absorption 1.2, depthTint 0.85, contact foam 0.12 m, no vertex foam.
  - more than 64 surfaces, or a manifest the parser rejects, **fails closed**: the legacy
    mesh is kept and `[water] NEREID conversion refused, legacy water kept: <reason>` is
    logged; editor `geometry_stats.nereidWaterRefused` carries the reason.
- `RtsView` owns a `NereidWaterScene` for the converted surfaces (editor Scene/Game views,
  native client, anything drawing an `RtsView`). It re-parses only when the converted
  manifest changes (editing cells re-runs the build). Editor `geometry_stats` reports
  `waterRenderer`, `nereidMapWaterDraws`, `nereidMapWaterTriangles`.

The same manifest JSON can be pasted into a map's `nereidWater` block if a project wants to
author NEREID water by hand afterwards; the converter does not do that by itself.

## Templates (before/after, native Metal editor, clean Scene captures)

Captured by `tools/editor/nereid_legacy_water_shots.py` (same camera framing the water
cells; legacy = `AIVERSE_WATER_RENDERER=legacy`, NEREID = `=nereid`), then re-captured
with **no override** after switching the template to confirm the config path.
Images: KINGSTON `AIVERSE-builds/nereid-next/migration-shots-v2/` (`<template>-{legacy,nereid}-{near,wide}.png`,
`sheet-near-*.jpg` side by side) and `migration-config/`.

| template | map | water cells | legacy | NEREID surfaces / draws / tris | status |
|---|---|---|---|---|---|
| fourages | highland | 112 | sheet | 1 / 1 / 8 | migrated |
| frostcrown | frozen_pass | 49 | bodies | 1 / 1 / 8 (bounding box hidden by terrain) | migrated |
| hearthhaul | green_dale | 96 | sheet | 1 / 1 / 8 | migrated |
| ironfront | ore_valley | 224 | sheet | 1 / 1 / 8 | migrated |
| km2d | hills | 28 | sheet (3D view) | 1 / 1 / 8 | migrated |
| lua_lab, scripted, showcase, skirmish, wildfire | valley / forest | 160 each | sheet | 1 / 1 / 8 | migrated |
| tiles2d | dunes | 34 | 2D tile art on top | 1 / 1 / 8 under the tiles | **kept legacy**: images identical, nothing to migrate |
| lastcolony | horde_plain | 585 | sheet | - | **kept legacy**: the editor did not open this project in the harness (map_open: JSON error on the project), not verified |

Config path check (no environment override, templates switched in `game.json`):
`geometry_stats.waterRenderer` = `nereid` with 1 NEREID map-water draw in all ten migrated
templates, `legacy` with 0 in tiles2d. Mean per-pixel difference (0-255 grey) of the
config capture against the `=nereid` capture 0.33-0.34 (wave animation between runs),
against the legacy capture 1.39-3.13 (fourages, frostcrown, skirmish sampled).

Total draw calls per frame were identical in both modes in every captured template (one
legacy water draw replaced by one NEREID draw). What changes visually: the water is the
NEREID material (depth absorption over the dug cells, thin contact foam at the bank,
animated waves, reflections where the quality tier enables them) instead of a flat tint;
footprint and level are the legacy ones.

## After merging origin/main (75b3e290d)

Rebuilt editor, client, renderbench and tests; CTest ocean_lod, rts_water_nereid,
renderer_contract, surface_mesh, flow_map, river_grade 6/6 PASS; Metal ocean acceptance
PASS again; config captures of skirmish, frostcrown (nereid, 1 draw) and tiles2d (legacy)
repeated (`migration-postmerge/`).

## Not done / not verified

- Web (WASM) client and Studio HTML5 builds were not rebuilt; GL/Vulkan/mobile pixels not
  captured. Native client (`aiverse_client`) compiles and links with the change; a played
  match was not captured.
- Heightfield maps with several bodies at different levels: only covered by the unit test
  (exact rectangle cover, level per body); no engine template has one.
- Secondary LENS cameras (`drawPreparedTerrainFog`) do not draw the converted water yet.
- Underwater PRISMA for RTS cameras is not wired (RTS cameras stay above water).
- The legacy path stays the default; remove it only after external projects are verified.
