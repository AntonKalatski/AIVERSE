# Geometry performance controls

The renderer keeps the portable 4 KB uniform path (32 instances). Metal advertises a
32 KB block and uses frame-owned, 256-byte-aligned buffer pages for larger uploads,
allowing 256 rigid/baked instances per draw. `Renderer3D::setInstanceLimit(32)` forces
the old batching limit for A/B checks; zero selects the backend limit. Shader variants
use the same instance count in GLSL and MSL. Changing the limit invalidates pipelines.
This does not merge different meshes or materials, and CPU-skinned/morphed parts retain
their individual draws. More instances per draw are not automatically faster on every GPU.

`StaticModelCache::prepare(model, world)` caches rigid node transforms, primitive offsets
and conservative world bounds. Assign it to `ModelDraw::staticCache`. The model and cache
must outlive submission; rebuild after changing model geometry/materials/bind transforms.
A different world matrix or animation/material override bypasses the cache. GeometryScene
owns caches for its immutable instances and group proxies. Camera-dependent visibility and
transparency depth are still evaluated every frame. No mutable gameplay state is cached.

`GeometryScene::loadPrepared` accepts owned, nonempty rigid CPU models for procedural/import
pipelines. Geometry GPU resources belong to the scene. Images may carry `borrowedTexture`:
the texture owner must outlive the scene, and releaseModel does not destroy those handles.
HLOD can merge equal materials referring to identical live texture handles; matching filenames
alone are not proof of identical resources. Existing vertex budgets and transparency exclusions
remain in force. `setRuntimeOptions` is an A/B switch, not permission to modify static assets.

Geometry policy adds optional `shadowClusters` (default false) and `shadowErrorPixels`
(default 1, range 0.1..16). Rigid shadow casters are tested against the current light frustum.
Opt-in cluster selection uses that shadow map's matrix/resolution, including the spot pass;
it never uses main-camera occlusion. Masked casters retain fine cluster geometry. This is an
approximate geometric error policy; inspect silhouettes and moving light/camera transitions.
Offscreen objects may still cast visible shadows and are retained when the light sees them.

Offline mesh LOD tools are reused from commit 837b0211 (Sponza work), with meshoptimizer v1.3,
MIT, pinned source/hash manifest in third_party/meshoptimizer/UPSTREAM.json. Build with
`-DAIVERSE_BUILD_AUTOLOD=ON`, target `aiverse_lod_simplify`, then run
`tools/models/autolod.py source.gltf destination.gltf --worker /path/aiverse_lod_simplify`.
The existing Python generator accepts unpacked external-buffer glTF; it preserves source
resources. With `--skinned`, four-influence skins use canonical per-joint weight attributes (weight 8) in the simplification metric; joint IDs are never treated as continuous values. Without the flag skins are skipped. Morph targets remain unsupported. Retained vertices keep their original position, UVs, joints and weights byte-for-byte. Up to 32 total simplification attribute components are supported; larger joint regions require splitting. This is an attribute-aware error estimate, not a guarantee for arbitrary unbounded bone transformations; inspect extreme animation poses. Include referenced resources when distributing output.
`meshes[].extras.rtsLods` persists descending pixel thresholds and target meshes. Nested/cyclic chains, morph targets and mismatched skin targets are rejected. Skinned target meshes inherit the source skin even though they have no scene node. The renderer chooses using both main-camera and
sun-shadow projected extent; reflection/spot views currently reuse that mesh choice. Do not
claim per-pass AutoLOD for those auxiliary views. `setModelLodEnabled(false)` forces LOD0.

`Renderer3D::setProfiling(true)` enables CPU timers. `profile()` resets at beginScene and reports
submission, occlusion construction, per-pass selection/sorting/encoding, submitted triangles,
draws, instances, selected LOD nodes and cached models. Phases: 0 shadows (sun + spot), 1 opaque
(including reflections), 2 blended, 3 outlines, 4 transparent depth. CPU timings do not measure
GPU work; GPU command-buffer timing remains asynchronous. Frame wall intervals include the
macOS platform's 120 Hz NSTimer and presentation scheduling; they are not uncapped throughput.

Benchmark: tools/geometrybench accepts a final positional profile after keep-masked:
`surface` (default), `cache`, `clusters`, `occlusion`, `shadows`, `lod`, `hlod`, `all`, `instances`.
`static` combines LOD, HLOD, surface proxies, static/binding caches and larger batches,
without cluster or software-occlusion traversal. It is a comparison profile, not a universal preset.
Prepared-scene profiles use identical source-node decomposition in both A/B phases and toggle
runtime options; CPU mesh preparation is outside timed frames. `all` combines applicable
features; explicit surfaces still prevent parent HLOD from bypassing their threshold. Legacy
visibleInputTriangles is -1 for grouped/LOD profiles; use per-pass triangles0..4 counters.
The instances profile on Bistro is not a repeated-mesh stress test; use aiverse_instance_preview
for 1025 identical meshes, forced 32/default batches, GPU timing and screenshot comparison.

Prepared C++ scenes retain a 2-million source-vertex default. A caller may explicitly request
up to 16 million; this is a memory budget, not a performance promise. Bistro's per-node split
uses an explicit 8-million budget. JSON-authored scene limits are unchanged, and HLOD proxy
vertices retain their independent 2-million budget.

Metal also avoids rebinding the identical texture/sampler or vertex buffer within one encoder.
The cache compares actual Metal objects (not recyclable handle IDs), resets for each pass, and
can be disabled through `gfx::Device::setBindingCacheEnabled(false)` for A/B diagnostics.
Other backends retain their existing behavior. The additional benchmark profile is `bindings`.

CPU cluster selection skips primitives below 2048 triangles and rejects fragmented selections
above four draws, or when the removed indices do not pay a heuristic 4096-triangle cost per
extra draw. This prevents fewer triangles from creating hundreds of extra CPU draw calls.
The heuristic is a bounded fallback to exact geometry, not a universal hardware cost model.

For a macOS throughput diagnostic only, `AIVERSE_BENCH_UNCAPPED=1` disables CAMetalLayer
presentation sync, uses a 1 ms app timer and holds a user-initiated latency activity until
termination. Normal applications keep their existing pacing. This is not a perfectly uncapped
busy loop: report actual frame intervals and CPU/GPU times, and do not compare results against
paced runs as if only geometry changed. The benchmark JSON records this flag.
The benchmark also accepts warmup and sample counts after the profile (defaults 60/120).
Throughput confirmation uses 180/600 to avoid shortening the sample window when pacing is removed.

Measured benefits and regressions are retained in
[the native Bistro report](benchmarks/bistro-geometry-stack-2026-11-12.md).

### Skinned AutoLOD runtime

Palette-skinned draws with a current pose and node-world matrices select embedded AutoLODs, retaining animation graphs, per-instance shoulder edits and sockets. Selection bounds conservatively enclose transformed source primitive boxes for every influencing joint; main-camera and sun-shadow size both constrain detail. Original LOD0 remains available. Baked-only draws without current pose bounds retain LOD0 (no claim of skeletal AutoLOD in that mode). Material overrides stay on the shared material indices. Morph LODs remain unsupported.

`aiverse_modelview model.glb@Run --time 0.16 --distance 20 --lod-report` prints actual selected LOD-node count and main/shadow submitted triangles. `--no-lod` supplies a LOD0 A/B reference.


### Incremental rigid CPU transform cache (opt-in)

`AIVERSE_PERSISTENT_CPU_MODEL_CACHE=1` enables an engine-level, backend- and
genre-independent `Renderer3D` cache for `ModelDraw` submissions with a
nonzero stable `persistentId` and `kDrawStatic`. The first eligible draw
precomputes the existing `StaticModelCache` (model-space bind transforms
multiplied by world, primitive offsets and conservative bounds). Later draws
reuse these values when the model pointer, GPU vertex/index buffers, hierarchy
sizes and the **exact 4x4 world matrix** match. A changed model or transform
rebuilds the entry. Entries unused for several Scene/Game viewports are evicted
at the next 64-scene sweep. Disabling the flag preserves the previous draw path.

This cache is not a persistent render queue: callers still visit each object,
and the renderer still performs camera/light frustum culling, per-node/LOD
selection, material grouping, depth sorting and GPU submissions every frame.
Material overrides and per-node material choices are allowed because the
cache contains geometric transforms rather than colour/material state; active
vertex-displacement graphs force the ordinary uncached path. Animated,
skinned, posed, baked, runtime-mutable-node and clustered geometry models are
excluded from automatic caching. Externally prepared `ModelDraw::staticCache`
still takes precedence when valid. Different draw IDs (including attachments)
must not accidentally reuse one another.

**Asset lifetime/invalidation:** the cache is not an owner of Model data.
Call `Renderer3D::invalidateStaticModelCaches()` before in-place edits to
bind transforms, hierarchy or geometry that could retain the model pointer and
GPU buffer IDs. The editor calls this API from `reloadRules()`; native games
and other asset-reload paths must do the same. The Model also exposes
`geometryRevision` and `markGeometryChanged()`; `finalize()`, CPU upload
preparation and `releaseModel()` bump it automatically. A matching pointer,
GPU handles, hierarchy dimensions and world matrix alone cannot authorize a
cache hit if the revision has changed. Call `markGeometryChanged()` after any
custom in-place edit to bind matrices, hierarchy, primitive geometry or model
bounds that bypasses these engine APIs. External static caches still follow
their caller-owned invalidation contract. Do not modify model data concurrently
with `drawModel`. The remaining native streaming / editor life-cycle coverage
and real GPU validation are why this feature is initially off by default.

