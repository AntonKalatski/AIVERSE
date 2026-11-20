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


## Renderer3D / game-host material quality bridge (Stage 7AH)

The shared renderer already owns `render::MaterialQuality` and
`Renderer3D::setMaterialQuality`. VIVID does **not** add a second
renderer-wide quality switch. The header-only
`engine/character/vivid/VividRendererQuality.h` supplies a host adapter.
It never changes HumanDNA, gameplay ECS or texture residency budgets.

An ordinary project/game host can keep its own render quality policy and
resolve a *snapshot* for each generated human asset:

```cpp
#include "engine/character/vivid/VividRendererQuality.h"
#include "engine/character/vivid/HumanPreview.h"

// Call after initializing a Renderer3D and deciding its quality.
const auto rendererTier =
    aiverse::render::Renderer3D::defaultMaterialQualityFor(device, mobile);
renderer.setMaterialQuality(rendererTier);
const auto projectVisuals =
    aiverse::vivid::VividPresentationSettings::loadFile(
        "content/vivid-presentation.json");
const auto effective = aiverse::vivid::resolveVividPresentation(
    projectVisuals,renderer.materialQuality());
auto character = aiverse::vivid::generateHumanPreview(dna,effective);
```

`resolveVividPresentation` never increases `materialQuality` beyond the
current Renderer3D tier, preserving independent `groomQuality` so the host
can budget CPU guide simulation and hair geometry separately. A renderer
quality change requires the host to rebuild/rebind affected Material and
Model GPU state at a safe frame boundary; it cannot mutate existing in-flight
models. Normal gameplay ECS and MotionWeave animation remain untouched.

The standalone Creator intentionally keeps its material graph tier and
Renderer3D tier in sync via `applyVividMaterialQuality(renderer,presentation)`
on initialization and on a subsequent render-frame-boundary quality change.
The engine renderer already retires old pipelines through its normal path.
The VIVID model candidate still uses `HumanRetirementQueue` for GPU-safe
old-actor/resource retirement. Game hosts that do not use VIVID are unaffected.

This bridge is **not** Texture RHI integration. The texture agent still owns
compressed KTX2/DDS formats, samplers, mip residency, streaming, VT and GPU
material resource identity. Read
`docs/agent-handoffs/vivid-texture-rhi-integration-2026-11-20.md` before
merging. No Vulkan/GLES/Metal runtime validation or performance benchmarking
has been run on this stage's sources.


## Sculpt source IDs through palette seams and authored LODs (Stage 7AK)

An imported character's palette-seam repair may duplicate model-global
vertices. A visible triangle can therefore reference an **appended clone**,
even though the morph authoring sidecar still addresses the source vertex.
The Creator now publishes a per-model original-source-ID table alongside
its new model and morph CSR:

- Original `0 .. sourceVertexCount-1` IDs map to themselves.
- `SkinSeamSplit::Result::clones` map each actual appended seam clone to its
  original authorable ID.
- Authored/generated LOD vertices and seam clones **of those LOD vertices**
  map to the invalid `UINT32_MAX` sentinel; sculpting cannot accidentally
  overwrite them.
- Wrong source/target indices and repeated clone targets reject the new
  mapping transactionally. A rejected candidate cannot replace the previous
  active selection map.
- The raycast traverses real morphed+skinned triangles at their model IDs,
  then converts the chosen hit vertex to its source ID *after* intersection.
  Its world-space marker uses the actual chosen clone, not a bind-pose guess.

Core opt-in functions are `buildSculptSourceVertexMap` and
`pickDeformedSculptVertex(..., sourceVertexCount, authorIds)` from
`engine/character/vivid/DeformedSculptPick.h`.
The lower-level `pickSkinnedWeightVertex` accepts the same optional map;
existing skin-weight painting calls remain unchanged.

This is a bounded **on-click CPU reference**: above 1,000,000 model vertices,
Creator still loads/renders the character but disables this sculpt picker.
The old model-independent `pickSculptVertex` API remains available for
standalone tools; it does not model skinning or morphs. A full Vulkan/Metal/
GLES pixel-perfect ID/depth pass, inter-mesh occlusion, cloth-aware picking,
GPU readbacks and acceleration for million-triangle meshes remain unbuilt.
**Actual native C++ build, CTest, Creator GPU capture and performance tests
have NOT RUN for Stage 7AK.** See the chronological agent handoff.


