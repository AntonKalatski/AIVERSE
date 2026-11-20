# Native vegetation authoring

Use TerraWeave first: [world workflow](../../skills/workflow/aiverse-terra-first/SKILL.md)
and [vegetation workflow](../../skills/world/aiverse-terra-vegetation/SKILL.md).

The optional `ArborMeshSettings::naturalConifer` mode extends the existing Arbor
mesh builder with curved bark centre lines and branch-attached needle sprays.
It requires a 2x2 stem-at-bottom needle atlas. The authoritative integer skeleton
is unchanged. Whole-tree LOD preserves the same High skeleton while reducing
radial and spray detail; independent BranchLOD remains supported. Normal maps
receive generated tangents. The option defaults off for old profiles.

Grass uses the existing aggregate `GrassPatch` model: bounded `bladeSegments`,
`bladeHeightCm`, `bladeHalfWidthCm` and seed-owned yaw/bend. The optional CPU
placement predicate accepts signed centimetres and excludes roads/water without
creating blade entities. Existing live residency owns patch invalidation/upload;
the CPU predicate is not automatically installed into that host.

## Host exporter

Build target `aiverse_terraweave_vegetation`. Example:

```sh
aiverse_terraweave_vegetation --out /new/forest-pack \
  --materials /engine/engine/assets/materials/vegetation/textures \
  --seed 2718 --ground /project/ground.json
```

Output directory must not exist. Twelve native conifer prototypes (spruce/pine
profiles), shared external PBR textures, catalog and optional grass patch assets
are exported through the regular model exporter. Grass is an opaque dielectric
rough PBR material with vertex colour, not a texture atlas. Keep accompanying bin
files and texture directory. Reuse prototypes through instances.

Optional ground JSON is an authoring snapshot, not a new terrain/save format:
originXcm/originZcm signed integers, stepCm 1..1000, columns/rows 2..2048;
heightCm row-major signed centimetres and coverPermille row-major 0..1000 arrays,
each exactly columns*rows. Up to 256 patches contain minX/minZ/maxX/maxZ in cm
and blades 1..8192. Patches must lie inside the supplied grid. Sampling is bilinear;
for a triangle heightfield the host must provide matching samples/contact handling.
Coverage masks exclude water and let the host author bare areas. No water ownership
or scene-specific height function is added to the engine.

## Limits and validation

Exported conifers are presentation assets; this does not install live native
streaming, ten species, seasonal snow, root junctions or physics. Current procedural
sprays remain cards, not individual needle geometry. Near/mid/far transition quality
must be inspected in the intended game, and prototype count/density tuned to budget.
Tests extend `terraweave_model` for stable generation, decreasing LOD budgets,
bounded grass, grounding and exclusions; run `terraweave_arbor` for legacy parity.
See `AGENT_HANDOFF.md` for commands actually run and backend coverage.