The Scene → Batching popup exposes hits/rebuilds/declines, live entries and
evictions (counters are per Scene invocation). Measure unchanged view/camera,
with a warmup, comparing separate processes with flag absent vs `=1`.
Compare `[scene-stages]` and `[view-stages]` as well as visual consistency:
these cache counters do **not** prove a frame-time improvement. MDI / GPU ICB
is independent and should stay disabled during the initial A/B on Metal.


### Persistent rigid CPU render packets (experimental)

`AIVERSE_PERSISTENT_CPU_PACKETS=1` enables a **second independent opt-in**
in `Renderer3D`. It caches each eligible rigid `ModelDraw`'s material/primitive
selection, static item transforms, and per-mesh colour/shadow batch
representation as `ModelItem` templates. A warm hit copies those item
templates instead of walking the model's primitive/material batches and
reassembling the full items. The engine **still** performs per-node main-camera
and shadow-frustum culling, computes current transparent depth and then runs
normal per-pass material grouping, instancing, shaders and GPU draws.
No indirect GPU commands or static GPU buffers are preserved.

Native project example (must maintain the revision contract):

```cpp
render::ModelDraw d;
d.model = myImmutableRigidModel;
d.world = worldMatrix;
d.persistentId = uniqueEntityDrawId; // never share IDs between distinct draws
d.flags = render::kDrawStatic;
d.staticPacketRevision = immutableAppearanceRevision; // nonzero!
renderer.drawModel(d);
```

`ModelDraw::staticPacketRevision` defaults to zero and is the caller's
explicit opt-in for non-GPU-state appearance semantics. On every packet hit
the renderer automatically compares a snapshot of resolved GPU material
state — factors, shader graph and texture resource handles — for both model
materials and `ModelDraw::materials` overrides. Copied, reallocated or
pointer-stable material vectors are remapped by material **index**; a
changed state invalidates the packet and uses the ordinary rendering path.
This prevents stale Material pointers when a native caller reallocates
its override vector. External texture writes that keep the *same* GPU handle
and non-renderer state still require explicit owner revisions. The renderer
checks all standard `ModelDraw` visual fields and world transform too,
plus per-node material assignment indices and the selected variant.

CPU runtime LOD is supported for rigid models: a packet remembers the
selected LOD mesh per node and re-selects the LOD against the **current**
main-camera and sun-shadow projected error on every submission. When the
chosen mesh differs, it rebuilds. When a transition is in progress (LOD
crossfade), it bypasses packet reuse entirely so fade weights are always
current. Experimental GPU-compute LOD remains on its existing uncached path.

Static per-node material overrides (`ModelDraw::nodeMaterial`) and
`KHR_materials_variants` selections can now use packet replay. The current
override index vector is compared by value against a stored copy before replay
and is never retained by pointer; switching variant also invalidates the packet.
These modes preserve baseline renderer behaviour: **per-node material overrides
or an explicit variant disable distance-based CPU LOD selection** for that draw,
while all geometry/material batch compatibility decisions remain unchanged.
Snapshots are included in the bounded packet memory budget.

Eligibility still excludes *active* animation poses (`nodeWorld` /
`pose` / baked samples), skinned and morphed models, runtime-mutable
nodes, cluster geometry, vertex displacement, baked GI and active LOD
diagnostics. A model file may contain authored animation clips and still
be cached for a particular `kDrawStatic` instance using the rigid bind
pose, provided it supplies no live pose, has no skins, and its bind
transforms have been finalized. The instance remains an ordinary
uncached draw if later supplied an animated pose. No simulation state
is cached or made genre-specific. The optional RTS view bridge marks
safe stationary scenery with a nonzero packet revision; this has **no**
effect unless the global CPU packet flag is explicitly enabled.
Native games can instead use `engine/render/EcsStaticModelRender.h`
to bridge integer ECS presentation components and the generic
`WorldTransform` into `Renderer3D`.

ECS integration (shared, header-only, without RTS/FPS dependencies):

```cpp
#include "engine/render/EcsStaticModelRender.h"
// A render-only phase after SceneHierarchy::update:
render::drawEcsStaticModels(world, renderer,
    [&](uint32_t asset) -> const render::Model* { return lookupModel(asset); },
    [&](Entity entity) -> uint64_t { return stableNamespacedDrawId(entity); });
```

The host must supply a collision-free nonzero ID across the Worlds and
multiple draw sources sharing a Renderer3D. `EcsStaticModelRender` carries
a stable integer `assetKey`, `appearanceRevision`, draw flags and visibility,
plus optional **appended** `materialOwnerKey` and `materialOwnerRevision`
fields (both zero by default). Existing positional four-field aggregate
initializers remain source-compatible. All owner fields are copied into
`ModelDraw` by `visitEcsStaticModels` / `drawEcsStaticModels`. Set these
two fields only when many static Entities share one revision-controlled
material set; do not populate them from a raw Material pointer hash.
For an external per-instance material vector, use `visitEcsStaticModels`
with a presentation submit callback that resolves and attaches the live
`draw.materials` before calling `renderer.drawModel(draw)`. This avoids
putting C++ pointers into the ECS component.

Example:
```cpp
render::EcsStaticModelRender view{
    assetKey, 1, 0, true, sharedMaterialSetId, sharedMaterialSetRevision
};
world.add<render::EcsStaticModelRender>(entity, view);
render::drawEcsStaticModels(world, renderer, resolveModel, stableNamespacedDrawId);
```

There are no renderer/model pointers or floats in the authoritative ECS
component. `SceneHierarchyRender::hierarchyMatrix` performs fixed-point
to float conversion with render-origin subtraction. The bridge is optional:
old projects do not change until they register these components and call
the adapter.

Source templates hold non-owning `Model*` and `Primitive*` references.
Cached material pointers are explicitly rebound from the current live vector
before each replay. `Renderer3D::invalidateStaticModelCaches()` clears both
transform and packet caches for whole-scene reloads; the editor does so from
`reloadRules()`. Native streaming hosts can now call
`Renderer3D::invalidateStaticModelCache(persistentId)` to retire a single
object's transforms and packet, or `invalidateModelCachesFor(model)` to
retire **all** retained entries referencing an asset before unloading that
model. These API calls update packet byte accounting immediately instead of
waiting for periodic expiry; they do not modify already queued frame draws.
`engine/render/EcsStaticModelRender.h` offers
`invalidateEcsStaticModel(renderer, entity, stableKey)` for ECS despawn or
asset replacement. The host must call these between submissions, on the
renderer thread, before releasing source models, or use full invalidation.

The implementation caps each packet at 128 items and combined retained
item/material/LOD/override snapshot payload storage at 64 MiB, with periodic
expiry of unused packets. A cache miss always uses the original draw path.

Scene → Batching → Persistent rigid draw packets reports per-scene replay,
rebuild and declined counts. Use matched fixed-camera Metal/other-backend
A/B/A runs and pixel checks to decide whether copying item templates is
faster than normal assembly for actual scenes. Do not infer GPU performance
from CPU packet hit count. The feature is off by default and has not been
validated with owner Mac build/GPU capture yet.

### Real-backend persistent packet parity fixture

The focused GPU batching workflow now runs the existing `aiverse_material_batches`
program on macOS with `AIVERSE_PALETTE_TEST_CASE=static_packets` and
`AIVERSE_PERSISTENT_CPU_PACKETS=1`. It uses an inline 16-part,
three-material rigid fixture with a copied material vector and a stable
entity key, to exercise the native material-override code path without
external assets. The first reference frame is submitted with
`staticPacketRevision=0` (uncached); later frames use revision 1 and
produce an actual packet replay. The test fails if warm packet hits are
zero, opaque/shadow API draw counts differ, or the two GPU captures differ
byte-for-byte. The fixture does not mutate `materialBatches` each frame:
that would invalidate stored primitive pointers without a geometry revision
and make the test measure an unsupported asset-lifecycle violation.

Run on the Mac with the HDR fixture enabled:

```sh
cmake -S . -B build/metal-packets -DAIVERSE_BUILD_HDR_TESTS=ON \\
  -DAIVERSE_BUILD_EDITOR=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-packets --target aiverse_material_batches -j8
AIVERSE_PERSISTENT_CPU_PACKETS=1 AIVERSE_PALETTE_TEST_CASE=static_packets \\
  AIVERSE_MULTI_DRAW_INDIRECT=off \\
  ./build/metal-packets/aiverse_material_batches unused.obj /tmp/static-packets
```

This is a visual + CPU policy regression, **not** a CPU/GPU throughput
benchmark. The CI job and runtime screenshots must succeed before this
experimental mode can be considered validated. Real RTS static scenes with
LOD and dynamic material changes still need separate A/B/A.


### Revision-memoized static model eligibility

