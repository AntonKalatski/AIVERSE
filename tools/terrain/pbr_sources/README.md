# Source provenance

Built-in image_gen (not API CLI), 2026-11-20. Five distinct base-color requests: meadow grass,
loamy soil, fine dune sand, weathered limestone, packed granular snow. Prompt constraints:
orthographic top-down, seamless all edges, evenly lit diffuse base color, no cast shadows,
no perspective, no objects/text/borders, restrained natural palette, high fine detail.
Each source was separately referenced to author a matching grayscale microrelief height map,
raised details light and crevices dark, same layout/scale, no illumination or albedo color.
The output is an authored approximation, not physical measurement. Sources are preserved here.
Checkerboard is mathematically generated (8x8 cells, flat normal, AO=1, metallic=0, roughness=.68).
Baking uses periodic-plus-smooth boundary correction and periodic height derivatives for +Y normals.
Roughness and local cavity AO are tuned approximations from the authored height, never claimed scanned.
Run: python3 tools/terrain/bake_pbr_pack.py (numpy + Pillow).

2026-11-20 expansion: forest_floor, wet_soil, gravel_soil and snow_crust
were generated as aligned left-albedo/right-height source pairs using the
built-in OpenAI image generator. Original `*_source_pair.png` files are
preserved; halves are extracted losslessly before deterministic runtime baking.
These are authored approximations, not measured scans. Grass baking removes
broad source colour drift before tiling to avoid repeated distant brightness bands.


2026-11-21 river_sand and pebble_sand: built-in image_gen source pairs, not API
CLI. Full original left-albedo/right-grayscale-height sheets are preserved as
*_source_pair.png; halves extracted without resizing, runtime maps baked1K/4m.
Prompt set: overhead orthographic4m square, seamless all edges of EACH aligned
left/right square; unlit diffuse mineral sand/compact sand with sparse2–15mm
quartz grit on left and matching grayscale height on right; subtle damp grey
beige/brown, no waves/dunes/stripes/directional grooves, shadows, highlights,
plants, footprints, text, borders or perspective. First pair fine sand, second
sparse embedded pebble sand. Height alignment/roughness/AO are authored artistic
approximations, not measured or scanned surface properties. Tool image output
was1774x887; halves887px before deterministic1024px runtime resampling.
Files generated with exec-32d50571-a834-44ab-bea7-17fab120447e and
exec-bf64098b-cd88-4922-8178-6b093feb0a0c respectively.
