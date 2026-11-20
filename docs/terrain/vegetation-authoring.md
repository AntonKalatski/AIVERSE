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

## Tufts, shore masks and camera/shadow LOD

GrassRenderQuality now appends bladesPerTuft (1..32, default1 preserves old
placement) and tuftRadiusCm (1..100). Tufts share stable centres; each blade keeps
its independent width/height/yaw. Density remains maximumBlades per patch, not
one ECS entity per blade. The exporter uses16 blades/tuft, radius15cm and exports
three stable density tiers (full, one-third, one-eighth) using native grass APIs.
The optional ground snapshot waterHeightCm grid rejects roots below270cm above
the local water surface, matching the continuous sand transition. Missing field
preserves existing input compatibility. A 64m² patch can request8192 blades;
exported triangle counts are asset budgets, not visible frame submissions.

For explicit rigid vegetation LODs, the generic model sidecar can opt into:

```json
{"autoLod":false,"independentShadowLod":true}
```

Camera geometry then uses projected main-view extent and dithering; a separate
shadow-only packet uses the shadow projection without camera fade. Existing
variants, node-material overrides, deformation and geometry policies retain their
legacy eligibility gates. Ordinary models default to the prior shared maximum.
The opt-in path bypasses experimental immutable CPU packets and GPU LOD, keeping
those paths unchanged rather than replaying a packet with incompatible selections.
High-resolution shadows no longer hold all visible vegetation at LOD0.

Native editor diagnostics can be enabled with AIVERSE_LOD_DIAGNOSTICS=1;
geometry_stats returns cameraLodNodes, lodChains, lodBlocked and shadowLimitedLod.
Renderbench reports the same main-view counters. Counts describe source mesh
nodes, not draw calls, triangles, GPU time or measured performance improvement.


## Dense clump presentation and stable sampling

The exporter now defaults to65cm blades,45% height variation and24cm tuft radius;
per-patch bladeHeightCm, bladeHalfWidthCm, bladesPerTuft, tuftRadiusCm and
heightVariationPermille can override these within GrassRenderQuality bounds.
Coverage and water exclusion remain supplied by the ground snapshot. Increasing
height does not populate empty coverage or replace a missing host grass patch.

Native grass LOD retains the same seeded roots and tuft centres, including when
the presentation budget is below the logical population. Medium/far retain fewer
blades inside every tuft, widen those blades by sqrt(sample stride), and simplify
ribbon segments. This conserves approximate projected cover, not exact pixels.
sourceSamples/sampleStride are presentation-only sampling controls used by the
shared LOD builder; legacy full-detail placement with bladesPerTuft1 is preserved.
Visual transitions and grass shadows/wind/species need separate acceptance.