## Reusable BVH sculpt selection (Stage 7AL)

The import sculpt picker now owns a persistent
`DeformedSculptWorkspace` (`engine/character/vivid/DeformedSculptWorkspace.h`).
It keeps reusable weights, morphed bind positions, a scratch CPU-skinned
world surface and a per-selected-mesh topology BVH. Unlike the original
brute-force `pickDeformedSculptVertex` reference, it:

- Builds a deterministic median-partitioned triangle BVH on the **first**
  click for an imported model/mesh. It does not build one each render frame.
- Refits all leaf/internal boxes to the **current** morph-first, skin-second
  world-space positions for every click; model topology stays immutable.
- Intersects only candidate triangles that survive ray/AABB pruning,
  retaining original primitive-triangle order as a tie-breaker.
- Keeps Stage 7AK author ID mapping: seam clones resolve to the source
  vertex ID, while LOD/generated vertices stay nonauthorable.
- Reuses buffers after warm-up. `SkinnedWeightScratch` also keeps a
  first-primitive index per model vertex instead of allocating a separate
  palette vector for each vertex.
- Stages morph and skin outputs transactionally: on an invalid weight,
  palette or pose, existing last-good buffers are untouched.
- Emits candidate/tested triangle counters in the Creator's pick status,
  which are diagnostics **not** equivalent to CPU/GPU timing benchmarks.

For a standalone tool:

```cpp
#include "engine/character/vivid/DeformedSculptWorkspace.h"
aiverse::vivid::DeformedSculptWorkspace picker;
auto result = picker.pick(model,meshId,nodeWorld,publishedMorphs,
                          faceBinding,faceFrame,rayOrigin,rayDirection,
                          originalVertexCount,authorIds);
const auto stats = picker.stats(); // total and narrowphase triangle counts
// On new model publication or any in-place mesh topology edit:
picker.reset();
```

The full CPU morph and skin reconstruction and BVH box refit remain
**O(model vertices + mesh triangles) per click**; only ray narrowphase
is accelerated. BVH build is amortized across repeated clicks, not
eliminated. The CPU editor budget remains 1,000,000 vertices and
1,000,000 selected-mesh triangles. Visible-surface correspondence can
still differ from actual GPU depth due to inter-mesh occlusion, cloth,
GPU floating-point differences or unsupported deformers. We have **not**
implemented GPU ID/depth rendering here.

**Actual full C++ build / native CTest / Creator GPU screenshots /
Metal / Vulkan / GLES tests / performance benchmarks are NOT RUN on
this stage's new source.** The final macOS one-pass acceptance remains
`bash tools/vivid-validate.sh --full` after the remaining functions.


## Cached skinning and opt-in sculpt benchmark (Stage 7AM)

The persistent editor `DeformedSculptWorkspace` now also owns
`SculptSkinningPlan` (in
`engine/character/vivid/SculptSkinningPlan.h`). First use validates
model/mesh ownership, bone palettes, skin influence slots and inverse-bind
matrices, building one immutable per-vertex influence plan. Every
subsequent user click evaluates **one combined world × inverse-bind
matrix per skin joint** rather than rebuilding the triangle palette
lookup and each vertex's palette on every ray. The vertex positions
still follow VIVID's morph-before-skin pipeline and the same refitted
BVH from Stage 7AL. Both source-ID and seam-clone rules remain unchanged.

- `build(model,meshId)` is transactional; failures do not overwrite an
  existing plan. No GPU resources or ownership transfer is involved.
- `evaluate(model,nodeWorld,morphBind,output,staging)` preserves the
  last-good surface on an invalid pose, nonfinite morphed bind position,
  or missing skin joint. No special case allows picking against a
  stale model when the presented pose is invalid.
- `reset()` on the workspace invalidates both BVH topology and skinning
  plan after **every successfully published character candidate**. If an
  external host changes skin weights, bone palettes, inverse binds or
  Model topology *in place* it must call `reset()` before picking.
- The existing generic `prepareSkinnedWeightSurface` and
  `pickDeformedSculptVertex` remain the independent baseline used for
  parity checking. This cache does not become animation state.

### Reproducible CPU performance probe

A test-only, opt-in standalone benchmark is available; not part of CTest
and not run automatically by the Creator:

```bash
cmake -S . -B build/vivid-onepass -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_VIVID_SCULPT_BENCH=ON
cmake --build build/vivid-onepass --target aiverse_vivid_sculpt_pick_bench -j 4
build/vivid-onepass/aiverse_vivid_sculpt_pick_bench 3 \
  > build/vivid-onepass/sculpt-pick-benchmark.json
```

It creates synthetic imported-style meshes of 10,000, 100,489 and
1,000,000 model vertices. A checkerboard topology keeps the 1M-vertex
case below the 1M selected-mesh triangle cap. It records cold build +
first click, median warm clicks, one complete linear-reference
reconstruction/raycast, BVH visited nodes, tested triangles and
source-ID/ray-distance parity into JSON. This is a CPU performance
measurement on the **actual execution host** only. No claimed threshold
or fabricated GPU/FPS numbers are attached.

The benchmark is a synthetic rigid-mesh profile, not evidence of a real
skinned 1M-vertex character under full joint/morph load. Use the separate
`vivid_human_creator` regression fixtures for multiple skinned joints,
joint movement, invalid pose rollback, morph and seam-clone behavior.
Actual macOS build, CPU timing and Metal/Vulkan/GLES parity are **NOT RUN**
at this source stage. Continue with the deferred consolidated acceptance
before merging anything to main.


## GPU triangle-ID/depth selection contract (Stage 7AN)

The optional **GPU ID/depth selection format is now specified in code**,
but real per-pixel readback and integration into Creator's live offscreen
render pass remain separate work. The safe CPU BVH picker is still the
active, functional Creator path. Do **not** claim Metal/Vulkan/GLES GPU
picking is live merely because shader sources and a token codec exist.

`engine/character/vivid/SculptGpuPickContract.h` contains:

- `packSculptTriangleId(ordinal)` and `unpackSculptTriangleId(rgba)`:
  the *mesh-local* ordinal is encoded as `ordinal+1` little-endian into
  **linear RGBA8**; zero = no hit/occluder clear, max one million triangles.
  No sRGB conversion, tone mapping, blending, dithering or MSAA is allowed
  in an ID render target.
- `SculptGpuPickProtocol`: one pending click at a time with a full token
  `(generation, modelPublicationSerial, capturedFrameSerial, mesh,
  viewportWidth/Height, topLeftPixelX/Y)`. Another click/model
  publication invalidates all older callbacks. Exact token, actor
  publication, frame ordering, viewport and optional normalized depth
  are checked on readback.
- `resolveSculptGpuTriangle`: given that selected triangle and a CPU
  surface sampled from the **same exact morph/skin frame**, intersects
  only that triangle and maps an actual seam clone to its original
  source vertex; generated/LOD vertices and non-matching ray positions
  fail closed.

`SculptGpuPickShaders.h` provides paired GLSL ES 3.00 and MSL
`vs_main/fs_main` shader sources, 16-byte `SculptGpuIdVertex` and
standard `gfx::ShaderDesc`/`gfx::PipelineDesc` builders. This GPU
input is one expanded 3-vertex record per *rendered triangle*, all
three vertices carrying the exact same encoded ID, preventing
provoking-vertex ambiguity. The shader writes an RGBA8 ID color
with the ordinary hardware depth test; the host must render all
other relevant occluders into that depth target with zero ID.
`buildSculptGpuIdVertices` assembles that stream from *already
morphed and skinned* world coordinates and keeps inactive/LOD triangles
as zero-ID depth occluders. It never edits the source Model.

