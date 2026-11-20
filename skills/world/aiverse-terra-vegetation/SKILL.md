---
name: aiverse-terra-vegetation
description: Build AIVERSE trees, grass, rocks and forest scenes through native TerraWeave Arbor/Flora and shared PBR/LOD APIs; diagnose sparse cover, repeated silhouettes and disconnected foliage before writing custom vegetation.
---

# Native Terra vegetation

Use with `skills/workflow/aiverse-terra-first/SKILL.md`. Determine whether the visible
asset came from Terra or a scene-local placeholder before changing parameters.

## Trees

Reuse `engine/world/TerraWeaveArbor.h`, `engine/world/TerraWeaveArborMesh.h` and
`engine/render/TerraWeaveModelAdapter.h`. Cache bounded shared prototypes with varied
seeds/profile dimensions; do not generate a unique mesh for every instance or frame.

For the supplied 2x2 conifer spray atlas, opt into ArborMeshSettings naturalConifer
and choose sprayPairs (1..12). It places sprays along curved structural branches,
with stem ends toward their owner. Use doubleSidedLeaves=false with a double-sided
alpha-masked material to avoid duplicated back-face triangles. Supply shared bark
and foliage PBR maps; the adapter's opaque fallback is not finished foliage.

The whole-model adapter preserves the High skeleton through all three natural-conifer
LOD tiers, reducing tube/spray detail. The per-branch adapter retains its existing
independent branch LOD. Neither is proof of visually seamless transitions. Existing
phenology, age and ecology owners remain authoritative; cosmetic mesh changes must
not silently rewrite seeded skeleton/save state. Ten parameterized botanical recipes are now available in
engine/world/TerraWeaveTreePresets.h and the exporter --catalog botanical. Read
docs/terrain/vegetation-authoring.md for age/winter and shared PBR families.
Recipes and leafless winter are implemented; seasonal snow coating, final art
acceptance and smooth root/branch junction welding remain unfinished.
For live ForestScene register the unscaled profile and let existing instance
age/ecology size it once; offline treeProfileAtAge is for exports only.
Larch has explicit Deciduous canopy habit despite its conifer crown shape.

## Grass

Use `engine/render/TerraWeaveGrassRender.h` over existing aggregate GrassPatch state.
Choose patch area AND maxBladesPerPatch: a cap of 1024 over a large field is sparse,
regardless of the logical population. bladeSegments (1..4), bladeHeightCm (4..150)
and bladeHalfWidthCm (1..12) control bounded bent ribbons. Density must be evaluated
per square metre and visually at the gameplay camera. Include bare paths and varied
patch coverage rather than a uniform carpet. Use makeGrassPatchLodModel for resident/exported chains: it preserves tuft centres,
reduces blades within each tuft and compensates width, rather than re-scattering
all roots at each level. Exporter patch fields can tune bladeHeightCm,
bladeHalfWidthCm, bladesPerTuft, tuftRadiusCm and heightVariationPermille.
No per-blade ECS entities or draw calls.

Use the optional GrassPlacementFilter in CPU generation for water/road exclusions;
the live GrassPatchRenderer still requires the host to supply suitable patch state.
Do not claim the offline mask was installed into native streaming. Regenerate only
affected patches after terrain/coverage changes, with the owning RHI lifecycle.

## Rocks and offline export

Use `engine/world/TerraWeaveRocks.h`, `engine/render/TerraWeaveRockModel.h` and
`docs/terrain/rocks.md`. Reuse native scatter/height support, seeded variants and LOD.

Host tool aiverse_terraweave_vegetation exports the SAME native Arbor/grass models;
see `docs/terrain/vegetation-authoring.md` for arguments and the bounded ground-grid
schema. aiverse_terraweave_rocks exports native rocks. Both refuse existing output
directories. Keep PBR maps external and shared once per pack; preserve glTF rtsLods.
The grass fallback is a dielectric rough PBR material, not a textured blade atlas.

## Verify

Run the relevant TerraWeave model/arbor/rocks tests and build the exporter. Inspect
actual native close/mid/far images, alpha silhouettes, grounding and material scale.
Report tested backend and triangle/instance counts without claiming GPU speedups.
Keep renderer-owned LOD, bounded CPU generation and per-stage handoffs.

Runtime library: none. Workflow over existing native TerraWeave APIs and host tools.