The optional static transform cache and persistent CPU packet renderer share a
renderer-owned memo of **model-level** eligibility. This avoids repeatedly
scanning the same rigid asset's node flags, skin references, primitive morph
targets and mesh deformation flags for every building/prop instance.

`engine/render/ModelStaticEligibility.h` implements a genre-neutral, CPU-only
`classifyModelStaticEligibility(const Model&)`. Its output has two independent
capabilities: `transforms` (safe to reuse bind-pose geometry transforms) and
`packets` (safe to retain and replay rigid primitive/material packet templates).
`StaticModelCachePolicy.h` then applies the normal **per-draw** gates to those
traits: stable nonzero ID, `kDrawStatic`, no live pose/baked sample,
active vertex displacement, baked lighting and inspector rules. Model metadata
never overrides current per-instance eligibility.

`Renderer3D::cachedStaticModelEligibility()` retains model traits across
views/frames. Each lookup verifies the model's `geometryRevision`, GPU
vertex/index buffer handles, counts of nodes/meshes/bind matrices/skins/materials,
and geometry attachment before reusing a result. A mismatched signature
forces fresh classification. The memo holds at most **8192 model entries**;
new uncached assets are classified directly when full, and idle entries are
pruned periodically. Full scene invalidation and
`Renderer3D::invalidateModelCachesFor(model)` retire memo entries along with
their transform and draw packets. This cache holds no GPU allocations.

Native callers modifying node flags, skin state, morph targets or hierarchy
**in place** without using standard import APIs must call
`Model::markGeometryChanged()` before drawing, even when the vector size
does not change. Before model destruction, call
`invalidateModelCachesFor(model)`; raw `Model*` identity alone is not a
lifetime guarantee. Renderer access must remain on its submission thread.
The Scene → Batching panel reports model classification hits, misses, live
entries and evictions.

Both existing environment gates remain unchanged and OFF by default:
`AIVERSE_PERSISTENT_CPU_MODEL_CACHE=1` for transform reuse, and
`AIVERSE_PERSISTENT_CPU_PACKETS=1` for packet replay. The memo is only active
with at least one of these paths. Camera/shadow frustum culling, LOD choices,
per-draw appearance and material GPU state checks continue every frame. No
GPU indirect submissions, shader output or simulation code is affected.

### Versioned material owner contract (experimental)

`Model::materialRevision` is a separate runtime monotonic revision from
`geometryRevision`. `Model::markMaterialChanged()` advances it when native
code changes model-owned material state, shader graphs, image bindings or
texture **content** that might retain an existing GPU handle. Engine paths
also advance it when `finalize()` rebuilds the imported material table,
when `uploadModel()` changes a texture binding, when `addImage()` adds an
image, and when `releaseModel()` destroys textures. This opt-in revision
is not part of serialization or authoritative simulation.

`ModelDraw::materialOwnerKey` and `materialOwnerRevision` (both default
zero) are optional presentation-side fields for independent static objects
sharing one material-set owner. An owner key must uniquely identify one
material-set source **within the Model**, and the revision must change
whenever that owner's material factors, shader graphs, texture bindings,
content or vector layout mutate. The renderer must not infer material state
from `Material*` identity. If either value is zero the standard exact
resolved-GPU-state comparison remains mandatory. The optimization remains
behind `AIVERSE_PERSISTENT_CPU_PACKETS=1`; providing version fields alone
never enables caching or bypasses eligibility checks.

Example:
```cpp
ModelDraw draw = makeStaticDraw();
draw.materials = &sharedMaterialSet;
draw.materialOwnerKey = stableMaterialSetId; // nonzero, same for sharers
draw.materialOwnerRevision = sharedMaterialSetRevision; // starts nonzero
// On any sharedMaterialSet in-place edit: ++sharedMaterialSetRevision.
// On model-owned GPU state change: model.markMaterialChanged().
renderer.drawModel(draw);
```

Material owners must outlive the draw currently being submitted; explicit
revisions are a correctness contract, not an automatic write observer. A
texture content update behind the same GPU handle must trigger a revision
even when material pointer, vector size and GPU handle ID do not change.
`Renderer3D::invalidateModelCachesFor(model)` remains mandatory before
destroying/replacing a Model. Do not use this fast path for an owner that
cannot guarantee version propagation; the existing unversioned path is
available and checks the full material state.

**Implemented shared material-state memo:** With
`AIVERSE_PERSISTENT_CPU_PACKETS=1` and both nonzero owner fields,
`Renderer3D` maintains a separate cache keyed by
`(Model*, materialOwnerKey)`. For a newly observed owner revision it
computes resolved GPU material fingerprints once, plus the minimum shader
quality that enables vertex displacement. Later draws with the same
`materialOwnerRevision`, `Model::materialRevision`, geometry revision and
material count reuse that signature. An increasing internal serial
identifies each refreshed material state. A saved static packet may hot-replay
only while its serial matches; when owner or source revisions change the
packet rebuilds, so it cannot silently reuse the old material selection.

The versioned path rebinds every packet's material index to the **live**
draw's material vector. It does not save a per-object material copy or hash
vector when a shared owner signature exists; the signature is retained
once for all objects sharing that owner. Thus thousands of rigid static
instances can share the same material-state computation and storage.
`minVertexOffsetQuality` also avoids rescanning each material's shader
graph on hot draws while remaining sensitive to the current material
quality level. If either owner token is zero, the renderer still performs
its existing full material hash plus exact GPU-state equality checks
for every packet candidate, with no pointer-identity shortcuts.

The shared owner cache is bounded to 4096 entries, 512 materials per
owner, and **8 MiB of fingerprint payload**. Old owners expire after
128 unseen render-scene epochs at periodic sweeps; full invalidation
and `invalidateModelCachesFor(model)` discard associated signatures.
`Renderer3D::invalidateSharedMaterialState(model, materialOwnerKey)`
retires just one owner signature before a native material source is replaced
or destroyed. Packets that referred to its previous serial then miss and
rebuild if the owner is encountered again. This is a rendering-thread
operation; keep material sources alive through already queued frame draws.
The Scene → Batching panel exposes shared hits, misses, entries,
evictions and payload memory. No GPU resources are owned by this cache.
Model pointer identity is only a scoped lookup key, not an asset lifetime
guarantee. Owners changing material vector contents without incrementing
their revision violate the explicit fast-path contract; use the conservative
unversioned path for data without reliable revision propagation.



### Static cross-model GPU-page groups (experimental CPU MDI)

`AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` enables a targeted **CPU-side**
grouping policy for rigid static ModelDraw primitives originating from
*different Models* that share exactly the same pooled GPU geometry page
(`Model::geometryPoolPage`, vertex and index buffers). It does **not**
merge CPU asset meshes or change the geometry pool allocation. It relies
on the renderer's existing indexed multi-draw indirect (MDI) backend and
its per-command `firstIndex + gpuFirstIndex`, `gpuBaseVertex` and
`firstInstance` offsets. The opt-in therefore also needs the normal
model MDI option and `Device::supportsMultiDrawIndirect()`. The Metal
command threshold remains governed by `AIVERSE_METAL_MDI_MIN_COMMANDS`
(default 16 unique commands); smaller groups keep their ordinary draws.

**Two separate stages preserve correctness:**

1. Every source ModelDraw independently performs current-camera/frustum,
   sun-shadow and per-node LOD selection, including crossfade decisions.
   Then the normal pass selector applies transparency, reflection,
   occlusion and other visibility rules to each ModelItem. No model is
   admitted to the shared batch solely because another model is visible.
2. Only after that, the main opaque and sun-shadow pass may sort rigid
   static candidates with one shared `geometryPoolPage` toward each other.
   The existing material equivalence registry (GPU state + resolved
   texture handles, exact comparison), pipeline flags, GI, shadow roles,
   current visible primitive and vertex/index handles still gate each
   indirect group. Different source model pointers are fine **only** when
   their shared GPU arena and buffer handles agree. Otherwise the usual
   sort, instancing and indexed direct draw fallback remain intact.

The additional sorter is deliberately conservative: it excludes animated
or skinned/morphed draws, transparent/faded/actor-reveal objects, active
LOD crossfade, GPU-authored LOD, material-batch roles, baked GI, shader
graphs and unpooled geometry. It does not reorder blended passes,
reflection/auxiliary rendering or editor draw-call debug mode. Vulkan
indirect draws with nonzero `gpuBaseVertex` use the existing direct
fallback. `AIVERSE_RENDER_SAFE=1` or
`AIVERSE_MULTI_DRAW_INDIRECT=off` disables the optimization. It is
independent of `AIVERSE_PERSISTENT_CPU_PACKETS` and can group ordinary
static ModelItems with no retained packet cache.

The common policy lives in
`engine/render/StaticCrossModelBatchPolicy.h` as the pure
`staticCrossModelPageCompatible(a, b)` page/buffer check.
`Renderer3D::renderModels` does the per-pass safe candidate sorting
and grouping using the existing CPU MDI backend.
`ModelRenderStats::staticCrossModelSubmissions`,
`staticCrossModelCommands` and `staticCrossModelItems` count
**successfully submitted** multi-model CPU MDI groups, not merely
candidates. See Scene → Batching → Instancing and indirect details.
Ordinary model MDI already supported limited mixed-model grouping via
`AIVERSE_MDI_GEOMETRY_SORT=1`; this opt-in provides a narrower
static-only sorting and shared-page contract rather than replacing it.

