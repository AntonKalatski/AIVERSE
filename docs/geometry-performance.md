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
checks all standard `ModelDraw` visual fields and world transform too.

CPU runtime LOD is supported for rigid models: a packet remembers the
selected LOD mesh per node and re-selects the LOD against the **current**
main-camera and sun-shadow projected error on every submission. When the
chosen mesh differs, it rebuilds. When a transition is in progress (LOD
crossfade), it bypasses packet reuse entirely so fade weights are always
current. Experimental GPU-compute LOD remains on its existing uncached path.

Eligibility still excludes node material overrides and material variants,
skinned/morphed/animated models, runtime-mutable nodes, cluster geometry,
vertex displacement, baked GI and active LOD diagnostics. No simulation
state is cached or made genre-specific. The optional RTS view bridge marks
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
a stable integer `assetKey`, `appearanceRevision`, draw flags and visibility;
there are no renderer/model pointers or floats in the authoritative ECS
component. `SceneHierarchyRender::hierarchyMatrix` performs fixed-point
to float conversion with render-origin subtraction. The bridge is optional:
old projects do not change until they register these components and call
the adapter.

Source templates hold non-owning `Model*` and `Primitive*` references.
Cached material pointers are explicitly rebound from the current live vector
before each replay. `Renderer3D::invalidateStaticModelCaches()` clears both the
transform cache and packet cache before asset destruction/reload; the editor
does so from `reloadRules()`. The implementation caps each packet at 128
items and combined retained item/material/LOD snapshot payload storage at 64 MiB, with periodic
expiry of unused packets. A cache miss always uses the full original draw path.

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
