# VIVID Creator — Native digital human authoring

VIVID Creator is a standalone, genre-neutral C++20 tool built on AIVERSE
Renderer3D, its existing ShaderGraph PBR materials and MotionWeave node poses.
It does **not** bundle or decode Epic MetaHuman DNA or third-party scans.

## Build and open on macOS

```bash
cd ~/Projects/AIVERSE-vivid
git fetch origin feature/vivid-digital-humans
git switch --detach origin/feature/vivid-digital-humans
cmake -S . -B build/vivid-onepass -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vivid-onepass --target aiverse_vivid_creator -j 4
build/vivid-onepass/aiverse_vivid_creator
```

The Creator opens a reference bust with layered sclera, iris, cornea and
eyelids, real ShaderGraph skin/eye/hair materials, corrective face morphs,
simple cloth-like hair-guide/card motion and MotionWeave motion.

Use the **Face and body**, **Rig**, **Skin**, **Eyes** and **Hair** sections
to edit DNA. Undo/redo and Save/Load JSON operate on a versioned DNA recipe.
Editing an external artist mesh does **not** silently deform its authored
topology; the numerical anatomy controls apply to the procedural reference.

## Open a user-authored glTF/FBX human

From Creator, enter a package manifest path and click **Import
glTF/FBX/OBJ + DNA + morphs**, or run:

```bash
build/vivid-onepass/aiverse_vivid_creator \
  --asset /absolute/path/to/human-package/manifest.json
```

The package manifest is an open JSON file, for example:

```json
{
  "version": 1,
  "model": "my-character.glb",
  "dna": "my-character.vivid.json",
  "morphs": "my-facial-shapes.json",
  "materials": {
    "Face_Skin": "skin",
    "Eye_Iris": "iris",
    "Eye_Sclera": "sclera",
    "Eye_Cornea": "cornea",
    "Hair_Cards": "hair"
  },
  "routes": [
    {"channel": "jawOpen", "target": "jawOpen", "gain": 1.0},
    {"channel": "eyeBlinkLeft", "target": "eyeBlinkLeft", "gain": 1.0},
    {"channel": "eyeBlinkRight", "target": "eyeBlinkRight", "gain": 1.0}
  ]
}
```

The three referenced file paths must be **relative to the manifest's folder**
and cannot contain `..`. The loader uses the AIVERSE model import pipeline;
GLB/glTF can include PBR textures and skeletons, FBX needs the optional
`ufbx` build, OBJ supports a morph-only root/head reference without skinning.
Imported image/albedo, normal, metallic-roughness, AO and emissive texture
indices are preserved when installing VIVID skin/eye/hair BRDFs.

The optional `morphs` file is in source **model-relative vertex index order**.
VIVID validates that original vertex positions survive preparation and fails
rather than animating the wrong vertices:

```json
{
  "version": 1,
  "vertices": 12345,
  "targets": [
    {"name": "jawOpen", "deltas": [
      {"vertex": 120, "position": [0.0, -0.02, 0.004]},
      {"vertex": 121, "position": [0.0, -0.02, 0.004]}
    ]}
  ]
}
```

The `dna` file is optional. It uses `HumanDNA::toJson()`; user-facing
Creator Save DNA JSON produces one. The default DNA rig expects a `root`
node and a `head` descendant. Jaw/eye joint aliases are editable and
optional for morph-only assets. The rest of the skeleton belongs to
MotionWeave, which handles imported clips, retargeting and IK. Without a
usable rig hierarchy, the import fails explicitly.

For a runnable code-only example use
`tests/vivid/assets/import-head.manifest.json` together with its tiny
original OBJ and facial morph JSON. It is not a realistic human scan.

## One-pass acceptance

```bash
bash tools/vivid-validate.sh
```

This compiles VIVID and MotionWeave, runs CPU/native Metal CTests, verifies
a single and 12-actor facial GPU preview, and launches both procedural Creator
and imported-head GUI captures. All results and diagnostic PPMs are saved to
`build/vivid-onepass/vivid-validation`. Run on a Mac with an active GUI Metal
session. CTest results do not replace artist review of skin, hair, eyes or
shadows.

## Known production limits

- GPU facial deformation, per-character outputs, shared immutable GPU CSR
  data and token retirement are proven in the prior VIVID GPU stage.
- Skin scattering and hair strand reflection are practical **BRDF
  approximations**, not physically measured spectral multi-layer scattering.
- Procedural preview geometry is a bust/reference, not a photogrammetry scan.
- Card groom collision uses scalp proxies. It does not simulate strand-to-strand
  collisions, full-body clothing contacts or sophisticated cinematic grooming.
- A scanned human needs properly authored models, textures, eye meshes,
  blendshape deltas, motion clips and rig metadata. The open importer supports
  this, but no proprietary/labeled artist assets are distributed.
- The shader changes and full GUI import/smoke tests added in the new Human
  stage must still pass actual Apple Metal compilation and visual testing.
- High-count human skin/hair benchmarks, Vulkan GPU parity and the full engine
  regression are separate release gates.