### Page-aware static MDI preflight (Stage 18)

Before doing the *additional* static cross-model sort, the renderer now
preflights only ModelItems that already passed the regular per-pass
visibility, shadow/occlusion and LOD selection. This avoids rearranging a
scene's ordinary draw order when no useful mixed-model indirect group is
possible.

The preflight groups candidates by the exact tuple
`(GeometryPoolPage*, materialGroup, drawFlags, GPU vertexBuffer, GPU indexBuffer)`.
The material group is already resolved with full GPU-state equivalence, not
raw material pointer identity. A group is **ready** only when it includes
at least two distinct Model objects, at least the backend's minimum number
of distinct primitive commands, and enough items to cover those commands
within `Renderer3D::instanceLimit()`. On Metal this uses the existing
`AIVERSE_METAL_MDI_MIN_COMMANDS` (2–32, default 16); other backends use
the existing four-command threshold. The same primitive repeated many
times counts as **one** potential indexed command, never as enough MDI
work by itself.

Unique primitives are held in a fixed array of 32 pointers per preflight
group (no per-group dynamic vector allocation). A pass limits the preflight
map to 2048 candidate groups: when this budget is exceeded, it leaves
the original sort order in place rather than expanding memory/work
without bound. Only items in preflight-ready groups are promoted into
the additional cross-model sort. Geometry pages are kept separate within
that order even if GPU handle numbers happen to match. Each sun-shadow
and opaque pass decides independently; a ready group in the shadow pass
does not imply one in the main view.

Preflight is deliberately a **necessary-condition filter**, not a promise
that MDI will submit. The existing indirect batch builder still verifies
exact buffers, materials, pipeline/draw state, per-command offsets and the
actual count of contiguous primitive runs; it falls back to ordinary
instancing/direct draws where a real group is too small or incompatible.
The cross-model sort remains disabled in transparency, reflection,
auxiliary/debug, unsafe or unsupported GPU paths, and while
`AIVERSE_MDI_INSTANCE_PALETTE` is active (the separate palette sorter
already handles that experiment). The normal `AIVERSE_MDI_GEOMETRY_SORT`
path remains independent.

Scene → Batching now displays preflight groups, ready/rejected groups and
selected items separately from successful mixed-model MDI submissions
and commands. These counters should be measured alongside CPU
`sortMs`, `drawMs`, backend draw calls and GPU frame time; a high
number of ready groups alone does not establish a speedup.

**Performance / testing:** Keep OFF by default until it is built,
pixel-compared and benchmarked on Metal and other backends. Static page
grouping does not promise fewer calls unless visible compatible meshes
meet the backend's command threshold. Compare same-camera CPU/GPU time
and rendered pixels OFF/ON/OFF, including buildings, cars, distance LOD,
shadows, transparency and missing shared-page models. No numerical speedup
or parity proof has yet been obtained; feature implementation precedes
tests at the user's request.


### Adaptive static pooled CPU MDI feedback (Stage 19)

`AIVERSE_CPU_MDI_ADAPTIVE=1` optionally makes the **Stage 17/18 static
pooled-page** CPU MDI path select between its extra cross-model grouping and
the ordinary instancing/direct path using recent renderer-side **CPU**
preparation cost. It only activates when
`AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` is also enabled. Both are OFF
by default; GPU-authored ICB, dynamic models, general CPU MDI and other
render passes keep their existing policies.

The common genre-neutral, backend-independent
`engine/render/CpuMdiAdaptivePolicy.h` contains the adaptive
`CpuMdiAdaptiveFeedback` policy. The renderer retains one feedback state
for the **sun-shadow pass (0)** and one for the **opaque main pass (1)**.
Each qualifying static pooled group still needs the page/material preflight
and enough **unique indexed primitive commands** for the backend's MDI
threshold. Only then is its cost feedback consulted. The signal is measured
CPU wall-clock microseconds from compatible-run scanning through
indirect command packing, material/pipeline binding and backend submission
calls, divided by the number of actual unique primitive runs. It does
**not** measure GPU execution, total scene frame time, or predict a GPU
speedup.

The first two measured attempts are treated as warmup; feedback then
tracks an exponential moving average (15% new sample), and starts gating
after eight usable samples. A separate threshold,
`AIVERSE_CPU_MDI_MAX_US_PER_COMMAND` (default **25**, accepted numeric
range **1..1000**), determines whether the observed normalized CPU
submission cost is too high for this workload. The default is a
**provisional heuristic**, not a benchmark-derived platform optimum.
When too high, the extra pooled cross-model sort is disabled for the
current pass and eligible pooled static objects take their normal
instancing/direct batching path. Other model MDI groups are unaffected.
Once every 32 preflight-qualified opportunities, one normal MDI pass
is probed to allow recovery after pipeline warmup, workload changes or
a different camera. Feedback is retained per `Renderer3D` lifetime,
and reset on renderer shutdown; no global or gameplay state is modified.

The **minimum number of distinct commands and GPU capability checks remain
hard requirements** regardless of feedback. Model material/pipeline
compatibility, culling, LOD, shadow selection and per-instance bounds
still run normally. Failure of indirect submission remains handled by
the existing draw fallback. Stage 19 never introduces extra GPU
commands or bypasses an eligibility check.

Scene → Batching → Instancing and indirect details reports the moving
average in microseconds/unique command for shadow and opaque, its sample
count, adaptive skip/probe pass counts and measured attempts, alongside
successful cross-model CPU MDI submissions and total actual draw calls.
These are diagnostics for later A/B/A benchmarks, not proof of faster
rendering. Compare `AIVERSE_CPU_MDI_ADAPTIVE=0/1` with the same camera,
scene and backend on Metal and other supported GPUs, tracking rendered
pixels, CPU scene time and GPU frame time. Tests and runtime benchmarking
remain deferred by the user's feature-first request.


### Page-local adaptive CPU MDI feedback (Stage 20)

Stage 20 replaces the Stage 19 **pass-wide admission decision** with a
**per-geometry-page** decision. Enable the existing pair of experimental
flags, `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` and
`AIVERSE_CPU_MDI_ADAPTIVE=1`, to use this path. There is no new default-on
behavior and no genre-specific policy. The Stage 19 `CpuMdiAdaptiveFeedback`
EMA remains the same; it is now maintained independently for each live
GPU geometry page and for the shadow (phase 0) and opaque (phase 1) passes.

The renderer tracks a bounded `CpuMdiPageKey`: the identity of
`GeometryPoolPage` plus exact GPU VB/IB allocation handles. The value
holds a `weak_ptr` to the GPU page, a pair of per-pass cost feedback
objects, and an epoch-local admission decision. On lookup it validates
the weak lifetime against the model's current `shared_ptr`, so a raw
address reused after streaming/unloading cannot inherit an old CPU cost.
At most **256 GPU pages** have retained feedback per renderer; pages not
tracked because of this cap use the original CPU MDI path, not a
potentially incorrect global fallback. Idle/expired pages are pruned
at 64-epoch sweeps after 128 unseen render-scene epochs; whole-scene
static cache invalidation and renderer shutdown reset page feedback.

After the normal view/LOD/material selection and Stage 18 page-aware
preflight, each *ready* material/page group consults the page's feedback.
Only **one** `permit()` decision is made per page, scene epoch and pass,
even if many groups on that page use different materials. A ready group
receives one of two local outcomes: **permitted (mode 1)** for special
cross-model sorting/MDI, or **declined (mode 2)** for ordinary instancing
and direct drawing. This is a per-item decision bitmap, not a single
pass-wide Boolean. Therefore a costly page can decline CPU MDI without
suppressing another eligible, inexpensive page in the **same** shadow
or opaque pass. Groups below the unique-command threshold remain on
their existing ordinary path. The global pass average remains available
for diagnostics only and is **never** used to block a page.

Actual CPU wall time from the *permitted* page's indirect preparation
and submission is recorded against that page's own feedback, normalized
by its unique indexed commands. The same two warmup attempts, eight
usable EMA samples, budget `AIVERSE_CPU_MDI_MAX_US_PER_COMMAND` (default
25 us) and periodic one-in-32 *page* recovery probe apply. The existing
exact GPU page, material, shadow, pipeline, command-offset and LOD
checks remain mandatory. This signal still measures **CPU overhead
only**, never GPU execution or a guaranteed FPS gain.

Scene → Batching exposes the number of live/created/reused/evicted and
untracked page feedback records, slow pages separately for shadow/opaque,
skipped and probed *groups*, and aggregate CPU cost labelled
diagnostic-only. Experimental flags remain OFF by default. This stage
has not been compiled or GPU pixel-compared yet because the user
requested feature work before tests.


### Reused page-aware preflight workspace (Stage 21)

