# Water from splines: straits, rivers, the open sea

Water drawn as splines, like the spline roads (`docs/roads.md`): a strait or a river is a centre line with a width in
every point, the open sea is a shore line from one map edge to another. Every bank is cut into shore segments of
four kinds: **embankment** (quay wall, coping, railing or stone parapet, paved promenade, lamps, benches, bollards,
steps down to the water), **cliff** (a rugged rock face from the water to a grassy top and a back slope, boulders and
foam at the foot), **beach** (sand running under the water, surf, umbrellas, loungers, lifeguard towers) and
**natural** bank (a muddy slope). The engine chooses them by the land behind the water ("auto") or takes them drawn
by hand; a game only gives its own textures and prop models.

| Part | Where |
|---|---|
| Data, built-in shore types, banks, auto shores, cells (float: editor-time / presentation) | `modules/rts/view/RtsWater.{h,cpp}` |
| Look: shore meshes, props, foam | `modules/rts/view/RtsShoreView.{h,cpp}` (drawn by `RtsView` after the roads) |
| Operations (MCP) and baking | `apps/editor/core/WaterOps.{h,cpp}` |
| Editor window «Вода» (Мир) | `apps/editor/windows/WaterWindow.cpp` |
| Tests | `tests/tests_water.inc` |

**The simulation never reads `waterNetwork`.** Every operation bakes the water into the map's cells — `~` where the
water is, `#` under cliffs (blocked), and, when `tiles` names them, the chars of the map's tiles layer — and the
simulation reads those as before. Paths, naval units, bridges and the checksum see ordinary cells. Old maps without
the block load unchanged.

## Shore types (rules `shoreTypes`)

Values in metres; `cellMeters` (default 4) turns them into cells. The built-in library is always there unless
`"builtin": false`: `embankment`, `embankment_stone`, `cliff`, `beach`, `bank`. A game retunes a type by its id or
adds one (`"kind"` or `"extends"`).

```json
"shoreTypes": {
  "cellMeters": 4,
  "embankment": {"wall": {"height": 1.2, "surface": "stone"}, "edge": {"parapet": "railing", "height": 0.9, "post": 3},
                 "promenade": {"width": 6, "surface": "paving"}, "stairs": 120,
                 "props": [{"shape": "lamp", "spacing": 25}, {"model": "models/water-kit/bench.glb", "spacing": 40}],
                 "textures": {"stone": "models/water-kit/tex/quay.png", "paving": "models/water-kit/tex/promenade.png"}},
  "cliff": {"height": [6, 15], "slope": [70, 85], "topWidth": 6, "surface": "rock", "top": "grass", "rubble": true, "foam": true},
  "beach": {"width": [15, 30], "surface": "sand", "surf": true,
            "props": [{"shape": "umbrella", "spacing": 16}, {"shape": "lounger", "spacing": 16}, {"shape": "lifeguard", "spacing": 200}]},
  "bank":  {"kind": "natural", "width": [6, 10], "surface": "soil"}
}
```

- `kind`: embankment | cliff | beach | natural.
- Embankment: `wall` (`height` of the coping over the water, `surface`), `edge.parapet` none | railing | stone,
  `edge.height`, `edge.post` (railing posts, m), `promenade` (`width`, at least 1.5 cells; `surface`), `stairs` (a
  flight of steps down the wall every N m, 0 none).
- Cliff: `height` and `slope` (degrees) ranges, varying along the shore; `topWidth`; `surface` (the rock), `top`;
  `rubble` (boulders at the foot), `foam`. A cliff gets low towards a neighbour of another kind (a stony slope).
- Beach / natural: `width` range, `surface`, `surf`.
- Surfaces: stone, rock, sand, paving, concrete, cobble, grass, soil, gravel, asphalt, wood. `textures` replaces the
  procedural texture of a surface with the game's image (the first type that names a surface wins).
- Props: `shape` (lamp, bench, bollard, umbrella, lounger, lifeguard, boulder, and the road shapes) or `model` (a model
  of the game), `spacing`, `phase` (m from the segment start), `offset` (m from the water's edge; default: the kind's
  place), `scale`.

## The map (`waterNetwork`)

```json
"waterNetwork": {
  "bodies": [
    {"id": 1, "kind": "sea", "side": "south", "shore": [[-1, 100], [60, 97], [165, 88], [193, 99]]},
    {"id": 2, "kind": "strait", "points": [[120, -1, 64], [118, 40, 64], [120, 104, 64]]},
    {"id": 3, "kind": "river", "points": [[30, -1, 26], [34, 40, 28], [36, 104, 34]], "depth": 2.5,
     "shores": [{"side": 1, "from": 0, "to": 120, "type": "embankment_stone"}]}
  ],
  "tiles": {"water": "w", "cliff": "x"}
}
```

- Coordinates in cells, widths in metres. Points are smoothed (Catmull-Rom); a body may run off the map.
- `shores`: `"auto"` (default) or a list of segments `{side, from, to, type}` in metres along the bank; `side` -1 / 1
  is the left / right bank seen along the points (the sea has one bank, 1). Hand-drawn segments are kept, auto fills
  the rest and is written with `"auto": true` (recomputed by every bake).
- `baked`: what the last bake changed (`[cell, old char, old tile]`); the next bake and `water_delete` restore it.

## Auto shores

- **Strait**: embankment on both banks, the whole length.
- **River**: embankment where spline roads, blocked or road cells come within 40 m of the bank (for at least 30 m),
  else a natural bank.
- **Sea**: samples every 10 m look 60 m inland: town → embankment; a headland (the shore bulging 6 m into the water
  against the chord over ±60 m) or the last 50 m at a map edge → cliff; open land → beaches with cliffs between them
  (about one change every 110 m). Segments shorter than 30 m merge into the longer neighbour, equal neighbours merge.
- Where the land side of a bank is water too (a strait running into the sea, the sea shore across a strait's mouth)
  no shore is built there; the shores close with an end there.

## Operations (MCP) and the editor

`water_types`, `water_network`, `water_set {body, tiles?}` (adds or replaces a body by id), `water_delete {id}`,
`water_shores {id, side, from, to, type}` / `{id, reset: true}`, `water_bake`. Every change bakes and returns: water
and cliff cells, changed cells, shore metres and segments per bank (`openMetres`: a mouth), `landParts` with
`separatedLand` (`[x, z, cells]`) and a warning when the land fell apart without a bridge (spline-road bridges join
it). The window «Вода» lists the bodies and their segments, changes a segment's type, resets to auto, deletes, bakes.

## A game: only models and textures

A game does not generate shores itself. Its graphics go into a pack (`models/water-kit/`, textures once, 512 px —
AGENTS.md § Model packs) and into `shoreTypes` overrides: `textures` per surface and prop `model`s. Its maps get
water through the operations or the window.

## Not yet

Wall / parapet modules from the game's models repeated along the bank; waves; water bodies at different heights.
