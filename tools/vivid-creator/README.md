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


## Controlled optical A/B capture cases (Stage 7AE, source only)

The Creator has an opt-in `--optical-case` capture mode. Each case starts
from exactly the same `HumanDNA::preset("neutral")`, uses the existing
static start camera/sun settings, and disables MotionWeave animation,
groom simulation and wind for the capture. Optical capture *requires*
`--smoke` and cannot be mixed with `--preset` or `--asset`.
Normal editing and existing Creator smoke captures are unchanged.

Available cases:

| Case | Only changed DNA scalar |
|---|---|
| `reference` | None |
| `skin-scatter-low` / `skin-scatter-high` | `skin.scatteringMm = 0.10 / 4.0` |
| `eye-ior-low` / `eye-ior-high` | `eyes.corneaIor = 1.25 / 1.45` |
| `hair-rough-low` / `hair-rough-high` | `groom.roughness = 0.08 / 0.90` |

Example (after building on a Mac with a working graphics session):

```bash
AIVERSE_VIVID_CREATOR_CAPTURE=/tmp/skin-low.ppm \
  build/vivid-onepass/aiverse_vivid_creator --smoke \
  --optical-case skin-scatter-low
```

The final `bash tools/vivid-validate.sh --full` also runs the seven
fixed-camera captures and emits:

- `optical-skin-scattering.json`
- `optical-cornea-ior.json`
- `optical-hair-roughness.json`

under `build/vivid-onepass/vivid-validation/`. The RGB comparator
`tools/vivid-creator/optical_ab.py` accepts `--roi` as normalized
`left,top,right,bottom` **within the existing viewport-only crop**, plus
`--label`, `--parameter`, `--reference-value` and
`--candidate-value` metadata. Each case has a **provisional broad region**
that must be calibrated against native Creator screenshots; these are not
object-ID masks or rigorously isolated surface pixels. A difference above
the minimal threshold is a smoke diagnostic, not evidence of physically
accurate scattering/refraction/fiber optics. Verify real screenshot placement
and shader compiler/runtime parity before tightening thresholds.

**Actual verification has NOT RUN** for this stage (C++, Python fixtures,
CTest, Metal, Vulkan, GLES and screenshot pixel comparison). An agent should
check `docs/agent-handoffs/vivid.md` Stage 7AE, then run the deferred
single consolidated acceptance sweep and fix observed issues.


## Material quality profiles (Stage 7AF)

VIVID provides three explicit **presentation-only** material quality tiers.
They do not change versioned `HumanDNA`, MotionWeave, gameplay ECS state or
the imported mesh. The default is **High**, preserving existing source-level
appearance and C++ call signatures.

| Profile | Material graph | Lighting model | Target |
|---|---|---|---|
| Low | Constant pigment/iris-pupil/hair-color graphs; no procedural surface noise | Generic PBR for skin, eyes, cornea, hair and lashes | Mobile/low-end GPUs |
| Medium | Reduced skin Fresnel tint, a single iris grain and hair variation | Existing dedicated Skin/Eye/Hair BRDFs | Balanced desktop/mobile |
| High | Existing full pores/wrinkles, layered iris, dual-lobe hair graph | Existing dedicated Skin/Eye/Hair BRDFs | Desktop/high quality |

Native C++ APIs (old overloads remain High by default):

```cpp
auto skin = aiverse::vivid::humanMaterial(
    aiverse::vivid::HumanSurface::Skin, dna,
    aiverse::vivid::HumanMaterialQuality::Low);
auto character = aiverse::vivid::generateHumanPreview(
    dna, aiverse::vivid::GroomQuality::Medium,
    aiverse::vivid::HumanMaterialQuality::Low);
auto policy = aiverse::vivid::humanMaterialBudget(
    aiverse::vivid::HumanMaterialQuality::Low);
```

The `humanMaterialFromSource` overload retains the imported PBR texture
slots while selecting the quality tier. To preview tiers in Creator, use
the **Material Low / Medium / High** controls or run:

```bash
AIVERSE_VIVID_CREATOR_CAPTURE=/tmp/vivid-mobile.ppm \
  build/vivid-onepass/aiverse_vivid_creator --smoke --material-quality low
```

The source enforces a per-graph budget (Low: at most 24 nodes; Medium:
48; High: 128; texture samples: 0 for current VIVID-authored graphs).
Those limits count the compiled ShaderGraph DAG, **not** the entire
fragment shader's GPU instructions, texture samplers from imported PBR
maps, or the true device cost. In particular the renderer's base PBR
path still runs. GPU cost and the correctness of compiled permutations
on Vulkan/GLES must be measured on real target devices.

The final one-pass macOS acceptance now includes Low and Medium Creator
GPU smoke captures; none has been executed for this source stage.
See `docs/agent-handoffs/vivid.md` Stage 7AF.


## Project presentation profile (Stage 7AG)

The reusable, genre-independent `VividPresentationSettings` type is declared
in `engine/character/vivid/VividPresentationSettings.h`. It is a versioned
**presentation policy**, distinct from `HumanDNA`: project and editor hosts
choose material quality and groom geometry quality without changing character
recipes, authoritative ECS checksums or network saves.

Schema v1 (strict; unknown fields/tiers are rejected):

```json
{
  "version": 1,
  "materialQuality": "medium",
  "groomQuality": "low"
}
```

Examples:
`tools/vivid-creator/vivid-presentation.example.json` (High/High) and
`tools/vivid-creator/vivid-presentation.mobile.json` (Low/Low).

C++ host:

```cpp
#include "engine/character/vivid/VividPresentationSettings.h"
#include "engine/character/vivid/HumanPreview.h"
auto visual = aiverse::vivid::VividPresentationSettings::loadFile(
    "content/vivid-presentation.json");
auto head = aiverse::vivid::generateHumanPreview(dna, visual);
```

Creator GUI now has a **Presentation config** filename and
**Save presentation config / Load presentation config** controls, plus
the existing Material Low/Medium/High and Hair High/Med/Low buttons.
The selected profile is used when rebuilding both procedural preview
and imported material/groom candidates. CLI flags:

```bash
build/vivid-onepass/aiverse_vivid_creator --smoke \
  --presentation-config tools/vivid-creator/vivid-presentation.mobile.json
# Flags are applied in command-line order; later overrides take precedence:
build/vivid-onepass/aiverse_vivid_creator --smoke \
  --presentation-config tools/vivid-creator/vivid-presentation.mobile.json \
  --material-quality high --groom-quality medium
```

Old two-/three-argument preview and material APIs are still available and
default to High. The profile is not automatically read by unrelated game
modules: each host opts in explicitly. Rendering receives the resulting
material and groom models; this is **not** a global GPU-wide
`Renderer3D::setQuality` change. No texture residency/mip settings are
persisted yet, because their canonical implementation is on the parallel,
currently unmerged `feature/texture-rhi-unification` branch. Read
`docs/agent-handoffs/vivid-texture-rhi-integration-2026-11-20.md` before
sharing texture policies or modifying GPU retirement.

The new profile JSON roundtrip/unknown-field tests and the final Mac
smoke capture using the mobile config have been **added but NOT RUN**.
The renderer/Creator and texture branch must undergo real CMake/CTest/
Metal/Vulkan/GLES checks before any tested-compatibility claim.