`Renderer3D` now owns one
`StaticCrossModelPreflightWorkspace`, implemented in the common
`engine/render/StaticCrossModelBatchPolicy.h`, instead of allocating a
fresh `unordered_map`, a material/page group for every hash-map node,
and a separate local per-item mode array on every sun-shadow or opaque
model pass. The workspace remains strictly **per render pass** in
meaning: `beginPass()` invalidates the previous lookup generation and
clears all group state before observing the new pass's visible ModelItems.
It retains only allocated capacity between passes, not a persistent
visibility, LOD or material decision.

The directory is fixed at **4096 open-addressed hash slots**, allocated
lazily the first time this experimental option is used. Group storage is a
reusable vector capped at **2048 exact**
`(geometryPage, resolvedMaterialGroup, drawFlags, GPU VB, GPU IB)`
entries; each group already contains the bounded 32-primitive preflight
array from Stage 18. Hash collisions require an **exact key match**,
not merely equal hashes. Slot generations advance with each pass; a
generation wrap clears old slot stamps to prevent stale lookup. The
workspace stores a temporary 16-bit group index for every ModelItem in
the current pass (`UINT16_MAX` conceptually denotes no group), so the
second ready-group promotion pass does **not** repeat the expensive
GPU-page/material hash lookup or recalculate eligibility. Its 8-bit
`0=ordinary, 1=cross-model MDI, 2=adaptive instancing fallback`
mode map is only initialized when at least one group passes preflight;
small/fragmented scenes skip that full-array write entirely.

This retains preflight grouping CPU memory at a predictable high-water
capacity: at most 2048 groups with their fixed-size primitive arrays,
4096 hash slots, plus two small arrays proportional to the current model
item count. No extra GPU allocations, model asset ownership, new shaders
or additional draw calls were introduced. A full scene reload via
`invalidateStaticModelCaches()` or `Renderer3D::shutdown()` releases
the reusable buffers, rather than retaining large buffers after asset
streaming changes. Model asset unloading still follows the existing
renderer submission-thread and source-lifetime rules.

The original conservative `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1`
flag and optional `AIVERSE_CPU_MDI_ADAPTIVE=1` gate this path (both
default OFF). Independent per-object camera/sun-shadow culling,
occlusion, dynamic LOD and material-state equality still run before
preflight; the final CPU MDI builder independently checks compatibility
and command thresholds. Stage 20's per-GPU-page admission remains
unchanged. The editor Scene → Batching panel now displays the amount of
**retained preflight workspace memory** in KiB, in addition to ready
groups and actually submitted mixed-model MDI groups.

This is a **source-level optimization, not yet a measured performance
gain**. Compile/CTest and a same-scene Metal CPU/GPU off/on/off comparison
remain a later phase per the user's instructions.


### Sparse epoch-stamped preflight items (Stage 22)

The Stage 21 `StaticCrossModelPreflightWorkspace` now avoids clearing the
**entire current ModelItem index and mode arrays** at each shadow or opaque
render pass. `beginPass(itemCount)` advances a separate 32-bit *item
generation*, clears only the actual ready-group records and keeps the
previous item scratch buffer capacity. The first scan writes a group index
and its generation stamp **only for actually eligible static items**.
`groupForItem(index)` returns the invalid sentinel for any entry with an
older generation; an unvisited item never inherits last pass's group.

After at least one group qualifies, `activateModes(itemCount)` makes the
mode map available without zero-filling every element. The promotion scan
calls `rememberMode(index, mode)` for touched eligible items; the sort,
CPU MDI candidate scan and adaptive instancing fallback now all use
`modeOf(index)`, which returns zero unless both the current generation
and active-mode flag match. Thus stale `1=MDI` or `2=instancing` from
previous shadow/main camera views is never read. When a pass has no
qualifying group, `clearModes()` performs a constant-time logical
invalidation rather than a linear array fill.

The item generation uses 32-bit stamps for compactness. At the extremely
rare wrap to zero, it resets **both stamp arrays once**, then resumes from
generation 1; the independent 64-bit open-addressed directory generation
continues its own existing wrap handling. `release()` on whole-scene
cache invalidation or renderer shutdown frees the index/mode/stamp
storage and resets both generations.

**Memory/performance tradeoff:** Each retained ModelItem slot now requires
2 bytes for the group index, 1 byte for its mode, and 4 bytes for **each**
of the group and mode generation stamps (11 bytes per high-water ModelItem
instead of the earlier 3 bytes, excluding vector capacity overhead).
This is intentionally more retained CPU memory to avoid repeated
full-scene zeroing on passes with few eligible objects. The editor's
`Preflight workspace retained` diagnostic includes all additional
stamp capacities. A scene that reuses nearly every item every pass may
not be faster; only same-camera CPU/GPU A/B can establish the benefit.
No shader, GPU command ABI, geometry, LOD, culling, or model ownership
behavior changes, and experimental flags remain disabled by default.


### Per-pass ranked static group sorting (Stage 23)

The optional `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` path previously
repeated multiple expensive `ModelItem` comparisons inside the full
`std::sort` comparator for every eligible pair. These included reading
resolved material-group IDs, shared GPU geometry-page pointers, vertex/
index buffer IDs and flags from the source Models even though Stage 18/21
preflight had already assigned identical values to each exact group.

`StaticCrossModelPreflightWorkspace::prepareSortRanks()` now runs **once
per pass after page-aware preflight and per-page adaptive admission**.
It orders only groups marked ready **and** permitted, with a compact
`uint16_t` rank assigned to each accepted group. The group comparator
uses the same ordering as the prior cross-model item comparator:
`materialGroup → geometryPage → GPU VB → GPU IB → drawFlags`. Page
pointers are ordered with `std::less` (not subtracted or cast to
integers). The group list has at most 2048 entries and is stored in a
renderer-owned reusable vector with high-water capacity. Sorting group
ranks costs `O(G log G)` per eligible pass (`G <= 2048`) instead of
repeating these multi-field comparisons across many item-sort
comparisons.

Only when **both** ModelItems are confirmed as accepted static
cross-model candidates does `renderModels` use the numeric group
ranks. Within a single exact group it continues the same
`lightmapPage → primitive pointer → original ModelItem index` order.
The original comparator is used for all other items, including
translucent objects, shadow-role or material-role batches, animated/
nonpooled models, the instance-material-palette experiment and
adaptive pages that declined MDI. This preserves the existing
material, visibility, LOD, shadow, GPU buffer and indirect command
requirements; the final MDI builder still validates the real
submission. The feature does **not** replace the overall
`std::sort(order_)` with a linear-time algorithm — the improvement is
a smaller per-comparison cost for qualifying static groups.

`StaticCrossModelPreflightWorkspace::retainedBytes()` includes the
new reusable vector of sorted group indices (at most 2048
`uint16_t` values); it is released with other workspace allocations
on full scene cache invalidation or shutdown. The public
`ModelRenderStats::staticCrossModelRankedGroups` metric and the
Scene → Batching panel report the number of groups whose sort key
was computed once per pass, not a claimed reduction in draw calls.
All experimental flags remain OFF by default. Real compile,
Metal screenshot parity and dense-scene CPU/GPU A/B remain deferred
until the later test phase.

### Ranked page buckets instead of global candidate sort (Stage 24)

When `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` and this view has permitted
page-aware static CPU MDI groups, the renderer partitions the current-view
visible `order_` into admitted (mode 1) and ordinary/fallback entries. This
matches the prior candidate-first total ordering. For at least **64**
admitted items it counts the already exact group ranks, performs an
`O(C + G)` prefix distribution of admitted items into **ranked page/material
buckets**, and sorts only each individual group by `lmPage`, primitive
pointer and stable ModelItem index. The ordinary suffix still uses the
**unchanged legacy comparator**, including material, joints, baked, GI and
geometry-sort policies; transparent phase 2 retains depth sorting. The
separate palette mode remains mutually exclusive with this opt-in path.

`StaticCrossModelPreflightWorkspace::bucketizePermittedPrefix` owns
high-water CPU scratch, validates every rank *before mutating the order*,
and returns failure for stale/invalid ranks. The renderer falls back
to the Stage 23 comparison-sort for fewer than 64 items or if bucket
preconditions fail. The bucket rank is per current visibility/material
preflight and never retained across views. Its high-water memory is
included in Scene → Batching `Preflight workspace retained`, and released
on full scene invalidation or shutdown. The panel separately reports
`staticCrossModelBucketedItems` and
`staticCrossModelBucketedGroups`, **not GPU draw calls**.

This reduces cross-group comparisons for large accepted prefixes but
does **not** promise lower end-to-end CPU or GPU time; the partition,
counting, and group sorting still cost work. Geometry page identity,
exact material compatibility, per-model LOD/occlusion/culling, the real
GPU command builder and adaptive page fallback are unchanged. Tests,
native compilation, Metal raster parity and actual benchmarks were
not executed at this stage; all experimental flags remain OFF.

### Exact-layout MDI zero-copy replay and allocation-free PBR palette (Stage 25)

The existing `drawModelIndirectBatch` per-pass positional
`IndirectLayoutEntry` cache still performs full per-item signature checks
(primitive identity, index count/range, GPU geometry offsets, base vertex).
On a validated hit it now passes the retained `entry.commands` vector
**directly** to the normal GPU buffer-upload API instead of assigning/copying
it into `indirectCommandScratch_`. Cache misses still build commands in the
renderer-owned scratch, and copy a finalized layout into the cache for the
following pass. Both paths use the same command threshold, pipeline, GPU
upload, backend fallback, triangle accounting and shader-visible ABI.
No GPU buffer upload or per-view geometry validation is skipped.