`DeformedSculptWorkspace::resolveGpuTriangle` is an opt-in bridge to
the existing CPU pose and source-ID mapping. For asynchronous GPU
results the workspace's matching `pick(...,sourceIds,frameSerial,
publicationSerial)` must have prepared the pose; a different frame,
different model publication or `reset()` rejects the result. Generic
CPU picks (without stamps) cannot be reinterpreted as GPU readbacks.

**RHI integration still missing:** the current VIVID branch has
`gfx::Device::captureNextFrame()` (presented screen) and
`readBufferAsync()` for GPU buffers, but no generally supported
asynchronous per-pixel offscreen texture readback used by this tool.
No ID pass resources are allocated, no draw command is recorded,
and no GPU callback is registered by VIVID yet. Building this
renderer-owned pipeline with proper occlusion, GPU morph/skin
vertex override, device-dependent depth, frame fencing, mobile
support and a real readback is the next integration step,
preferably alongside the stabilized shared Texture RHI work.
Do not use a screen screenshot workaround or stall each click.

CPU fixture assertions for ID encode/decode, stale queries,
invalid depth, seam-clone resolution, linear fragment layout and
source stream preparation are added in `vivid_human_creator`, but
**native build, CTest, GLSL/MSL compiler, Metal/Vulkan/GLES GPU
capture and real pixel parity have NOT RUN** on Stage 7AN.


## Stage 7AO — experimental Metal ID/depth renderer

Stage 7AN's GPU ID/depth format now has **an opt-in real Metal pass**.
It is not a complete all-backend or all-scene visibility solution.
By default Creator continues to pick using the CPU morph+skin BVH.

In imported sculpt authoring, enable **Experimental Metal GPU ID check**
before selecting a source vertex. After the CPU picker finds a vertex,
the Creator prepares the same-frame surface with a publication/frame
stamp. The optional `SculptGpuIdPass` (in
`engine/character/vivid/SculptGpuIdPass.h`) expands the **selected mesh**
into triangle-ID vertices, draws it using the canonical `gfx::Device`
offscreen `RGBA8` colour and `Depth` attachments, applies hardware
depth testing, and requests an asynchronous pixel readback. A later
frame correlates the returned RGBA bytes with the exact click,
published character, frame and viewport before resolving the source
ID via the original morph/skin CPU surface.

The new shared RHI method is:

```cpp
using PixelReadFn = std::function<void(std::vector<uint8_t>)>;
virtual bool gfx::Device::readTexturePixelAsync(
    gfx::TextureHandle texture,int topLeftX,int topLeftY,
    PixelReadFn callback);
