# Native TerraWeave + NEREID landscape authoring

All new environment generation uses TerraWeave for terrain, plants and rocks,
and NEREID for water. Project recipes choose composition; they must not contain
replacement terrain/noise/water solvers.

`aiverse_terraweave_landscape --recipe recipe.json --out NEW_DIR --materials engine/assets/materials/terrain/textures`
uses TerrainSettings, Earthworks brushes and bakeEarthworksTerrainPatch. Tiles
are 64 metres with 65 samples. The output is ordinary glTF terrain, a shared
PBR pack, vegetation/rock placements and a ground snapshot. This offline export
does not install live terrain editing, collision, ecology or gameplay authority.
Use the existing native WorldScene binding for those capabilities.

Recipe fields: `terrain` (seed, baseHeightCm, reliefCm, basePeriodCm, octaves),
`brushes` (xCm,zCm,radiusCm,depthCm,mode fill/dig), half-open `tiles`
(minX,minZ,maxX,maxZ), `vegetationBounds` in centimetres, `grassPatches`
(as consumed by the vegetation exporter), and the existing `nereidWater` manifest.
Limits: 128 terrain tiles and 64 brushes. Output directory must not exist.
The native vegetation exporter consumes `ground.json` from this output, ensuring
that placement heights and water exclusion derive from the same surface.

`carveNereidRiverbed` in `engine/world/TerraWeaveRiverbed.h` is an integer/Fixed
Earthworks authoring operation. It follows NEREID RiverSpline points/widths,
carves a bottom and smoothly raises the bank to the original ground. Its bounds
are half-open; adjacent chunks do not apply an edit twice. Each call is limited
to 65,536 candidate vertices. It never mutates or simulates a water body.

`NereidManifestHost` reuses the native manifest parser and NereidWaterScene.
Renderbench reads its root `nereidWater`; the native editor reads MapDoc extra
in Scene and RTS Game views. Configure/prepare before beginScene, draw inside
the scene, shutdown before destroying the graphics device. World coordinates
must match terrain asset transforms. Missing manifest clears the prior water.
This is native animated water presentation, not a replacement for the ECS contact,
buoyancy, erosion or persistence pipeline. Existing native WorldScene water stays
with its own host. The renderer supplies water time; renderbench freeze-time does
not freeze water, and its report explicitly records this limitation.

The shared glTF exporter must finish edits to a nested Json member before adding
sibling keys: Json objects store members in a vector, so sibling insertion may
invalidate references. PBR bindings are written in that safe order.