The CPU palette path now uses fixed `std::array<const Material*, 64>`
workspaces in BOTH the eligibility preflight and the final U3 slot
assignment, eliminating two small allocations per palette submission.
It preserves the same first-seen material-pointer order, 64-slot limit,
uniform fill and unique material tests; it does not allow different
textures/pipelines to merge.

Scene → Batching reports `mdiLayoutZeroCopySubmissions` and
`mdiLayoutCopyAvoidedBytes` (the command bytes the old warm path would
copy, only when the indirect backend submission actually succeeds).
These are **CPU-memory-copy savings**, not GPU traffic eliminated:
`createBuffer/updateBuffer` are still called exactly as before.
The existing `AIVERSE_MDI_LAYOUT_CACHE=off` mode bypasses reuse.
Source/connector consistency was inspected, but **native compilation,
CTest, Metal image parity and CPU/GPU benchmarks were not run**;
the experimental cross-model path remains OFF by default.

### Bounded MDI layout cache and safe asset retirement (Stage 26)

The CPU `drawModelIndirectBatch` positional command cache now has a
bounded capacity of **96 entries per phase** (five model phases) and **256
source items per entry**. Beyond either bound, it continues using the
existing renderer-owned uncached `indirectCommandScratch_` and still
submits exactly the same validated GPU indirect commands; it never drops
a draw or silently switches to another shader/material. Worst-case
retained signature and command payload are bounded by the fixed entry
and per-entry sizes, rather than growing indefinitely with city size.

Each cached signature now includes the source `Model*`, its
`geometryRevision`, GPU vertex/index buffer IDs, and the original
primitive pointer/index/base-vertex fields. The renderer still checks
the **complete current signature** before every warm replay.
`invalidateModelCachesFor(model)` retires *all* dependent command
layouts, including mixed-model groups with one unloaded member,
comparing stored pointers without dereferencing them. The public
`invalidateStaticModelCaches()` clears all command layouts on full
scene reload. `shutdownModels()` releases retained layout vectors
and scratch as part of renderer shutdown. Call invalidation on the
renderer/submission thread **between** passes, not during a borrowed
zero-copy command upload.

Every `beginScene()` advances a renderer-local ageing epoch and removes
entries idle for more than 120 scenes, including when no renderModels
phase executes. Positional layouts may shift after removal, producing
a safe full-signature miss/rebuild rather than stale reuse. Entry
vector memory is reclaimed when substantially underused after pruning.
Scene → Batching exposes retained CPU layout entries/bytes, cumulative
retirements and current-scene capacity bypass attempts; these figures
do not describe GPU memory or GPU performance. `AIVERSE_MDI_LAYOUT_CACHE=off`
continues to disable all command caching. Feature-first implementation
is **not yet compiled, CTest-validated, visually compared on Metal or
A/B benchmarked**.

### GPU-safe indirect command snapshot prefix replacement (Stage 27)

The model CPU MDI path creates its **dynamic indirect buffer** with
`BufferDesc::snapshotUpdates=true` and now explicitly opts into
`BufferDesc::discardOnUpdate=true`. This is a narrow RHI contract:
an offset-zero `updateBuffer` call replaces the entire **readable**
command prefix, and subsequent `drawIndexedIndirect` reads only that
many commands. The unwritten tail is undefined. It does **not** let
the backend overwrite in-flight GPU data: earlier command snapshots
remain private to their submission until fence completion. All other
dynamic/partial buffer clients default to `discardOnUpdate=false`.

Metal's existing triple-frame-slot, per-submission indirect-snapshot
pool continues to assign an independent `MTLBuffer` per submission.
For the opt-in command-prefix buffers it grows each snapshot buffer
geometrically (at least 256 bytes, doubling when needed), then reuses
the capacity at the same completed frame-slot/submission ordinal.
It skips copying an old snapshot's unused tail before an offset-zero
update; partial or non-opted-in updates preserve the existing
previous-contents path. With the **existing optional**
`AIVERSE_METAL_ICB_DIRTY_UPDATES=1`, a completed snapshot may have
128-byte chunks compared to the new command stream and only changed
chunks copied. This incurs additional `memcmp` CPU work and is still
OFF by default until measurements justify it. Under
`AIVERSE_ICB_PROFILE=1` the backend logs requested CPU writes avoided,
actual copied bytes and snapshot reallocations every 600 indirect
updates. The flag does not elide the logical RHI update/draw or
permit reuse of an in-flight snapshot.

Vulkan stores the same opt-in contract in `VBuffer`; for
offset-zero replacement it does not copy stale shadow-buffer tail
bytes to a newly selected GPU-safe version. All GPU serial/fence
versioning and the actual uploaded command prefix are retained.
Other buffers still preserve their prior partial-update semantics.

Scene → Batching reports `mdiIndirectCommandBytes` as bytes passed
to the RHI on successful indirect submissions and
`mdiIndirectPrefixOverwrites` as subsequent command updates. Neither
metric represents PCIe traffic, saved GPU frame time, or backend bytes
physically copied. The Metal snapshot log is the source for CPU
copy-elision measurements. The command ABI, material/geometry checks,
per-camera culling/LOD and fallback are unchanged. This stage was
source-checked only; **CMake, CTest, Metal screenshot parity, Xcode GPU
validation and A/B timing are NOT RUN**. Metal CPU ICB remains opt-in.

### Per-buffer backend-observed indirect upload diagnostics (Stage 28)

`gfx::Device::bufferUpdateStats(BufferHandle)` is a **read-only optional
instrumentation API**, returning `gfx::BufferUpdateStats` with
`available=false` for other backends or missing handles. Metal and Vulkan
implement it for dynamic indexed indirect buffers. It reports CPU operations
actually performed by the backend (no GPU fence wait or readback):
`updateCalls`, `requestedBytes`, `mappedWriteBytes` (CPU memcpy into
mapped GPU-visible memory), `cpuShadowWriteBytes`, `comparedBytes`
(bytes passed to memcmp), `skippedWriteBytes` (verified equal byte
spans skipped), `preservedBytes` (old contents copied into an updated
version), `allocations`, `reused`, and retained allocation/CPU-shadow
capacities. These counters are **cumulative since buffer creation** and
include the initial backend buffer allocations, rather than being per-frame
numbers. `mappedWriteBytes` includes creation-time writes and version
preservation where applicable; shadow writes are separate.

Metal's per-submission, per-frame-slot snapshot path counts each successful
update and exact memcpy/memcmp work. It distinguishes allocating a larger
snapshot from recycling a fence-safe existing one and computes retained
capacities without double-counting the current MTLBuffer when it aliases a
snapshot slot. The existing experimental dirty-byte flag remains off by
default. Vulkan tracks host-visible mapped copies and CPU shadow copies
separately, counts GPU-safe version allocations/reuses, and reports each
Vulkan memory suballocation size as an **allocation capacity estimate**,
not exact resident physical GPU memory. Renderer3D exposes only its own
`indirectCommands_` metrics via
`indirectBufferUpdateStats()`; the editor Scene → Batching panel displays
them alongside requested logical RHI payload bytes from Stage 27.

None of these data provide actual PCIe bandwidth, GPU copies,
executed GPU draw cost, driver memory accounting or frame timing. No
experimental feature is enabled globally. Source checks were performed
but **native build, CTest, Metal/Vulkan real-GPU validation, screenshot
parity and CPU/GPU A/B/A were NOT RUN**.

### View-scoped shadow vs opaque indirect CPU upload deltas (Stage 29)

`Renderer3D::renderModels` now samples the Stage 28 backend's
`indirectBufferUpdateStats()` at entry and exit for **phase 0 sun
shadow** and **phase 1 main-view opaque**. The renderer ignores
reflections (`geometryMainView_ == false`), auxiliary model passes
and phases 2–4 when attributing costs. If a new indirect command
buffer is created inside a pass, its baseline is zero, so the first
pass owns the backend's initial version allocations and
creation-time CPU copies. An existing handle must remain identical
across both samples; disappeared or unsupported handles produce no
misleading delta.

`gfx::bufferUpdateCounterDelta(before, after)` subtracts monotonic
lifetime counters, rejects invalid/reversed snapshots and
intentionally excludes point-in-time retained-memory capacities.
`gfx::accumulateBufferUpdateCounters` combines deltas when a model
pass repeats in one view. `ModelRenderStats::mdiBackendPassTransfers[2]`
and `mdiBackendPassSamples[2]` reset automatically with the renderer's
existing `modelStats_ = {}` at `beginScene()`. Editor → Scene →
Batching → Instancing and indirect details → Backend transfers by
Scene model pass reports actual backend CPU mapped/shadow memcpy bytes,
memcmp/dirty-elision/preserved bytes, version/snapshot allocation and
safe reuse counts and requested logical bytes for each of the two
passes. It retains Stage 28's separate buffer-lifetime totals.

