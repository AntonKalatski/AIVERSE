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
