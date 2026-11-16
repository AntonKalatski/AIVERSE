# Navigation mesh (`engine/nav`)

A genre-independent navigation mesh: one header, `engine/nav/NavMesh.h`, Fixed / integer math only, no RTS, FPS or
other gameplay types. Modules describe walkability; the mesh does the rest.

| user | source of walkability | entry point |
|---|---|---|
| RTS units (`modules/rts`) | passable cells of a path layer, links = `canStep` both ways (cliffs, ramps, slopes) | `MapGrid::findPath` with `movement.pathfinding: "navmesh"` (docs/pathfinding.md) |
| FPS bots (`libraries/.../bots/NavMesh.h`) | supported floor spans under a clear body from AABB collision, links = motor step + body sweep | `buildNavMesh`, `navPath`, `navStep` (docs/bots.md) |
| anything else | implement `nav::Source` | `nav::Mesh::build`, `findPath` |

## Model

- **Columns and spans.** The world is a raster of `width x depth` columns (`Config::cell` world units, corner at
  `originX/originZ`). `Source::spans(x, z)` lists the walkable surfaces of a column (ascending height, stacked floors
  allowed, an `area` class each). `Source::link(x0, z0, i, x1, z1, j)` says whether an agent crosses between span `i`
  and span `j` of an orthogonal neighbour; it must be symmetric.
- **Rectangles.** Linked spans of one area (and one height with `flat`) merge greedily (+x, then whole rows along +z)
  into rectangles. Rectangles never cross `tile` borders (default 32), so a change rebuilds only its tiles.
  Every point of a rectangle is walkable and every straight line inside it is too.
- **Portals.** For every side of a rectangle: runs of linked crossings to one neighbour rectangle. Each end records
  whether a wall touches that corner (`solid`) and on which side the wall continues (`pushLo/pushHi`).
- **Ids.** Rectangle ids are dense in tile order and stay valid until a refresh rebuilds tiles; `digest()` hashes the
  content (tests compare incremental refreshes with fresh builds).

## Queries

`findPath(from, to, Query, out, corridor)` returns `from, corners..., to`:

1. `locate` both points (column, then the span nearest in height within `yTolerance`).
2. A* over rectangles; the cost to enter a neighbour is the distance to the portal's crossing point (where the straight
   line to the goal meets the portal, clamped to it), scaled by `areaCost[area]`; heuristic = straight distance.
   `maxVisits` bounds the rectangles expanded. Ties by rectangle id.
3. Funnel (string pulling) over the portals of the corridor; portals shrink by `margin` at wall corners (never past
   their middle). Corners that would run along a portal line over a wall stretch step off it by up to half a cell,
   checked exactly against the corridor rectangles.

False: a point off the mesh, no connection, or the budget ran out. Callers keep their own fallback (RTS: grid A*).

## Updates

`markDirty(x0, z0, x1, z1)` marks the columns whose spans or links may have changed (include the ring of columns whose
links to them change); `refresh(source)` rebuilds the dirty tiles and the portals of their 3x3 tile neighbourhood.
`locate` / `findPath` refuse a dirty mesh, so callers refresh first (RTS does it in `MapGrid::navFor`).

## Determinism and limits

- Fixed math, fixed iteration order, ties by ids; results are a pure function of the source (refresh == build).
  Meshes are derived data: never hash or save them, rebuild from the same source.
- Rectangular cells only: no slopes inside a span, no triangle-collision import, no off-mesh links (jumps, ladders),
  no streaming between separately built regions. Corner clearance is a margin, not an erosion of the walkable area:
  narrow passages stay open and a path may graze a wall corner it does not turn at.
- `maxSpans` (default 2^24) fails a build explicitly instead of truncating it.
- Measured on a VM with random obstacles: 256x256 build 4 ms, 1024x1024 build 86 ms, refresh after a 3x3 change 0.3 ms,
  query 0.05 ms (256²) / 0.7 ms (1024²).

Tests: `navmesh_core_*`, `navmesh_rts_*` in `tests/tests_navmesh.inc`; FPS: `aiverse_library_navmesh_tests`.