This is **one beginScene/view's model-pass work**, not necessarily
one whole GPU frame: Editor Scene/Game, secondary camera targets and
multiple renderer calls can share a hardware frame. No GPU
readback, completion fence wait, GPU bandwidth or FPS claim is
involved. It excludes non-model uploads and ancillary passes rather
than claiming to explain total GPU memory use. All flags and actual
command-buffer behavior are unchanged. **Native build, CTest, Metal/
Vulkan image validation and frame-time A/B/A were NOT RUN**.

### Stage 30 — opt-in CPU MDI stage breakdown (2026-11-19)

The existing Scene → Batching **Profile Scene renderer** toggle
(`Renderer3D::setProfiling(true)`) now also collects
`ModelRenderStats::mdiCpuStages[0/1]` for sun-shadow
`renderModels(0)` and the primary camera's opaque `renderModels(1)`.
The new `CpuMdiStageProfile` is reset with `modelStats_={}` at
`beginScene`, does not retain model/geometry pointers and is backend-
and genre-independent. It includes the number of qualifying model
passes, MDI candidate scans, exact command-packing attempts, actual
successful MDI API submissions, pipeline warmups, under-threshold
pack attempts and backend submission fallbacks.

The timings are **synchronous CPU wall-clock measurements**:
`selectionMs` for per-item visibility/order selection;
`groupingSortMs` for material GPU-state classification, static
cross-model page preflight, ranking and order sorting;
`candidateScanMs` for compatible-run/primitive-run search and the
threshold decision (before command packing begins);
`commandPackingMs` for exact per-item layout signature lookup and
building/copying CPU indirect commands;
`pipelineSetupMs` for `drawModelBatch(... submit=false)` including
uniform/texture binding;
`bufferUploadMs` for RHI `createBuffer`/`updateBuffer`;
and `backendSubmitMs` for the synchronous
`drawIndexedIndirect` API call, **not** later GPU execution.
`totalMs` measures the whole model pass, including conventional
instancing and fallback draws, so the component timings should not
be summed to recreate it. The old `profile_` totals and opt-in
`AIVERSE_ICB_PROFILE` log output remain separate.

Normal operation does not add new high-resolution timestamps for
Stage 30 when `profiling_` is false. The editor displays these two
pass profiles under **Scene → Batching → Instancing and indirect
details → CPU MDI stage timings**. Reflection and auxiliary passes,
blended/outline/depth passes, terrain/water/other non-model renderers
are excluded from the two tables. No RHI ABI, GPU resource lifetime,
shader or model batching compatibility rule changes. Stage 29's
per-buffer CPU memcpy counters remain distinct, useful for comparing
`bufferUploadMs` against actual CPU memory writes.

**Verification:** source and GitHub write/read checks only; native
CMake, CTest, Metal/Vulkan raster parity and performance A/B/A **NOT
RUN**. No frame-time gain or stable production Metal ICB claim.

### Fixed 120-sample rolling CPU MDI stage history (Stage 31)

The Stage 30 opt-in `profiling_` timing path now keeps a **fixed,
allocation-free ring** of the last 120 eligible `renderModels`
invocations, separately for `phase=0` sun shadow and
`phase=1` main-camera opaque. `Renderer3D::CpuMdiRollingHistory`
lives outside `ModelRenderStats`, so `beginScene()` resets its
own per-view totals but **does not reset the rolling window**. Each
eligible invocation stores the *difference* between the Stage 30
cumulative pass timing fields at entry and exit, including the
zero-visible-item early-return case. No samples are appended when
profiling is disabled; the existing historical ring remains readable.

`Renderer3D::cpuMdiRollingSummary(phase)` provides a read-only
`CpuMdiRollingSummary` with count, arithmetic mean and
**nearest-rank P95** separately for eight CPU metrics:
total model-pass time, selection, material grouping/sort, candidate
scan, command packing, pipeline setup, buffer upload and synchronous
backend submit. `ceil(0.95 * N) - 1` selects the P95 array index.
It runs only on explicit UI/API read and sorts a bounded 120-double
stack array for each metric, without allocating or disturbing the
render thread's current history. The ring writes one eight-double
record per eligible model pass, independent of scene object count.

Scene → Batching → Instancing and indirect details → **CPU MDI stage
timings** shows mean/P95 beside the current view's Stage 30 totals.
The per-pass ring counts *eligible renderModels invocations*, not
whole engine/GPU frames: multiple viewports and repeated model passes
may contribute multiple samples, and reflections/auxiliary passes
remain excluded. Whole-scene invalidation and renderer shutdown call
`clearCpuMdiRollingHistory()` to prevent old scenes contaminating new
measurements; simply disabling profiling pauses collection.
This feature only records synchronous CPU timings; no GPU stamps,
memory bandwidth, GPU synchronization or batching behavior changes.

**Build/CTest/Metal/Vulkan visual and CPU/GPU A/B/A validation:
NOT RUN**, per feature-first direction.

### Automatic P95 CPU MDI variability warnings (Stage 32)

`Renderer3D::cpuMdiRollingSummary(phase)` now classifies **potential
intermittent CPU latency spikes** during the existing read-only
Stage 31 mean/P95 computation. It is a conservative **heuristic**:
wait until at least **30** eligible model-pass samples, and flag a metric
only when its nearest-rank `P95 >= 0.25 ms`,
`P95 - mean >= 0.15 ms`, and `P95 >= 1.8 * mean`.
All three numerical conditions must hold. The first 30 samples are a
warm-up baseline, not a guaranteed stable workload; slowly changing
cameras or editor operations can still produce legitimate variation.

The summary exposes `hasBaseline`, `spike[8]`,
`highlightedStages`, `dominantStage` and
`dominantExcessMs`. Metric index 0 is **total model-pass CPU time**,
including conventional instancing/fallback; indexes 1–7 are tracked
sub-stages. `highlightedStages` excludes overlapping total;
`dominantStage` selects the flagged sub-stage with the largest
absolute `P95 - mean` difference, breaking ties by existing metric
order. If only the total pass varies, the UI explicitly says no
individual MDI stage crossed the threshold, avoiding false attribution.

Studio Scene → Batching → Instancing and indirect details → CPU MDI
stage timings now highlights flagged rows in warm warning text and
shows the highest-excess stage, sample count, and threshold
explanation for **Sun shadow** and **Main opaque** independently.
Before 30 measurements it shows progress toward the baseline.
The existing `Profile Scene renderer` toggle still gates collection;
turning it off pauses history. No new per-draw timestamps, dynamic
allocations, GPU readbacks or extra sorting are added to the render
loop; classification runs only when a rolling summary is requested.
Scene invalidation/shutdown still clears history as in Stage 31.

Warnings identify **CPU P95 variability**, not proven bottlenecks,
regressions, GPU stalls, FPS loss or hardware time. Their thresholds
are diagnostic defaults, not optimization budgets, and a stable slow
stage will not be classified as a spike by this method. **Native
compile/CTest, Metal and Vulkan correctness, GPU parity and real
CPU/GPU timing A/B/A: NOT RUN** under feature-first priority.

### Independent sustained CPU stage cost detection (Stage 33)

The opt-in Stage 31/32 120-sample read-only summary now calculates
**P50 (nearest-rank median)** alongside its existing arithmetic
mean and P95 for each of eight CPU metrics. This uses the **same**
already-sorted fixed scratch array on UI/API read; no new per-frame
sorting, allocation or high-resolution timestamp is introduced.

After at least **30 eligible samples**, a *specific* sub-stage
(indices 1–7) is marked as a **sustained CPU cost candidate** when
both conditions hold: its median `P50 >= 0.35 ms` and its
arithmetic mean represents at least **15% of the mean whole model
pass time**. The median makes a few isolated slow invocations
insufficient to trigger a sustained warning; the share guard avoids
highlighting a relatively expensive stage that consumes too little
of the overall model pass to be a likely optimization target.
Index 0 is an **overlapping whole-pass total**, never a specific MDI
stage. Neither criterion requires P95 variability: a consistently
high stage is deliberately detected even with no Stage 32 spike.

`Renderer3D::CpuMdiRollingSummary` exposes `p50Ms[8]`,
`sustained[8]` (index 0 always false), `sustainedStages`,
`dominantSustainedStage` (1–7 or -1),
`dominantSustainedP50Ms` and `dominantSustainedShare`.
The dominant stage has the largest P50 among sustained candidates;
exact ties retain stage order. Stage 32 `spike[]`/dominant-spike
results remain independent; a stage can be flagged by *both*.

Studio Scene → Batching → Instancing and indirect details → CPU MDI
stage timings now shows mean/P50/P95 columns, separate **orange P95
spike** and **blue sustained CPU cost** notices, and combined labels
when both conditions occur. Before 30 samples, neither classification
is emitted. Shadow and main opaque remain separate, while reflection,
auxiliary views and other genres' rendering pathways are unaffected.
Disabling profiling pauses collection and full scene invalidation
or shutdown clears the rolling history exactly as Stage 31.