```

It returns `false` on unsupported implementations. For now only
`engine/gfx/metal/GfxMetal.mm` overrides it, using a Metal
texture-to-shared-buffer blit with a 256-byte padded row, asynchronous
command-buffer completion, owner-thread callback on a later frame,
and explicit **BGRA8 → canonical RGBA8** channel conversion.
It rejects offscreen pixels outside the image, non-linear/non-RGBA8
textures, active encoders, missing command buffers and unbounded
callback queues. No synchronous pixel readback is performed on the
click path. GLES3, GL33 and Vulkan currently return unsupported;
CPU BVH remains the fallback for them.

The native platform CTest `vivid_metal_sculpt_id` is defined in
`tests/vivid/metal_sculpt_id.mm`. It creates two overlapping triangles
at different depths in an actual Metal offscreen pass, checks that the
nearest triangle's ID survives hardware depth test, checks a zero
background pixel, and verifies the owner-thread async readback
and Metal colour-channel conversion. The CTest and focused build target
`aiverse_vivid_metal_sculpt_id_tests` are included in the final
`tools/vivid-validate.sh` flow.

### Important limits — this is not yet full GPU picking

- **Only the selected imported mesh** is currently written to the ID pass.
  Other geometry, floor, hair, transparent objects and masked
  material visibility are not represented as zero-ID occluders. The
  hardware depth test is valid within the selected mesh only.
- The GPU ID stream is generated from the CPU morph+skin reference
  positions, **not the actual Renderer3D vertex-override GPU stream**.
  Therefore a numerical CPU/GPU deform difference or another deformer
  can still make the selected surface disagree with the normal
  scene colour pass.
- The GPU check is optional and disabled by default. It corroborates
  the CPU selection; if its draw/readback/token/deformation comparison
  fails, the CPU BVH result is retained.
- Framebuffer dimensions for this experimental mode are capped at
  4096×4096. GPU ID vertices are expanded to three records per
  triangle (max one million triangles), which can be expensive.
  Program/target/buffer lifetimes are owned by `SculptGpuIdPass`;
  pending per-click vertex allocations are released after true GPU
  completion and unsupported/failing jobs retain their buffers
  until a verified idle barrier, with a small bounded queue.
- A fresh pixel response is validated by model publication, exact
  captured pose frame, viewport dimensions and click generation.
  A new click or character publication invalidates older readbacks.
  No gameplay ECS, DNA, animation scheduler or texture streaming
  semantics are changed.

**Real verification remains pending:** although the native Metal
test and the operational rendering code have been added, no macOS
compiler, CTest, Metal pipeline compilation, GPU readback, Vulkan
or GLES driver check has been executed on this new commit. The
actual hardware performance and correct pixels are unknown.
The next stage should add proper complete-scene occlusion,
GPU-vertex-override parity and async GLES/Vulkan implementations
on a separate, carefully reviewed shared-RHI integration path.


## Stage 7AP — conservative scene-wide GPU ID occlusion

The experimental Metal selection pass now covers the **geometry of the
entire current VIVID Creator preview**, rather than only the selected
imported mesh:

- The selected mesh produces the normal 1-based RGBA8 triangle IDs
  (only for fully opaque, selectable material primitives).
- Other imported head/body/eye/groom meshes attached to nodes use the
  exact current morph-before-skin CPU reference positions and write
  **zero ID plus depth**. This supports different rigid or skinned mesh
  owners and fails closed if a mesh/palette cannot be reconstructed.
- The Creator's procedural floor is retained as CPU source triangles
  when first built, then supplied to the depth pass in its existing world
  coordinates.
- Dynamic procedural hair cards are supplied from their current
  `HumanGroomRuntime`-updated vertex stream using the same rigid head
  transform as the normal Renderer3D hair draw.
- Other-mesh, floor and hair occluders are appended to the same
  `SculptGpuIdVertex` stream as the selectable mesh; each has all four
  ID bytes set to zero. Invalid indices/non-finite positions or a
  1-million-total-triangle overflow reject the GPU confirmation and
  retain CPU BVH selection. No new GPU draw/texture owner is introduced.

**Alpha/transparent policy is conservative, not pixel-exact:**
`AlphaMode::Mask`, `AlphaMode::Blend`, non-opaque baseColor alpha or
explicit `Material::transparent` cannot emit a selectable ID.
For all occluder meshes (including cards and cornea), *entire triangles*
write zero-ID depth, regardless of the source alpha map. A partially
transparent card can therefore suppress an otherwise valid GPU ID,
even though a source texture has a visible hole. This is a conservative
false-negative in the GPU *confirmation* and preserves the existing
CPU result; it never claims a hidden vertex was GPU-confirmed.
Actual shader-side alpha testing and transparency/depth-after ordering
must eventually be aligned to `Renderer3D`.

Other limits also remain: the pass is CPU-assembled from one frame's
morph/skin geometry instead of reusing the GPU vertex override; the
normal renderer's precise LOD/culling decisions and custom renderer
objects are not yet shared; Metal is the only implemented async pixel
readback backend. The current Creator preview consists of the model,
floor and optional generated hair, all now represented. A game with
arbitrary external scene geometry must pass those extra occluders to
the same depth stage before claiming scene-wide visibility.

The Stage 7AP source fixtures check selected opaque versus masked or
blended material IDs, another skinned/rigid mesh's world-space zero-ID
occluder, and floor/hair transformation. The real Metal
`vivid_metal_sculpt_id` test additionally records a closer independent
zero-ID triangle and a farther independent triangle, expecting the
closer one to block the ID and the farther one not to block it.
They are **added test cases, not recorded passing results**: native
compilation, CTest, real GPU pixels and timing remain NOT RUN.

The experimental option remains off by default. Ordinary picking
still uses the CPU BVH and can proceed if GPU readback is unsupported,
invalid, occluded or conservatively blocked.


## Stage 7AQ — GPU alpha-masked ID pass and native GLES3 pixel readback

The Stage 7AP conservative whole-polygon mask has been upgraded **only
for standard material cutouts** where the original GPU base-colour
texture is available and the material has no custom ShaderGraph or
team/vertex-occlusion semantics that would change alpha processing.
The new `SculptGpuAlphaMask.h` defines a 32-byte
`SculptGpuMaskVertex`, a 48-byte std140/Metal
`SculptGpuMaskUniform`, paired GLSL ES300/MSL shaders and a
standard `gfx::ShaderDesc`/`gfx::PipelineDesc` pair. Supported
`AlphaMode::Mask` primitives use a separate fragment shader with
the original `Image::tex` GPU sampler, interpolated vertex alpha,
the exact `Material::uvScale/uvOffset/uvRotation` transform convention
and `Material::baseColor.w * textureAlpha * vertexAlpha` versus
`Material::alphaCutoff`. Fragments below the cutoff are discarded;
they do not write zero-ID depth, allowing geometry behind true
holes to remain visible in the selection ID pass.

The same hardware depth target receives opaque triangles, scene
zero-ID blockers and standard sampled mask batches. Selectable
masked fragments produce the true mesh-local triangle ID; masks on
other meshes produce zero ID but only on non-discarded pixels.
The established `SculptGpuIdPass` GPU resource owner retains each
temporary vertex buffer until the corresponding async pixel callback
has completed (or a verified GPU-idle barrier in a failure/shutdown
path). A per-click source material batch is capped, and external
masked occluders must carry zero ID and finite UV/uniform inputs.

`SculptGpuPickEligibility.h` explicitly rejects optional GPU
confirmation if the selected or any node-attached scene mesh has an
authored LOD chain, if mesh ownership is ambiguous or if a referenced
mesh is invalid. The current ID pass does not consume Renderer3D's
exact authored/generated LOD selection and must **not** guess which
level was actually rendered. This check only affects experimental
GPU confirmation: the existing CPU BVH picker remains active.

### Renderer / texture compatibility

The mask shader matches the ordinary ModelShaders alpha test only
when the conventional base-colour sampling path is representable.
Custom ShaderGraph alpha, team blending, local AO/back-occlusion
vertex-color overrides and missing base texture references still
use conservative full-polygon zero-ID blockers. `AlphaMode::Blend`
and depth-after/transparent cornea are also conservatively blocked
because real colour-pass transparent ordering is not mirrored here.
The normal render stage may apply texture derivatives, mip-residency,
LOD fade, virtual-texture sampling or custom vertex modifiers that
this diagnostic ID pass has not yet reproduced. This is **not**
a general guarantee of pixel-exact Renderer3D parity.

### GLES3 progress

`gfx::Device::readTexturePixelAsync` now also has a native
OpenGL ES 3 / native GL3 implementation using a 4-byte PBO, a
`glFenceSync`, non-blocking owner-frame polling, `glMapBufferRange`
**after** GPU completion and top-left-to-bottom-left coordinate
conversion. It preserves previous draw/read FBO, pack buffer and
pixel alignment bindings. Failed/unsupported requests return false
instead of blocking on a synchronous full-screen readback.
**WebGL2/Emscripten and the Windows GL-loader return unsupported**,
as they require separately verified data access/lifetime paths.

The experimental VIVID GPU ID pass remains **Metal-only** for now:
native GLES3 readback being available does not by itself supply the
completed-slot-safe `gfx::Device::frameSerial()` contract used for
actor/frame publication validation. GL currently reports no such
serial. Do not enable GLES scene picking by inventing a frameSerial.
Vulkan offscreen single-pixel readback also remains to be implemented
in the shared RHI, not copied into VIVID.

### Source fixtures and actual verification

`tests/vivid/human_creator.cpp` now covers GPU mask batching,
material alpha cutoff, UV rotation/scale/offset fields, source image
binding, packed vertex/fragment shader contract, no-ID fallback for
missing or custom alpha, and fail-closed authored-LOD eligibility.
`tests/vivid/metal_sculpt_id.mm` extends the real hardware CTest to
render fully transparent vs opaque texels from a Metal source texture,
expecting a zero/background ID from the hole and the normal nearest
mesh-local triangle ID from a solid texel.

**All newly added C++ tests, GLSL/MSL shader compilations, Metal ID
pixels and native GLES3 PBO callbacks are NOT RUN.** Do not mark the
latest branch validated until a specific Mac/Linux/Android build and
GPU tests report exact outcomes. The still-missing production-level
steps are true Renderer3D GPU vertex-override parity, actual visible
LOD/culling selection, complete alpha/Blend ordering, Vulkan pixel
readback and tested GLES/WebGL2 capability.


## Stage 7AR — Vulkan and native GLES3 experimental GPU ID readback

The experimental ID/depth shader/pipeline now accepts **Metal, Vulkan
and native GLES3/GL33** backends with a valid owner-frame serial. Its
normal CPU morph+skin BVH result is still available immediately and
remains the selected source vertex; a completed GPU result only
corroborates the **CPU-fed experimental ID pass**. If the GPU resolves
a different source ID or a stale frame/model/viewport, CPU selection
is retained and the discrepancy is reported.

**Important:** an ID confirmation here does not certify parity with
Renderer3D's actual GPU `ModelDraw::vertexOverride` stream, authored
LOD choice, culling, custom ShaderGraph vertex offsets, translucent
depth-after, or every non-Creator scene object. Those require shared
renderer-owned visible-draw provenance and follow-up hardware tests.
The Stage 7AQ authored LOD and mesh-owner rejection gate remains
enabled. Normal Creator GPU ID checks remain OFF by default.

### Shared RHI changes

- `engine/gfx/vulkan/VkDevice.h` and
  `engine/gfx/vulkan/GfxVulkan.cpp` implement
  `Device::readTexturePixelAsync` for a single pixel from a linear
  RGBA8 offscreen render target, outside render passes. Vulkan
  validates the texture/coordinates, allocates a four-byte
  host-visible coherent transfer destination, inserts exact
  `vkCmdCopyImageToBuffer` and conservative image/host memory
  barriers, then returns without waiting. Its existing reusable
  two-slot frame fences determine completion. On a subsequent device
  owner `beginFrame`, completed bytes are delivered to the callback
  in RGBA order. The pixel origin is transformed from the public
  top-left convention to Vulkan's current GL-oriented render-target
  storage. A failed GPU completion cannot publish fake data.
- Vulkan's `frameSerial()` reports the monotonic active recording
  submission serial; it never implies that submission already
  completed. The callback is gated by the slot fence, not just a
  numerical frame delta. Pending staging is released at confirmed
  completion or Vulkan device-idle teardown.
- Native `engine/gfx/gl/GfxGL.cpp` now also exposes a monotonic
  `frameSerial()` when a native GLES3/GL3 render frame is active.
  Actual PBO readback remains gated by the real
  `glFenceSync`/nonblocking `glClientWaitSync` already added in
  Stage 7AQ. Emscripten/WebGL2 and the custom Windows GL loader
  still report zero (unsupported), and their CPU fallback remains.
- `SculptGpuIdPass::initialize` may compile shader/pipeline resources
  before `beginFrame`; the strict nonzero/matching submission serial
  is enforced only when actually recording a click. If readback fails
  AFTER encoding draw commands, the pass disables further GPU work
  until an idle-safe shutdown instead of repeatedly retaining large
  in-flight vertex buffers.
- The Creator's opt-in checkbox now reads **Experimental GPU ID check**
  rather than Metal-only. On Vulkan/native GLES it uses the same
  triangle/depth and alpha-mask pipeline, the same click request
  generation, one outstanding request and async owner callback. It
  does **not** silently change a previously chosen CPU vertex.

### New native tests, not yet executed

The branch contains two new independent GPU tests:

```bash
# Linux/Windows with AIVERSE_VULKAN=ON and available Vulkan driver.
cmake -S . -B build/vivid-vk -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_VULKAN=ON
cmake --build build/vivid-vk --target aiverse_vivid_vulkan_sculpt_id_tests -j 4
ctest --test-dir build/vivid-vk --output-on-failure -R '^vivid_vulkan_sculpt_id$'