These are diagnostic CPU wall-time classifications, **not** proof of
a performance regression, GPU bottleneck or speedup. The stage-time
components can overlap and their ratios to total pass time should
not be added together. Thresholds are compile-time conservative
heuristics, not guaranteed CPU frame-time budgets and do not control
batch admission. **CMake/CTest, GPU raster parity, real device tests
and fixed-scene CPU/GPU A/B/A NOT RUN** under feature-first direction.

### Vulkan GPU-fenced indirect failure handling validation (2026-11-19)

A source audit of Stage 27/28 found that `VulkanDevice::writableVersion`
could return the *old current version* when an allocation for a new
dynamic version failed. Its caller would then memcpy new indirect
commands over a still-in-flight VkBuffer, breaking the central
per-submission GPU lifetime invariant. The allocator now returns a
nullable pointer and refuses that unsafe fallback. The normal
`updateBuffer` path selects a safe destination **before** writing
the CPU shadow, leaving both unchanged on allocation failure.
Dynamic growth and immutable→dynamic growth also allocate a new
buffer first and only retire the previous VkBuffers after successful
allocation; failed growth preserves existing versions and metadata.
These changes do **not** introduce a new GPU command path or weaken
serial/fence versioning.

`tests/render/model_batching_policy.cpp` gains CPU-only assertions
for rank/bucket preflight, generation invalidation, bounded group
selection and `gfx::bufferUpdateCounterDelta` / accumulator behavior.
An actual Vulkan allocation-failure regression requires a separately
instrumented/fault-injecting Vulkan device; these CPU tests do not
simulate GPU OOM or certify Metal image parity.

### Scene viewport runtime toggle for pooled static CPU MDI

The Studio Scene Batching popup exposes **Static cross-model CPU MDI**
with three modes: **Inherit env (default OFF)**, **Enable in Scene
only**, and **Disable in Scene only**. The renderer's corresponding
`StaticCrossModelBatchMode` value is evaluated at `beginScene()`.
Game and other views retain the environment-controlled opt-in because
Scene restores the override after rendering. This avoids setting
`AIVERSE_STATIC_CROSS_MODEL_BATCHES` for the whole editor process
just to inspect static grouped batching. The Scene-only mode does
not enable GPU-authored ICB, switch shared geometry pooling on, bypass
normal MDI disable/safety flags, alter PBR/material equivalence or
change rigid/LOD/transparent eligibility. Adaptive per-page CPU
feedback remains gated by its own environment setting *and* the
effective cross-model mode. The existing preflight/ranked-group
stats are shown when either the process or Scene opt-in is active.

This is a **feature-only implementation**. Native build/CTest,
Metal/Vulkan raster parity and performance A/B/A were not run under
the user's explicit requirement to finish all batching functionality
before starting tests.

### Runtime CPU MDI policy controls per render view (feature continuation)

The genre-independent `Renderer3D` exposes
`CpuMdiPolicyMode { Environment, Enable, Forbid }` and independent
`setCpuMdiAdaptiveMode`, `setMdiLayoutCacheMode` and
`setMdiGeometrySortMode` setters and getters. These resolve on
`beginScene()` to effective per-view state. When each remains
`Environment`, the original `AIVERSE_CPU_MDI_ADAPTIVE=1`,
`AIVERSE_MDI_LAYOUT_CACHE=off/0` (default cache ON) and
`AIVERSE_MDI_GEOMETRY_SORT=1` flags have unchanged semantics.
Adaptive CPU page feedback still requires effective static cross-model
MDI enabled; the CPU MDI geometry sort keeps material palette ordering
independent and never skips per-item material/geometry checks.

`setCpuMdiAdaptiveBudgetOverrideUs(double)` accepts `0` to inherit
`AIVERSE_CPU_MDI_MAX_US_PER_COMMAND` (default 25 µs per command),
or a runtime budget from 1–1000 µs, inclusive. Invalid values
are ignored; it affects admission feedback, not GPU time or global
process settings. Studio Scene → Batching now provides the three
matching Inherit/Enable/Disable modes and an adaptive budget slider.
All policies are reset to `Environment`, budget 0, immediately after
Scene rendering and before Game uses the shared renderer. Regular CPU
MDI, Metal GPU-authored ICB, render-safe fallback and actual GPU
snapshot/fence ownership remain unchanged.

This is a feature-only implementation; **no native CMake, CTest,
Metal/Vulkan raster, OOM stress or frame-time benchmark was run**.
Keep experimental static cross-model batching default OFF.

### Per-host adaptive CPU MDI feedback isolation

`CpuMdiPageKey` now contains a `uint64_t scope` that is included in
equality and hashing. `Renderer3D::setCpuMdiAdaptiveScope(uint64_t)`
selects that namespace for the next renderer view. It is **only**
used to find/update per-page CPU MDI adaptive EMA and probe state:
shader/material, geometry-pool page matching, GPU buffer handles,
render order and indirect command encoding are unchanged.

The default scope is zero to preserve legacy native/game hosts.
The editor Scene window uses its own stable nonzero scope while it
renders and resets scope to zero before Game. Thus measuring slow
static batches in Scene cannot suppress otherwise eligible Game
batches on an identical GPU page. The bounded per-page directory
continues to be capped at 256 tracked entries **across scopes**;
when full, untracked pages use the ordinary fallback-safe MDI policy
rather than inferred cross-host feedback. Full scene invalidation
still clears all scope entries and weak page references prevent raw
address reuse. Display-only aggregate feedback may span scopes,
but **admission uses the scope-keyed page feedback only**.

Functional source integration only. Native compilation, CTest, GPU
raster parity, in-flight GPU fences and frame-time benchmarks remain
NOT RUN per user feature-first instruction.

### Scoped CPU MDI configuration snapshots for native game hosts

`Renderer3D::CpuMdiPolicyOverrides` is a value-type record containing
cross-model CPU MDI, adaptive page feedback, cached command layouts,
geometry sort, adaptive budget in microseconds and feedback scope.
`cpuMdiPolicyOverrides()` returns the **configured overrides**, not
resolved scene-time effective states; `setCpuMdiPolicyOverrides`
restores a previously captured configuration using the validated
individual API setters. It does not copy or own cached GPU buffers,
renderer models, scene materials or performance feedback.

The Studio Scene window captures the current config, temporarily
sets its independent policy/scope for rendering, and restores the
**exact prior config** afterward. In particular, a game host that
already configured explicit nondefault CPU batching policies no longer
has them accidentally reset to process Environment after opening
Studio. Other renderer states (LOD, culling, graphics debug etc.)
remain governed by the preexisting Scene lifecycle; this record
only concerns CPU MDI policies. No feature is enabled by default.

This is a feature-only API; CMake/CTest, visual GPU parity and A/B
performance tests remain unexecuted until all batching features
are finished.


### Exception-safe CPU MDI policy scope for game/editor hosts

Use `Renderer3D::ScopedCpuMdiPolicyOverride` for an owned
render-view invocation rather than remembering to issue a separate
`setCpuMdiPolicyOverrides(old)` call after every branch.
The guard restores precisely the **previous** configuration (including
host-specific adaptive feedback `scope`) at C++ scope exit, even on
early return or exception. Nested guards restore in reverse order.
The Scene viewport now scopes its baseline/optimized render(s) this
way, without replacing an already configured game host's policy with
process defaults. Native/FPS/RTS hosts can use the identical API:

```cpp
auto local = renderer.cpuMdiPolicyOverrides();
local.crossModel = Renderer3D::StaticCrossModelBatchMode::Enable;
local.adaptiveScope = stableViewportNamespace;
{
    Renderer3D::ScopedCpuMdiPolicyOverride scope(renderer, local);
    // draw / beginScene / render for this view only
}
// Previous renderer CPU policy is restored here.
```

This is a *configuration-only* stack guard: the renderer must outlive
it, calls must not race across threads, and the old effective flags
will be re-resolved at the **next** `beginScene`. It does not roll back
draw commands, GPU resources, scene profiling, previous page feedback,
LOD, culling or material state. The isolated scope key partitions page
EMA but not its bounded global storage. Explicit backend safety/MDI
eligibility still takes precedence. No experimental feature turns on
by default and there is no measured performance result yet.


### Preserve host model batching and LOD settings in Studio

`Renderer3D::ModelRenderPolicyOverrides` is a separate host-side
configuration record for `instancing`, `multiDraw`, `gpuAuthored`,
`culling`, `lod` and `lodFadeWidth`. Read the configured state via
`modelRenderPolicyOverrides()` and set it via
`setModelRenderPolicyOverrides(...)`. The nested
`ScopedModelRenderPolicyOverride` captures the previous values and
automatically restores them at scope exit. It also has an overload
accepting an explicit temporary policy. Combine it with
`ScopedCpuMdiPolicyOverride` to isolate one native render view.

Studio Scene's baseline/optimized batching comparison now restores
the **prior native/Game values**, not hard-coded true defaults. The
renderer's GPU pipeline/indirect safety rules, per-camera culling and
LOD calculation are unchanged. This snapshot does **not** rewind
shader pipeline `instanceLimit`, GPU commands, material arrays,
profile/debug toggles or ECS state; explicit shader-layout changes
retain their previous behavior. All experimental modes stay OFF unless
separately enabled. This is a feature-only update; native tests and
GPU A/B measurements are not yet performed.