# Linux with libEGL/libGLESv2 dev packages and a GLES3 pbuffer driver.
cmake -S . -B build/vivid-gles -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vivid-gles --target aiverse_vivid_gles_sculpt_id_tests -j 4
ctest --test-dir build/vivid-gles --output-on-failure -R '^vivid_gles_sculpt_id$'
```

`vivid_vulkan_sculpt_id` uses the production headless Vulkan
Device (Mesa lavapipe is acceptable), creates near/far face ID
triangles and a closer zero-ID blocker, reads the actual hardware
pixel asynchronously and checks clear/nearest/occluded values.
`vivid_gles_sculpt_id` creates a native EGL pbuffer + GLES3
context and checks the corresponding fenced-PBO path and depth.
They exit **77 (CTest SKIP)** if an appropriate driver or context
is absent; a mismatch on a working device is a FAIL, never a fake
PASS. Both were added as source tests only and have **NOT RUN**.

The user's one-pass macOS `bash tools/vivid-validate.sh --full`
continues to cover Metal and shared C++ tests. Vulkan and native
Linux GLES acceptance remain separate platform checks.
Never treat a macOS-only pass as Vulkan/GLES validation.

Stage 7AR does not modify `main` or merge Texture RHI. The
new Vulkan/GLES readback and frame-serial contracts affect
shared `gfx::Device` implementations: semantic reconciliation
with the texture agent's branch, GPU allocation lifetimes and
real driver verification are mandatory before main integration.
