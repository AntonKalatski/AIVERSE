# AIVERSE View: genre-independent scene

The canonical engine-level scene presentation class is
`aiverse::View` (alias `aiverse::view::View`),
available from `<engine/view/View.h>` and CMake
`AIVERSE::View` (legacy spelling `AIVERSE::view`).

It is not an RTS view, FPS view, map editor view or RPG view. All
hosts project their objects into generic scene data. The core
does not receive game orders, movement intents, network actors,
weapon IDs, AI states or gameplay module classes.

## Universal model and mesh packets

```cpp
#include <engine/view/View.h>

aiverse::View scene;
std::vector<aiverse::view::Object> objects;

aiverse::view::Object object;
object.id = stableSourceId;
object.generation = sourceGeneration;
object.draw.model = model;
object.draw.world = modelToWorld;
object.graph = compiledGraph; // optional
object.parameters.set("speed", animationSpeed);
objects.push_back(std::move(object));

// Every update uses a separate presentation-only ECS World:
scene.update(objects, frameDt);
scene.submit(renderer, objects); // inside an initialized renderer frame
```

For ordinary meshes:
```cpp
aiverse::view::MeshObject mesh;
mesh.mesh = uploadedMesh;
mesh.world = modelToWorld;
mesh.color = {1, 1, 1, 1};
scene.submitMesh(renderer, mesh);
```

`View::submitOne` forwards one generic model packet; it can accept a
host-precomputed node pose, baked GPU sample and all ModelDraw styling.
Models, meshes and material pointers are borrowed; host must keep them
valid until submission completes.

The animation side is `MotionWeave` and its generic
`MotionWeaveSourceBridge`. No genre-specific class is required,
and the same worker scheduler/animation graphs serve every project.
The renderer is still `Renderer3D`; this new View does not invent
a second material or GPU backend.

## Dependency direction

```text
Game, Editor, UI, Network, Simulation host adapters
                    ↓
             AIVERSE View
                    ↓
      MotionWeave + Renderer3D / RHI
```

Neither `engine/view` nor MotionWeave may import `modules/rts`,
`modules/fps` or any other gameplay module. World simulation remains
authoritative and separate from presentation.

## Migration status from legacy RtsView

The old `game::RtsView` is a compatibility adapter, not the canonical
View. A subset of simple AnimationGraph objects already delegates
pose evaluation and styled ModelDraw submission to `aiverse::View`;
complex procedural/contact IK and RTS-specific object decisions remain
in the game adapter until parity is established.

The FPS client uses the SAME View for character models and primitive
meshes, proving the API has no genre-specific branches. Further
migration is needed for generic layers/camera, terrain, roads, fog
and field/planet rendering that are historically entangled with
the RTS module. Do NOT rename the legacy class without removing
its RTS model and Rules dependencies.

## Deferred validation

Tests `view_genre_independent` and `view_genre_boundary` are registered,
alongside MotionWeave tests. **NOT RUN** by user's request;
CMake, CTest, TSAN, FPS/RTS screenshots and performance still need
real execution before production is claimed ready.

Every stage must update `AGENTS.md` and append a separate committed
entry in `docs/agent-handoffs/animation-universal.md`.

## Camera API migration

The engine renderer's target/orbit camera is now named
`aiverse::render::Camera3D`. `render::RTSCamera` remains an
exact `using` alias for source compatibility: no duplicate data,
no copy/conversion and no change to the existing camera math.

`View::beginScene(renderer,camera,width,height)` is a
genre-independent entry point to Renderer3D's existing
beginScene pipeline, so a client/editor no longer needs to
refer to an RTS-named camera type. The render backend, viewport,
shadows and original camera parameters are unchanged.

### Generic immediate geometry

`view::LineObject` and `view::TriangleObject` are unowned
scene-space geometry packets. Static `View::submitLine(renderer,line)`
and `View::submitTriangle(renderer,triangle)` forward their
vertices/colors to the existing Renderer3D pass. They do not
know what any diagnostic, selection, fog blip or game event
means; the host decides which geometry to submit.

The transitional game adapter already routes selected projectile
tracers and fog-of-war blip triangles through these shared packet
methods. All other legacy geometry remains valid on the old
immediate-mode renderer until its own host extraction.

## Mixed scene packet stream

Use `view::ScenePacket` and `view::ScenePacketBatch` when a host
must submit a collection of heterogeneous model, mesh, line
and triangle draw packets in the **exact original order**:

```cpp
aiverse::view::ScenePacketBatch packets;
packets.reserve(expectedDraws);
packets.add(modelObject);
packets.add(meshObject);
packets.add(lineObject);
scene.submitPackets(renderer,packets);
```

A ScenePacket is a tagged variant over the existing public
`Object`, `MeshObject`, `LineObject`, and `TriangleObject`.
The engine View dispatches one item at a time through
the same `submitOne` / `submitMesh` / `submitLine` /
`submitTriangle` paths as direct callers. It does not
sort, reorder, re-evaluate animations or change internal
Renderer3D batching. The host owns all borrowed model,
mesh and material resources for the duration of submit.

## Surface sampling

The generic header `<engine/view/Surface.h>` exposes
`view::composeSurfaceHeight(x,z,base,layer)` and
`view::sampleSurface(x,z,normalStep,heightSampler)`.
These are allocation-free templated algorithms for combining an
arbitrary host-authored base surface with a height modifier,
and for computing a visual terrain normal from five height
samples. No MapGrid, roads, physics, genre or entity type
appears in this engine-level API.

The historical game adapter's `groundAt` now composes
its own map-height policy with its own road surface policy
through that helper, retaining the previous values and
placement semantics. Future hosts may supply their own
heightfields, planets projected to local surfaces, mesh
raycasts or procedural floors without modifying View core.
This query is **presentation-only**, not a replacement
for deterministic character or vehicle collision.

## External scene layer providers

`engine/view/SceneLayerProviders.h` introduces
`ViewLayerProviders`. A host registers opaque named callbacks
and decides when they are enabled. `collect(ViewLayerQueue&,
ViewFrameInfo)` schedules frame work in registration order.
`ViewLayerQueue::flush()` invokes it synchronously on the
render thread, before any borrowed model/terrain/camera state
may expire.

The generic layer API knows only names, frame seconds, dt,
revision and presentation callbacks. No genre-specific
resource schemas live in engine View. The compatibility
game-view host now registers its separate path and
shoreline presentation passes as outside providers while
retaining their existing geometry algorithms and order.

### Fully prepared draw packets

Use `aiverse::View::submitModel(renderer, draw)` for a
precomputed `render::ModelDraw`. Unlike `View::submitOne`,
this does not read animation ECS or require a source ID.
It forwards existing staged/LOD model variants, baked samples,
per-node procedural poses, attachments, materials and flags
through the same Renderer3D model pipeline.

The old game-side draw implementation now forwards **all**
base model and attachment ModelDraw submissions this way,
not only the simple AnimationGraph subset. Complex authored
animation still runs inside the legacy host adapter until
its state is migrated; it is **not** recomputed by the View.
Model and material memory is borrowed only for the synchronous
submission.

## Reusable visibility field

The previously game-local visual fog implementation has moved to
`engine/view/VisibilityField.h/.cpp` under
`aiverse::view::VisibilityField` and
`aiverse::view::VisibilityLook`. It accepts only a host-provided
0/1/2 byte visibility mask, visual settings, elapsed presentation
time and a height sampler for the GPU sheet. Grid blur/fade,
visibility texture upload and draw are reusable in strategy,
stealth, exploration, spectator views and editors alike.

`modules/rts/view/RtsFog.h` remains a compatibility-only
header: `game::FogOfWar` and `game::FogLook` are aliases to the
engine types and `game::parseFogLook` forwards to
`view::parseVisibilityLook`. Existing RTS project settings and
editor behavior can retain their original names.

Link the optional compiled presentation effect target
`AIVERSE::view_visibility` when using the standalone
visibility system. This is a render effect, not the
authoritative visibility or enemy-detection algorithm.

### Portraits, previews and projectiles

The legacy game's portrait off-screen renderer now enters its
existing render-target frame through `View::beginScene` and
submits material previews and CPU poses through
`View::submitModel`. The view does not own color/depth
render targets or change graphics pass ordering.

Ordinary flat-map, procedural and planetary projectile meshes
are submitted as generic `view::MeshObject` packets through
`View::submitMesh`, including their existing transforms,
unlit flags and colors. `submitMesh` and `submitMeshes`
are stateless static entry points; legacy object calls remain
source-compatible. This is a **render submission migration**:
the game host still owns projectile gameplay, planet tangent
frames, and previews' simulation-specific data.

### VisibilityField buffer API and ownership

The visibility effect accepts any contiguous byte mask without
allocating a temporary `std::vector`:

```cpp
#include <engine/view/VisibilityField.h>
aiverse::view::VisibilityField visibleLayer;
visibleLayer.init(device);
visibleLayer.setLook(viewLook);
visibleLayer.build(device,width,height,heightSampler);
visibleLayer.update(std::span<const uint8_t>(mask.data(),mask.size()),
                    width,height,viewSeconds);
visibleLayer.draw(renderer,viewSeconds,isLowTier);
// On shutdown, before device destruction:
visibleLayer.shutdown();
```

Existing vector-based `update(vector,...)` calls remain supported
and forward to the same zero-copy span update implementation.
The host is responsible for mask semantics and lifetime during
the synchronous update. This is visual fade/blur and GPU
rendering only, never simulation-side visibility authority.

## Generic circular overlays

`view::CircleXZObject` and `View::submitCircle` send a
world-space ring directly to Renderer3D. The scene packet
variant also accepts `CircleXZObject`, preserving ordered
batch submission alongside models, meshes, lines and triangles.
The host decides whether rings represent selection,
markers, shadows or visual debugging. No game state
enters the View.

The game compatibility view routes previously hard-coded
radar marker rings and simple projected circle shadows
through these same generic packets.

## Visibility and scene-layer lifecycle

The reusable `view::VisibilityField` has a **visual-only**
0/1/2 visibility mask supplied by the host. Import
`<engine/view/VisibilityField.h>` and separately link
`AIVERSE::view_visibility` when the effect is needed.
Call `init(device)`, `setLook()` (or
`parseVisibilityLook` from generic JSON), `build(device,
w,h,heightSampler)` when the ground geometry changes,
`update(mask,w,h,seconds)` each presentation frame, and
`draw(renderer,seconds,lowTier)` in the desired
render pass. Call `shutdown()` before releasing its GPU
device. The field stores GPU textures and must remain on
the render thread; callers own the mask source and whether
terrain shader fog or a separate sheet is drawn.

`ViewLayerProviders` is intentionally only an ordered
registration/composition seam. Host-owned providers
may add terrain, visibility, roads, planet or post layers
without importing any game types into `engine/view`.
A `ViewLayerQueue` must be **flushed synchronously**
before returning from the frame scope; callbacks frequently
borrow textures, camera matrices and terrain references.

## Shared camera-side frustum (flat terrain and planets)

The header-only `engine/view/Frustum.h` provides
`view::SideFrustum::fromViewProjection(vp,normalized)`,
`visibleAABB(min,max)` and `visibleSphere(center,radius)`.
It extracts only four camera side planes, avoiding near/far
clip-convention differences. Flat terrain chunk culling uses
raw planes and AABB positive vertices; planet body and
chunk culling uses normalized planes and sphere radius.
The host alone owns quadtree LOD, horizon occlusion and
rendering policy. All CTest validation remains **NOT RUN**.

## Spherical horizon culling

`engine/view/Horizon.h` exports
`view::sphericalCapHiddenByHorizon(cameraLocal,
occluderRadius,capAxis,cosCapHalfAngle,maxSurfaceRadius)`.
It is a reusable floating-point visibility test for
any curved surface, sphere chunks, visual planet and
orbital scene; it knows nothing about game planets,
RTS/FPS or data models. The host supplies the camera
in the body's local coordinate frame and its precomputed
angular cap and radius bounds.

The old planetary renderer delegates its horizon
angle comparison to this helper, while retaining
its LOD/build budget, face tree and graphics pipeline.
Inside the occluder or with invalid occluder radius
the helper returns "not hidden" conservatively.
Test `view_spherical_horizon` is deferred (**NOT RUN**).

## Generic rectangular quad-grid geometry

`engine/view/GridGeometry.h` exports
`view::appendQuadGridIndices(vector<uint32_t>&,columns,rows)`,
a safe vertex-index tessellation primitive for regular 2D grids,
heightfield patches and cube-sphere face patches. It appends
triangles using the same a-d diagonal and winding as the
existing flat and curved terrain builders. The terrain
host retains its own LOD skirts, color sampling and morphing;
planet rendering retains outward normal/winding correction,
surface atlas mapping and horizon policy. The generic
algorithm only receives column/row counts and a vector of
indices, with no game-map, physics, height or render rule.
Deferred CTest: `view_grid_geometry` (**NOT RUN**).

### Per-frame geometry visibility caching

`SideFrustum::fromViewProjection` now caches all four
plane-normal lengths at camera setup. Sphere culling
uses those cached values instead of computing four
square roots for **every** planet/body/chunk.
`SphericalHorizon::fromLocalCamera(cameraLocal,occluderRadius)`
similarly caches eye direction and horizon base angle
**once per spherical body**; `hidden(capDir,cosCap,maxRadius)`
tests many chunks with no redundant camera-length
calculation. The original single-call horizon function
remains as a compatibility helper.

The quad-grid tessellator uses widened 64-bit intermediate
index arithmetic for large yet valid 32-bit grids.
These are static performance/correctness changes; all
benchmarks, CTest and graphics validation remain **NOT RUN**.

## Shared oriented triangle normal / winding

`engine/view/TriangleFacing.h` provides
`view::orientTriangleTo(a,b,c,outwardHint)`, returning
a normalized face normal, flip-winding flag and degenerate
triangle status. Road meshes use it to orient local
triangle vertices and UVs; spherical face meshes use
the same test before correcting the patch's triangle
indices. The host retains its material policy, UV
mapping, face basis and any geometry topology.

It has no genre, game geometry schema, simulation,
AI or physics dependencies. Deferred CTest:
`view_triangle_facing` (**NOT RUN**).


## Unified mixed-packet animation update

Hosts that build one ordered `view::ScenePacketBatch` can now use that
same batch to drive MotionWeave before submitting the renderer commands:

```cpp
aiverse::View scene;
aiverse::view::ScenePacketBatch batch;
batch.add(animatedObject); // view::Object with stable ID + optional graph
batch.add(staticMesh);     // view::MeshObject
batch.add(debugLine);      // view::LineObject

scene.update(batch, frameDt); // evaluates Object packets once
scene.submitPackets(renderer, batch); // keeps mixed draw order
```

`View::update(const ScenePacketBatch&,float)` extracts only Object packets
into reusable frame-local scratch storage. It keeps source ID/generation,
AnimationGraph, budget and visibility semantics identical to the original
`update(span<const Object>,float)`; other packet types do not become
animation sources. The bridge synchronizes missing sources, so a source
removed from the batch will leave the presentation World on the next update.
Do **not** also call `update(objects,dt)` in the same frame: that would
advance animation twice. All packet resources remain host-owned until
synchronous render submission. This is an API convenience, not a new
render pass or a change to authoritative gameplay.

## RTS single-writer animation boundary

The legacy RTS scene adapter selects only simple, pure pose graphs
(no procedural bones, contact IK, custom traits, attachments, stage/LOD
switches or host-injected animation overrides). It stages those sources
in one `ScenePacketBatch` and calls `View::update(batch,dt)` once
before ordinary scene rendering. View's presentation World is the
single evaluator for each selected source.

`RtsAnimate.cpp` then obtains the evaluated pose from View. When
`EntityAnim::genericViewPose` is true, legacy `Animator::update` and
bind-pose fallback **must not execute**, even if the model has imported
clips. Objects that fail eligibility or pose validation keep the existing
legacy graph/clip/procedural path, including terrain contact IK. RTS
still supplies material, world transform and draw style to View's
`submitOne`; no second MotionWeave evaluation is requested during
submission. This is a presentation-only migration: simulation,
network/save formats and deterministic checksums are unchanged.

**Verification pending:** the integrated RTS regression suite,
animated-model visual comparisons and macOS Metal path have not been
executed for this stage.

## RTS animation owner fallback verification (Stage 79)

When a pure graph object becomes ineligible for the generic View path,
`RtsAnimate.cpp` validates the current frame's generic pose before
recording the owner transition. If it cannot consume a valid pose, it
reinitializes **both** legacy graph and imported clip Animator clocks
(`setModel(nullptr)`, then `setModel(&model)`) and resets the clip
selection. This prevents a reused RTS object from resuming a stale
pre-MotionWeave imported-clip time after dynamic asset/controller changes.
When View still provides a valid pose, neither legacy branch updates
its own animation player. Graph evaluation and resulting pose are still
presentation-only; authoritative simulation state is unaffected.

`tests/view/view.cpp` adds a CPU regression scenario with an actual
single-bone Model, a sampled Walk AnimClip, a pure graph using that clip
and a dormant legacy Animator. The scenario checks per-frame View graph
clock and local pose progression, that the dormant Animator clock does not
advance, View-source removal, legacy clock reinitialization and a new
View player after re-entry. This checks the reusable animation contract;
**it does not directly invoke `RtsView::animate`**, so RTS in-game parity
and rendering still need native/visual integration validation.

## Generic transformed bounds for mesh presentation

`engine/view/GeometryBounds.h` exposes
`view::transformAabb(localMin,localMax,worldMatrix)` and a
`view::WorldBounds { min, max, valid }` return value. It transforms
all eight corners, handles rotation, non-uniform/negative scale and
translations, and returns an invalid result for reversed/non-finite
bounds or non-finite output coordinates. Only renderer float math is
involved. Gameplay collision/physics, authoritative ECS and network
serialization do not depend on this presentation helper.

The RTS adapter now consumes this helper when preparing bounds for
animation environment contact meshes, replacing a local eight-corner
AABB loop. Source-specific node/primitive IDs and contact decisions
remain in the game adapter; this shared geometry operation belongs
to View and can be reused by any genre or editor.

Deferred test: `view_geometry_bounds` in
`tests/view/geometry_bounds.cpp`. Native build and execution are
**NOT RUN** for this integration step.

## Genre-free ground ribbons (View Stage 81)

`view::QuadXZObject { start, end, width, rgba, y }` is an ordered
`ScenePacket` for drawing an XZ ground ribbon. Use
`aiverse::View::submitQuadXZ(renderer, packet)` or add it to
`view::ScenePacketBatch` and call `submitPackets`. The renderer's
existing `quadXZ` implementation owns the tessellation and GPU path;
View contains no road, RTS, terrain or gameplay rule. Invalid width
or nonfinite width/height values skip submission.

The legacy RTS road-link overlay now submits the same endpoints, width,
colour and lift through View, with the same immediate overlay layer and
draw order. This is an incremental extraction, **not** retirement of
`game::RtsView`. The mixed packet regression test checks the new
variant and parameter ordering but has **NOT RUN** as of this stage.

## Universal View owns the synchronous layer submission barrier (Stage 82)

The canonical `aiverse::View` now exposes two static submission APIs:

```cpp
View::submitLayers(layerQueue);
View::submitLayers(hostProviders, {seconds, deltaTime, frameRevision});
```

Both consume their work synchronously and preserve the original callback
insertion order, disabled-layer policy, exception semantics and borrowed
captures. A provider-based call collects into a local queue and flushes
before returning. These functions don't schedule work on animation threads
and don't move any gameplay authority into the renderer. Host-specific
terrain, roads, visibility decisions and effects remain owned by their
adapters.

The legacy RTS host delegates both normal and planet-scene layer
submission to `aiverse::View`. The old `RtsView` still constructs
some passes and handles gameplay-derived authoring; it cannot yet be
removed. A deferred `tests/view/view.cpp` regression covers disabled
layers, ordering, single submission and provider collection.

**NOT RUN**: CMake/CTest, native compilation and GPU parity checks
have not been performed on this stage.

## Decoupling editor selection from legacy RtsView (Stage 83)

The native/editor RTS host now uses
`modules/rts/view/RtsSelectionPresentation.h` for visual picking
radii. This is **RTS-specific authoring**, not a generic engine
algorithm: `view_adapter::selectionRadius(const UnitType&)` takes
authored footprint/radius and implements the existing building
footprint and unit radius policy; the overload taking
`const Rules&` validates the type index.

`apps/editor/Editor.cpp::pickEntity` no longer needs a mutable
`RtsView` to calculate picking bounds (four former
`const_cast<RtsView&>` calls removed). The existing
`RtsView::selectionRadius` remains as a compatibility forwarder,
not an independent source of logic. This contributes to retirement
of the legacy renderer without leaking `Rules` or `UnitType`
into `engine/view`.

The larger remaining coupling is explicit:
`apps/client/GameClient.h` still owns a `game::RtsView`;
`apps/client/GameClient.cpp` still calls `draw`, `drawGhost`,
`drawOverlays` and planetary conversion helpers;
`apps/editor/Editor.h` still owns and exposes `RtsView`;
`apps/editor/Editor.cpp` still calls drawing/overlay/preview APIs
and `heightAt`; third-party/specialized game adapters may also use
the legacy view. Full class deletion requires an independent host
scene/presentation adapter plus render, asset and terrain providers,
not simply renaming the old class.

A dedicated `view_rts_selection_presentation` fixture checks unit
and building radii, both Rules lookup overloads and invalid indices.
**NOT RUN** — compiler/CTest and native RTS/editor parity remain
to be executed.

## Standalone RTS ghost and model preview authoring (Stage 84)

`modules/rts/view/RtsPreviewPresentation.h` owns pure host-side
presentation descriptions, independent of the `game::RtsView` class:

- `makeGhostPresentation(type,position,allowed,time)` chooses placement
  position, owner, valid/invalid tint and the existing animated alpha pulse.
- `makeModelPreviewPresentation(type,owner,yaw)` chooses the preview
  identity key and initial facing.
- `previewFrameDt(now,previous)` clamps animation time to the
  historical 0..0.1s window, with a zero-delta first frame.

The legacy `RtsView::drawGhost` and `drawPreview` now consume these
descriptions. Complex model choice, placement-on-planets, LODs, material
overrides and GPU submissions remain in the specialized host renderer,
which already forwards reusable draw commands to `aiverse::View`.
The former `static EntityAnim ghost` has been changed to the
instance-owned `ghostAnim_`, preventing ghost runtime state from
crossing independent editor/client `RtsView` instances.

This is a **bounded architecture migration** and not full removal
of the legacy class. To retire it, extract host-owned model/terrain
resource providers and wire the editor/client to those providers
and the canonical View. The dedicated `view_rts_preview_presentation`
test covers adapter data without constructing RtsView. Native
compile/CTest/visual comparison are **NOT RUN** at this stage.

## RTS preview rendering provider boundary (Stage 85)

The new `modules/rts/view/RtsPreviewRenderProvider.h` is a standalone,
renderer-agnostic host adapter. `RtsPreviewRenderProvider::submitGhost`
requests resource availability, constructs the already-defined
`GhostPresentation` and submits it via the host-supplied drawing callback.
`submitModelPreview` similarly resolves the per-owner preview description,
queries the host-owned animation clock once, clamps the delta and calls the
host's renderer callback. If a type is unavailable, no render or clock
callbacks execute. The provider knows nothing about `RtsView`,
`MapGrid`, `Rules`, ECS or GPU resources.

`game::RtsView::drawGhost` and `drawPreview` now delegate their
orchestration to this provider, supplying their existing model loader,
planet/terrain placement, instance-owned ghost state, animation and
`drawObject` implementation as callbacks. This is the **first renderer
provider seam**, not a completed migration of model rendering: the
specialized legacy renderer still chooses materials, geometry, LODs,
skin poses, attachments and actual GPU submission. The direct editor,
client and portrait call sites still invoke the compatibility methods.

The existing `view_rts_preview_presentation` CTest source now tests
provider callback order, unavailable type short-circuiting and preview
clock forwarding, with no `RtsView` linkage. **NOT RUN**: source and
CMake setup have not been compiled or executed on this stage.

## Static previews without legacy animation ownership (Stage 86)

A new standalone header `modules/rts/view/RtsStaticPreviewModel.h`
accepts a `view_adapter::StaticPreviewModel` description of an
already-resolved model, geometry/material overrides, type presentation
style and model-space correction. Its
`buildStaticPreviewPacket(spec,yaw,packet)` emits a genre-free
`view::Object` with a complete borrowed `render::ModelDraw`.
`submitStaticPreview(renderer,packet)` delegates to
`aiverse::View::submitModel`. It does **not** construct an ECS
animation player, simulate gameplay or depend on `RtsView`.

`RtsView::drawPreview` now uses this independent renderer path
for models satisfying **all** of these requirements: one resolved
model variant; no build/damage stages or distance LOD; no imported
clips, AnimationGraph, bake, pose or procedural bones; no attachments,
weapon poses, additive overlays, FX or leg IK. The host still loads
the model and resolves the authored look. The resulting ModelDraw
preserves the resolved materials, node material map, geometry policy,
KHR variant, model-space correction, world transform, team colour,
tint and static/transparent flags.

Models that need animation, dynamic stages, custom procedural effects,
contact solving or LOD keep the legacy preview path unchanged.
Static-preview submission uses the existing View renderer directly,
so no second MotionWeave World can evaluate the same entity.
The renderer/resource owner must keep borrowed material and model
pointers alive through synchronous submission. This is not complete
`RtsView` retirement; host/editor/client/portrait entrypoints and
resource/terrain providers still need migration.

Per the user's implementation-first instruction, **NO TESTS RUN**
on this stage. Compile, native visual parity and platform validation
remain pending.

## Flat-ground ghost ModelDraw migration (Stage 87)

`view_adapter::buildStaticGhostPacket(spec, position, ground, tint, packet)`
extends the static preview packet path to placement ghosts. It reuses
the borrowed model/material/geometry and transform description, translates
the preview to the sampled flat-ground height, applies the valid/invalid
pulsating ghost tint, disables shadow casting and does not classify ghost
models as immutable/light-baked static scenery.

`RtsView::drawGhost` now submits eligible flat-ground rigid objects
directly via `aiverse::View::submitModel`. Its eligibility deliberately
requires one model choice without stage/LOD overrides, imported clips,
AnimationGraph, pose/baked skinning, procedural animation/IK,
attachments, weapon poses, additive overlays or effects. Planetary
placement, complex rendering and noneligible models still use the
existing compatibility draw path, which continues to own their model
lifecycle and special model placement.

This is not the deletion of legacy `RtsView`; its API and host
callers remain intact. **NOT RUN:** tests, compilation and visual
parity checks were intentionally deferred by the user.

## Shared RTS preview resource resolution (Stage 88)

`modules/rts/view/RtsPreviewResourceProvider.h` supplies
`view_adapter::resolveStaticPreviewModel(look,type,playerColor,out)`.
The host passes already-loaded appearance and type descriptions; the
provider applies the **same** eligibility test for ghost and portrait
previews, then builds one `StaticPreviewModel` with borrowed model,
materials, node-material bindings, geometry policy, variant, correction,
type transform settings, tint, and squared linear team color.

Both `RtsView::drawGhost` and `drawPreview` now call this single
resolver before their respective `buildStaticGhostPacket` and
`buildStaticPreviewPacket` submission. Ineligible models continue
unchanged through `RtsPreviewRenderProvider` and the legacy
animation/material/planet-specific draw path. No new model copies or
animation players are introduced in the direct path.

Resource residency/loading and lifetime are **still host-owned**;
the resolver is a renderer-independent borrowed-data adapter, not a
new asset cache. It has no dependency on `RtsView` or engine-side
genre enums and cannot mutate gameplay ECS state. The old class
and client/editor/portrait call sites are not yet retired.

**NOT RUN**: native compiler, CMake/CTest, CI, graphics parity and
sanitizer checks remain deferred.

## External preview model-resource ownership (Stage 89)

`modules/rts/view/RtsModelResourceProvider.h` introduces a host-facing
`view_adapter::ModelResourceProvider` contract and the
`submitResolvedPreview(resources,renderer,type,owner,yaw)` helper.
The provider resolves model residency and appearance into the existing
borrowed `StaticPreviewModel`; the helper produces one generic
`view::Object` and submits it through `aiverse::View` without an
animation update. It returns false for unsupported assets, allowing
the consumer to invoke its existing animated/complex fallback.

The legacy `RtsView` implements this resource contract temporarily.
It still owns models, GPU resources and per-type appearance data, but
`Portrait::render` now consumes the contract directly and submits
eligible static models using its own View-backed renderer instead of
calling `RtsView::drawPreview`. Animated portraits, LOD, contact IK,
attachments and specialized effects still call the old method.
`RtsView::drawPreview` uses the same resolver to avoid duplication.

This is **resource access separation**, not independent upload/cache
ownership and not the deletion of `RtsView`. Model and material
pointers are borrowed only until synchronous submission ends.
Any future independent editor or project model provider can implement
this interface without inheriting from `RtsView`.

**NOT RUN**: user deferred native compilation, CTest, rendering
parity and CI until feature work is complete.

## Per-host RTS model/texture ownership (Stage 90)

`modules/rts/view/RtsModelResourceCache.h` is the new per-host
`view_adapter::RtsModelResourceCache`. It owns the canonical
`std::map<std::string,std::unique_ptr<render::Model>>` model assets
(including procedural `shape:` models) and the shared
`render::ModelTextureCache`. Its surface includes
`models()`, `textures()`, `stats()`, `modelFiles()`,
`trimUnused(device,used,onRelease)` and
`shutdown(device)`. GPU destruction/texture trimming happen on the
render thread after outstanding asynchronous preparation has joined.
An optional host callback invalidates graph/animation entries before
a model is destroyed.

`RtsLook.cpp` model loading, asynchronous prepared-result insertion,
residency trimming, and model listing now access this one cache.
`RtsShapes.cpp` inserts procedural fallback shapes into the same
owner. `RtsView.cpp` delegates complete model/texture shutdown
instead of independently iterating raw model pointers. Existing
`RtsView::textureCache()`, `assetResidencyStats()` and
`modelFiles()` remain source compatible and forward to the owner.
A read-only `RtsView::modelResources()` accessor exposes this
per-host resource owner to native presentation adapters.

The resource cache itself does **not** depend on RTS Rules, View
simulation, FPS, AnimationGraph scheduling, or genre-specific game
commands; it stores `render::Model` and texture assets only.
The legacy `RtsView` still *contains* its resource-owner instance
and still selects/loading model variants. This stage is ownership
extraction, not fully independent lifecycle or complete elimination
of RtsView.

**NOT RUN:** per user instruction, no CMake/configure, native compile,
CTest, sanitizers, Metal graphics, CI or visual comparison was executed.
Before shipping, verify resource release ordering, shared-texture
hot reload, streaming cancellation, and asset residency under editor
scene switching.

## Independent model preparation lifecycle (Stage 91)

`modules/rts/view/RtsModelPreparationService.h` provides a
per-resource-owner asynchronous CPU preparation queue. It owns the
current source type/variant request, ordered source/key list, cursor,
displayed in-progress resource name, and at most one `std::future`
for a CPU-prepared `render::Model` with warnings. The service
exposes `begin`, `appendPath`, `launch`, `ready`, `take`,
`advance`, `reset`, `progress` and resource state queries.
`begin`/reset join any active task before discarding its state.
The caller still supplies immutable model/reader/texture-cache
inputs to the CPU task and publishes GPU uploads on the render thread.

The per-host `RtsModelResourceCache` **owns** this scheduler on
non-Emscripten builds and resets it before freeing models and textures.
`RtsView` no longer carries independent worker futures, type/cursor
or path state. `RtsLook.cpp` sends requests and polls completion
through `resources_.preparation()`; it still determines RTS model
choices, skinning policy, variant order, warning destinations and
render-thread uploads. Clearing loaded types or replacing hot-reloaded
models continues to be blocked when a preparation request is active.

This separates resource scheduling and lifetime from legacy rendering,
but it is **not yet a fully independent RTS asset-loading service**:
the host still owns source discovery and the model-file loading policy,
and it still stores rules-driven appearance data. New native hosts can
own `RtsModelResourceCache` independently and supply equivalent CPU
work/publish callbacks without instantiating `RtsView`.

The browser/Emscripten path retains its original synchronous/no-worker
behaviour. **NOT RUN:** no CMake, compiler, CTest, sanitizer, Metal
visuals or CI on this feature stage, by user request.

## Reusable model streaming operations (Stage 92)

`modules/rts/view/RtsModelStreaming.h` provides two host-agnostic
model-loading building blocks: `discoverModelPaths(authored,geometry,
alreadyCached,append)` traverses an authored JSON subtree in existing
order (including model variants, LOD and attachments), and
`prepareModelCpu(path,reader,textures,{geometry,animationBake})`
performs model decoding, optional cluster preparation, optional
animation-bake preparation and CPU upload staging. The latter returns
a `RtsModelPreparationService::PreparedModel` with warnings; it does
**not** issue GPU commands.

`RtsModelResourceCache::publishPrepared(device,key,prepared)` runs
synchronously on the render thread, skips replacing an asset already
published by a synchronous loader, and uploads/inserts a prepared model
(including the existing null-model failure cache semantics). The cache
still owns the model/texture lifecycle, while
`RtsModelPreparationService` owns the future and ordered queue.

`RtsLook.cpp::prepareType` now composes these three operations.
The RTS host retains policy decisions: which authored variants to
request, geometry/skinning flags, warning routing, and when to poll
or publish a result. Native hosts may reuse these functions and own
a `RtsModelResourceCache` without constructing `RtsView`.
The service remains serial (one in-flight CPU job per owner);
browser/no-worker behaviour is unchanged.

This moves the streaming **operations** out of the legacy class, but
does not yet replace RtsView's rules-driven type choice and residency
coordination. Material variant configuration and model hot-reload
invalidations still require the owning host. No animation ECS,
networking, save data, or renderer packet contract changed.

**NOT RUN**: native build, CTest, sanitizer, GPU visual parity, CI,
and cross-platform verification remain deferred per user instruction.

## Native editor model demand scheduler (Stage 93)

`modules/rts/view/RtsModelDemandScheduler.h` adds a small host-owned
policy service for model demand. It tracks per-type last-use times,
batches and de-duplicates demand requests, upgrades each request to
the highest priority (`Background`, `Visible`, `Interactive`),
drains in descending priority and FIFO within each priority,
and exposes periodic reclamation and recent-use decisions.
`resize()` resets demand on rules/hot-reload changes and advances
an independent revision, invalidating queued requests.

`apps/editor/Editor` now owns this scheduler instead of a separate
last-use vector and trim timestamp. It registers interactive requests,
drains them immediately so Inspector and material previews remain
responsive, and uses the scheduler's retained-time policy for its
existing 5-second sweep and 15-second grace. The original map,
selection, placement and overlay-preview keep rules still apply.
Both lazy and eager modes consume the queue; eager mode does not
reload already-resident models. Rules reload resets scheduler state.

`RtsModelResourceCache::generation()` additionally exposes a
monotonic counter updated on `publishPrepared`, `trimUnused`
and `shutdown`. **Important limitation:** the existing legacy
`models()` mutable map access is still supported and does not
increment this counter; callers must not interpret it as a complete
hot-reload epoch until all insertion paths are routed through the
owner API.

This feature establishes an externally owned scheduling/policy
component and integrates it with an actual editor host. It does not
yet move all rules-driven asset selection/hot reload from `RtsView`
or change MotionWeave. **NOT RUN**: CMake/native compiler, CTest,
Metal screenshots, sanitizers and CI remain deferred per user request.

## Tracked model cache writes and budgeted editor prefetch (Stage 94)

`RtsModelResourceCache` now exposes read-only `models()`, plus
`contains`, `find`, `insertIfAbsent`, and `replace(device,key,
replacement,onRelease)`. All known RTS model-map writes have moved
through this resource-owner API: synchronous imports preserve null
failure-cache entries, worker results use `publishPrepared`, and
generated shapes use tracked replacement which releases prior GPU
buffers and invalidates their host animation-cache entries.
`generation()` now advances on those writes, unused-model eviction,
and shutdown. This covers every currently identified mutation in the
RTS loaders, but callers must still rebuild/invalidate borrowed raw
`Model*` pointers after replacement, trim, or full reload.

The editor's `RtsModelDemandScheduler` adds `drainBudget(budgetMs,
maxCount,load)`, prioritizing interactive/visible requests and
preserving FIFO within equal priority. The editor continues to service
interactive selection/Inspector loads synchronously. Its residency
sweep queues missing *retained* map/type models for background
prefetch, and each non-playing non-FPS editor frame consumes at most
two requests with a nominal 1 ms wall-clock budget. Because
`loadType` can be synchronous, one load may exceed this soft budget.
Rules reload resets demand and the existing RtsView shutdown releases
all cache resources, advancing their generation; targeted per-file
hot reload still needs a safe external invalidation API.

Genre-specific keep rules (map references, current selection,
placement/overlay preview, and recent use) remain in the editor.
The generic engine View and MotionWeave are unchanged. RtsView is
still a compatibility host and owns the cache instance.

**NOT RUN**: native compilation, CMake, CTest, sanitizers, Metal
screenshots, editor/RTS/FPS smoke runs and CI were deferred at the
user's explicit request.

## Editor presentation-only asset refresh (Stage 95)

The model watcher in `apps/editor/windows/ModelWindow.cpp` now batches changed
**resident model files** into an editor-local pending set. It retains changes while
either in-editor game play or client play is active and flushes them once both
modes have ended. It resets timestamps and queued paths on a project switch,
avoiding reload requests leaking between projects.

`Editor::reloadVisualAssets()` re-creates the compatibility view on the editor
render thread, which joins outstanding CPU model-preparation workers before
destroying GPU model/texture resources. It invalidates queued model demands,
preserves last-used timestamps, requeues retained authored/selected types and
increments preview revision so borrowed scene data is rebuilt. Unlike
`reloadRules()`, this path does **not** reparse project rules, clear the
simulation, reset the editor's document or restart `reloadFx()`. Existing
synchronous interactive loads and lower-priority opportunistic prefetch remain.

This is a **whole-visual-view refresh**, not selective reload of changed models
or animation graphs. The watcher currently sees only model paths reported by
`RtsView::modelFiles()` (resident assets), not every uninstantiated type,
texture dependency or external graph JSON. The model cache's tracked
`generation()` is not yet a per-source dependency revision. A later stage
must index source paths to cache keys, detach all referencing TypeLooks and
presentation/animation graph instances, and retire changed GPU resources only
after that barrier. FPS visual resource reload and native project host adapters
remain separate work. Native build, CTest, sanitizers and Metal/editor visual
verification are **NOT RUN**, per request to implement features first.

## Selective model-source and animation presentation reload (Stage 96)

Model/animation hot reload no longer requires a full `RtsView` restart for a
changed **resident model source**. The editor watcher invokes
`Editor::reloadVisualModelSources(paths)`, which invokes
`RtsView::reloadModelSources(paths)` on the presentation/render thread
outside play mode. `Editor::reloadVisualAssets()` remains available as the
legacy full-view fallback for non-model asset categories.

The independently owned `view_adapter::RtsModelResourceCache` now supports:
- `sourceForKey(key)`: maps both `model.glb` and `model.glb#geometry`
  cache entries to the original source path.
- `affectedBySource(path)`: enumerates affected keys and borrowed model
  pointers; importantly, failed-load/null entries are also represented.
- `invalidateSources(device,changed,onDetach)`: joins any pending native
  CPU preparation, invokes the host detachment callback **before** any
  cached GPU model destruction, evicts only affected keys, advances cache
  generation and runs texture trimming. The callback must clear all
  external borrowed model/animation references and is invoked on the render
  thread between submissions; a throwing callback aborts before eviction.
- `modelFiles()`: reports deduplicated normalized source paths even for
  geometry-policy variants and failed loads, so a fixed missing model can
  be reloaded without reopening the editor.

The RTS host's detachment phase identifies loaded appearances through
current `ModelChoice` pointers (including attachments, stage choices and
LOD choices) and through recursive authored model references (covering
failed loads, late variants and nested objects). Streaming
`coarseModel/detailModel` and requested runtime models are checked too.
Affected `TypeLook` and `EntityAnim` records and per-model compiled graph
caches are invalidated, then only previously loaded affected types are
reconstructed. `MotionWeaveSourceBridge::forget` and
`aiverse::view::View::forgetSources` remove only affected presentation ECS
entities; unrelated animation clocks are not deliberately restarted.
The ephemeral ghost animator is reset conservatively. There is no second
animation tick or owner.

**Scope and constraints:** the editor watcher still enumerates only
resident cache sources, not all unloaded authored assets. File changes to
external graph definitions, materials, stand-alone textures and independent
FPS host caches are not routed into this targeted model path. They require
explicit dependency indexing and safe texture/graph invalidation next.
GPU submission completion, shared texture retention, active portrait
render targets and exact visual parity require native/graphics validation
before this is production-safe. `RtsView` remains a compatibility adapter.

**NOT RUN** by request: no local native compiler, CMake/CTest, Metal/RTS/FPS/
editor runtime, ASan/TSan, benchmarks or CI. The code was committed through
the GitHub API; source review is not a substitute for compilation.

## Source dependency index and revisioned external visuals (Stage 97)

`game::view_adapter::ModelSourceDependencyIndex` (new
`modules/rts/view/RtsModelSourceDependencies.h`) provides a reusable,
host-owned index of source path -> dependency categories and type IDs.
`rebuild(types,materialLibrary)` uses authored view descriptions, not
loaded models: nested variants, LOD, stages, attachments, streaming
coarse/detail model paths, referenced library materials, direct texture
overrides and optional external `animationGraph` paths are included
even for missing and not-yet-streamed sources. A host can add
`addResidentModel` and `addResidentTexture` entries from already
loaded render Models to catch imported glTF/etc external texture files.
`resolve(changedFiles)` returns impacted types, model sources and
category flags; paths are lexical-normalized. No workers, GPU, or game
simulation state are owned by the index.

`ModelWindow` rebuilds this index during its existing once-per-second
asset scan, safely excludes absolute/parent-traversal watch paths,
remembers both present and absent file timestamps and notices
delete/recreate transitions. Watch state is reset when changing projects.
Requests remain queued while in-game or client play is active.
Unchanged imported-model conversion behaviour is retained.

For **model changes**, Stage 96's selective model-source transaction
is used, with all authored model variants of affected types rebuilt.
For a **texture change**, the editor first increments that texture
source's revision through `render::ModelTextureCache::invalidateSource`,
then reloads the affected model sources. The decoded CPU entry is
discarded; the shared GPU cache uses a revision-suffixed key for new
uploads, leaving old texture leases with existing borrowed models.
When model records release, the normal cache trim collects old GPU
entries whose leases are no longer retained. No borrowed GPU texture
is forcibly destroyed by invalidation. If a texture affects an object
without any resolvable model source (e.g., a built-in shape), the
editor chooses the full-view refresh fallback.

`view.animationGraph` now also accepts a JSON asset **path string**
in addition to the existing inline graph object; the renderer-host
file reader loads and validates the external definition. The editor
watches external graph paths and falls back to the existing full
presentation refresh to avoid crossing model/graph lifetimes.
`materials.json` edits also use the whole-view fallback until
library material revision and ownership are independently managed.
`AnimationHotReloadRegistry::apply` now requests player restart and
clears stale montage/notify state on a changed graph, while
`forgetModel(model)` removes last-good compiled graphs before a host
destroys their `Model`. This generic registry API is **not** yet used
by the transitional RTS host, which has its own graph cache.

No JSON schema change is required for existing inline graphs, models or
type references. This stage adds optional file-backed graph syntax,
new C++ helpers, and an internal GPU texture-cache revision suffix.
There are no authoritative ECS, network/save/checksum or gameplay
changes and no duplicate animation evaluation.

**NOT RUN:** native build, CMake, CTest, Metal/editor/FPS/RTS screenshots,
sanitizers, CI, device/GPU fence checks and performance benchmarks.
Test sources `tests/animation/model_dependencies.cpp` and additions
to `tests/animation/hot_reload.cpp` were authored but not executed.

**Important limits:** the source index does not dynamically parse
nested external graph includes, imported model-specific sidecars that
are not exposed as models/images, runtime-generated external texture
paths that never enter the cache, or arbitrary dependencies produced by
custom loaders. The watcher currently lives in ModelWindow and is
editor-only. Targeted reload still depends on RtsView for appearance
rebuild and must run between GPU submissions. Model-derived external
materials/texture lease lifetimes and render synchronization require
real platform verification before production deployment.

## Window-independent visual reload and staged appearance transactions (Stage 98)

The editor now owns `VisualAssetDependencyService`
(`apps/editor/core/VisualAssetDependencyService.h`) directly as a member of
`Editor`. `Editor::pollVisualAssets(dt)` runs once during the normal
`Editor::frame` lifecycle after document changes and **before** any viewport
render submission. It is not contingent on `ModelWindow::open` or on any
individual window callback. `ModelWindow` is now strictly a UI and
`model_info` presenter; its former watcher state and process launcher have
been removed.

The service keeps the active project, 1-second scan clock, optional file
timestamps (including missing sources), per-file source conversion attempts,
pending changed paths and a `ModelSourceDependencyIndex`. It rebuilds its
dependency snapshot using authored rules/material library and the resident
model cache at each scan. It never mutates renderer resources. Deferred
changes survive editor or client play until an editor-frame safe boundary;
`reset` runs on rules/project changes and removes old timestamps/requests.
The old `modelconv` invocation now belongs to the Editor host as
`Editor::convertVisualModelSource`, with source modification-based deduping.
External graph file paths nested in authored model choices are now watched
as well as top-level graph references.

For graph-only or `materials.json`-only modifications,
`RtsView::reloadAppearanceTypes(types,reloadMaterialLibrary,&count)`
implements a staged appearance update. The host joins any pending CPU
model-preparation task, parses the new material library transactionally if
requested, builds candidate `TypeLook` resources for all affected **loaded**
types while retaining old looks, and rejects the batch if a previously
present model/graph would disappear or candidate building throws. A rejected
edit keeps existing animation playback and prior material library contents.
After successful staging, affected MotionWeave presentation source IDs are
unlinked via `View::forgetSources`, only their `EntityAnim` and streaming
choice state is cleared, and only affected `TypeLook` records are swapped.
Unrelated models and animation players keep their old cache residency,
player entities and clocks. Previously unloaded types naturally use the
latest file/library when loaded later.

Mixed source changes involving a graph/material together with model/texture,
or texture changes with no resolvable model source, retain the earlier
whole-view compatibility fallback. Model-only and recognized texture-only
changes still use Stage 96/97 selective model cache invalidation and
revisioned texture leases. This is deliberately not an attempt to mix GPU
model destruction with appearance graph swapping in one transaction.

**Public surfaces:** `VisualAssetDependencyService::due/scan/impact/
changedSources/acknowledge/reset` are editor-only; the generic
`ModelSourceDependencyIndex` and MotionWeave source bridge retain the same
engine boundaries. `RtsView::reloadAppearanceTypes` is an additive
compatibility-adapter API. No simulation ECS, save/checksum, networking,
render packet or authored JSON format changes. The `RtsView` compatibility
owner is NOT removed.

**GPU lifetime caveat:** this stage does NOT introduce a GPU completion
fence. It provides the CPU/presentation-thread staging boundary, but the
backend's asynchronous use of GPU buffer and texture allocations still
requires platform-specific lifetime validation, and shared material images
may remain in the resident model until that model is evicted. Stage 98's
selective appearance updates avoid destroying resident GPU model buffers,
but full-view/model-source fallback paths retain their previous
destruction policy.

**NOT RUN**, per user instruction: no local clone/native CMake compiler,
CTest, sanitizers, Metal/editor/FPS/RTS screenshots or GPU resource fence
verification. Deferred test source
`tests/animation/editor_visual_asset_service.cpp` exercises service
ownership independent of a window, initial timestamp seeding,
external graph detection, deletion/recovery, pending retention and project
switch reset. `CMakeLists.txt` registers
`animation_editor_visual_asset_watcher` for future native execution.

## Stage 99 — portable GPU resource retirement and universal model owner

**Renderer API:** `gfx::Device::waitForResourceIdle()` is a new
synchronous, explicit owner-thread barrier. It returns `true` only
when all **previously submitted** GPU work has completed; default
implementations return `false` rather than guessing that a given
`frameSerial()` represents completion. Call outside render/compute
encoders, **before** issuing GPU work this frame that could reference
retired resources. A queue-ordered Metal command buffer is committed
and waited for; Vulkan uses `vkDeviceWaitIdle` followed by completed
garbage collection; OpenGL/GLES uses `glFinish` with error inspection.
These are intentionally blocking correctness barriers, not a low-latency
async fence/timeline implementation.

**New genre-independent ownership classes:**

- `<engine/render/ModelPreparationService.h>` —
  `aiverse::render::ModelPreparationService`: host-owned CPU worker,
  ordered path requests and progress with explicit render-thread GPU
  publication policy.
- `<engine/render/ModelResourceCache.h>` —
  `aiverse::render::ModelResourceCache`: per-host owning source map,
  texture cache, preparation worker, generation, model variants and
  resource invalidation. Native FPS, RTS, editor and standalone project
  code can instantiate this without importing RTS modules.
- `<engine/render/ResourceRetirementQueue.h>` —
  generic `render::ResourceRetirementQueue<Resource>` queues move-only
  resources and invokes destruction only after an explicit fence
  callback returns success.

Compatibility headers in `modules/rts/view` now alias
`RtsModelResourceCache` and `RtsModelPreparationService` to those
generic render classes, preserving old method signatures and
`view_adapter` names. No conversion of game-authoritative state
is involved. No special renderer view object or second animation
clock is created.

**Release lifecycle:** `ModelResourceCache::replace`,
`invalidateSources` and `trimUnused` first detach host
`TypeLook`/`EntityAnim`/MotionWeave and then transfer removed
`unique_ptr<Model>` instances to `retired_`, retaining their GPU
buffers and shared texture leases. `pendingRetirements()` reports
the deferred queue. `collectRetired(device)` calls the backend
completion barrier once for a nonempty queue, releases old models
only after confirmation, then trims unleased texture-cache entries.
If a backend cannot prove completion, the full queue remains intact.
`shutdown` joins pending CPU jobs, waits for previously submitted GPU
work, then releases resident and retired models and finally textures.
It **fails closed with an exception** if the GPU barrier fails,
rather than destroying potentially active resources. It remains
the host's responsibility to call shutdown at a safe frame boundary
and not while a current frame has unsubmitted references.

**Real owner integrations:**

- `Editor::frame` collects completed retirements before viewport
  submissions; live `GameClient::frame` uses the same barrier before
  rendering RTS scene sources. There is no per-object wait or per-draw
  GPU stall.
- `game::Portrait` uses its *own*
  `render::ModelResourceCache previewModels_` to own the generated
  sphere and box of material previews, including their GPU lifetime,
  instead of raw `unique_ptr<Model> shapes_[2]`. This is a concrete
  independent editor/portrait resource owner. Its offscreen render
  target, material authoring and complex/animated preview fallback
  remain as before.
- Resource cache API is usable from native project C++ by including
  `engine/render/ModelResourceCache.h` and linking the existing
  `aiverse_models`/engine rendering target.

**Deferred tests:** source fixtures
`tests/render/resource_retirement.cpp` and
`tests/render/model_resource_provider_contract.cpp` are registered
as `resource_retirement_queue` and
`model_resource_owner_contract` CTest entries. The former simulates
a denied then granted completion barrier and verifies FIFO cleanup;
the latter verifies the generic/compatibility aliases and source
cache APIs without uploading GPU resources. **NOT RUN:** no local
compiler, CMake, CTest, screenshots, sanitizer, CI, backend GPU
execution or memory-life profiling.

**Limits and risks:** the barriers are conservative *blocking* waits
and can pause a frame when resource retirement is requested. An
asynchronous completion-token/timeline queue has not been implemented.
`waitForResourceIdle` does not flush or synchronize unsubmitted
GPU commands in the **current** frame, so hosts MUST only call it
before those commands can reference any retired object. Other older
engine renderers, FX targets and raw device clients still destroy
GPU handles directly and have not been converted to the universal
owner. Full legacy `game::RtsView` removal remains incomplete.
Native compilation and Metal/Vulkan/GL correctness are UNVERIFIED;
do not merge or claim runtime parity before the deferred checks.

## Stage 100 — nonblocking GPU retirement timeline and native model provider

`gfx::Device` now offers a token-based asynchronous GPU completion API:

- `beginResourceRetirement()`: return a nonzero ordered token after all
  **previously submitted** GPU work on the owning device/queue.
- `resourceRetirementComplete(token)`: nonblocking owner-thread polling;
  only `true` authorizes model/texture destruction.
- `endResourceRetirement(token)`: release a completed token's backend
  fence/callback state, never a still-running marker.
- `waitForResourceIdle()`: existing blocking fallback and final shutdown
  barrier; still fail closed if the device cannot guarantee completion.

Metal commits an empty queue-ordered command buffer and uses its completion
handler's atomic status; Vulkan queues a zero-command-buffer fence submission
and polls `vkGetFenceStatus`; GL/GLES/WebGL2 enqueues `glFenceSync`, flushes
the context and polls `glClientWaitSync(..., 0, 0)`. Windows GL's loader
now resolves the sync API. The implementation is owner-thread-only except
for the Metal completion handler. Frame sequence numbers and fixed delays
are NOT treated as GPU completion.

`render::AsyncResourceRetirementQueue<Resource>` keeps independent batches
for each nonblocking token. New model evictions after a token was issued
are never freed on that older token. At the next frame boundary
`ModelResourceCache::collectRetired(device)` releases any completed batches
and schedules newly queued models without stalling the GPU; it only uses
the blocking `waitForResourceIdle` fallback when token creation fails or
is unsupported. Shutdown still requires an explicit successful full-device
barrier and drains all remaining batches before destroying the texture
cache. Existing `ResourceRetirementQueue` remains for synchronous users
and its original tests.

### Reusable native model provider

New `engine/render/ModelAssetProvider.h` implements a per-native-host
source loader, uploading and caching ordinary authored models through
`render::ModelResourceCache`. It accepts a project-supplied
`render::FileReader`, returns a borrowed `Model*`, exposes failed-load
warnings, selective source invalidation, async retirement collection and
explicit shutdown. It knows no RTS, FPS, editor or simulation entities.
Native projects may pass its borrowed `Model*` to the existing generic
`aiverse::View::submitModel` or construct their own `view::Object`.

`game::Portrait::renderStaticModel(ModelAssetProvider&,source,yaw)` is a
real static model consumer: its renderer and canonical View submit the
provider's model without constructing `game::RtsView`, creating another
MotionWeave graph evaluator or assuming the resource provider owns game
rules. Complex/animated RTS portraits keep the existing compatibility
fallback until further migration.

### Verification / limitations

Source regression `tests/render/async_resource_retirement.cpp` checks
multiple retirement batches, out-of-order requests, missing token fallback,
failure to prove GPU idle, shutdown drain and exact destruction order.
`tests/render/model_asset_provider_api.cpp` is a native C++ consumer API
fixture without a real GPU. Both are registered in CTest (names
`async_resource_retirement_queue` and `model_asset_provider_api`).
**NOT RUN:** compilation, CTest, Metal/Vulkan/GL/WebGL2 native GPU runs,
sanitizers, graphics screenshots, frame-time profiling, CI and memory
retention checks remain explicitly deferred.

Token creation is legal only between frames or before current-frame GPU
commands may borrow retired resources. A token does not account for
**unsubmitted** commands recorded in the current frame. The fallback
`waitForResourceIdle` may still stall when a backend cannot create a
token. Fences with device loss can remain pending; the host must handle
GPU loss safely rather than assume a timeout proves completion. The
backend-specific implementations and lifetime ordering are not yet
validated on real hardware. Scene-level (RTS View) legacy retirement
and custom FX/terrain caches still need their own migration; don't merge
or claim parity before native verification.

## Stage 101 — bounded retirement pressure, async CPU imports and editor raw-model preview

### Resource pressure / diagnostics

`render::AsyncResourceRetirementQueue<Resource>` now exposes
`stats()` and `saturated(limit)`. Statistics include queued resources
(in submitted and unsubmitted batches), active fence batches, unsubmitted
resources, high-watermark, number of successfully released resources and
batches, repeated pending polls, async-token creation fallbacks and failed
synchronous-idle attempts. A **pending** GPU token is normal; it must not
be reported as a device error. A zero token means unsupported *or*
failed asynchronous marker issuance, not proof of GPU loss.

`render::ModelResourceCache` offers
`retirementStats()`, `setRetirementSoftLimit(count)`,
`retirementSoftLimit()` and `publicationBackpressured()`.
The default soft limit is 128 **retired model objects**; zero disables
the limit. This is an advisory allocation throttle for new generic
source-provider loads, NOT a hard cap on in-flight allocations and not a
VRAM byte quota. Existing resident models keep working; worker results
already prepared by the legacy RTS service are not discarded simply
because the queue reaches its limit. No timeout or backpressure signal
ever allows a model to be destroyed before its GPU fence completes.

`RtsView::assetResidencyStats` forwards these counters and the Stats
window displays waiting models, fence batches, queue peak, throttle
state and fallbacks. The actual GPU texture bytes, shared geometry-pool
allocations and other render targets remain outside this statistic.

### Generic asynchronous native-source loader

New `engine/render/AsyncModelCpuQueue.h` contains a reusable,
single-worker native CPU import queue. `request(path,reader,textures)`
deduplicates active/queued paths, limits the waiting queue (default 16),
and executes `loadModel` plus `prepareModelUpload` on a background
worker. No GPU Device/Renderer/ModelDraw functions are invoked there.
`pollReady(Prepared&)` is nonblocking on the owner thread;
`invalidate(path)` advances the source revision and removes queued
requests, so an already running stale CPU result is rejected instead
of being published. `shutdown()` joins the worker before the project
reader and texture-cache lifetime ends. The application-supplied
`FileReader` MUST be thread-safe.

`render::ModelAssetProvider` integrates that CPU worker with
`requestAsync(path)`, `cpuStats()` and
`publishReady(device,maxCount)` (native builds). A project can use
the following frame-boundary pattern:

```cpp
render::ModelAssetProvider provider(projectReader);
// On demand, without blocking the render thread for CPU import:
provider.requestAsync("models/prop.glb");
// During a later render-thread frame, before scene submission:
provider.collectRetired(device);
provider.publishReady(device, 1);
if (const auto* model = provider.find("models/prop.glb")) {
    render::ModelDraw draw;
    draw.model = model;
    aiverse::View::submitModel(renderer, draw);
}
// During project shutdown, on the device owner thread:
provider.shutdown(device);
```

The generic provider pauses admission/publication of NEW models if
retirement pressure is at its soft limit, but does not invalidate
finished CPU work prematurely. Its old synchronous `load` path is
still available. Emscripten remains on the synchronous path; no CPU
threads are required for browser projects.

### Actual editor consumer

`apps/editor/windows/ModelWindow.cpp` adds an explicit alternative
*“Исходная 3D-модель (без overrides)”* for imported static unskinned
models. Only when this inspection mode is selected, the window owns a
separate `ModelAssetProvider`, asynchronously imports the raw model,
and calls `Portrait::renderStaticModel(provider,path,...)` through the
canonical `aiverse::View`. The imported original deliberately omits
type-level colour, material overrides, graph/procedural animation,
attachments and authored transform correction. This is **not** a
replacement for the normal type-appearance preview; the latter remains
the default and retains the legacy path for fidelity. The independent
provider joins/releases its resources on project/cache revision changes
and editor window destruction.

### Validation and limits

New deferred CPU fixtures:
`tests/render/retirement_pressure.cpp` (`render_retirement_pressure`)
and `tests/render/async_model_cpu_queue.cpp`
(`render_async_model_cpu_queue`, native-only with Threads) are
registered in CMake. The first checks resource-pressure telemetry,
high-watermark, per-batch completion and fail-closed fallback; the
second checks worker request coalescing, bounded waiting queue,
invalidated-result rejection and safe shutdown. All are **NOT RUN**,
along with native builds, CTest, GPU backend tests, sanitizers,
screenshots and FPS/RTS/editor play parity, by the existing
features-first test deferral.

**Remaining issues:** the soft limit does not cap memory from all
legacy RTS model-publication paths, FX, terrain, texture bytes or shared
geometry pages; worker preparation is one-at-a-time, not a parallel
priority job system; the editor source preview is opt-in and differs
visually from a fully authored type by design. Backend device-loss
recovery and GPU-fence error states still require real hardware
validation. The generic resource owner is suitable for native projects,
but the full legacy `game::RtsView` scene adapter is not retired.

## Stage 102 — estimated GPU retirement footprint and prioritized native asset publication

### Source-derived GPU footprint (not driver VRAM)

`engine/render/ModelResidentEstimate.h` offers
`render::estimateModelResident(const render::Model&)`. It approximates
dedicated model vertex/index buffers using model or authored primitive
counts (even after CPU vertex/index arrays are freed), counts base-level
bytes of **owned, non-borrowed** model images, and estimates RGBA32F
baked-pose matrices from joint/row counts. It intentionally excludes
shared geometry-pool pages, borrowed shared textures, unknown morph
texture height, mip/driver alignment and other backend allocations.
The estimate is **not** exact VRAM, and differing loader batching or
compressed formats can make it over- or under-estimate usage.

`AsyncResourceRetirementQueue::estimatedPendingBytes(estimate)` walks
both unsubmitted resources and existing GPU-token batches; totals are
saturated rather than overflowing. The generic
`ModelResourceCache::estimatedRetiredGpuBytes()` sums the known-model
estimates and exposes
`setEstimatedRetirementByteLimit(bytes)` and
`estimatedRetirementByteLimit()`. This is an optional, default-disabled
(0) source-derived **admission** limit for NEW
`ModelAssetProvider` loads, combined with the previous retired-object
soft limit. No estimate, backlog or device-error status ever authorizes
early GPU destruction. The editor Statistics panel displays estimated
retired model MiB with an explicit shared-pool exclusion.

### Priority and render-thread upload pacing

`AsyncModelCpuQueue::request` accepts an optional
`ModelLoadPriority::Background/Visible/Interactive`; jobs already
queued can be upgraded (not duplicated), and work at equal priority
retains insertion order. One CPU worker continues to run at a time,
with its original revision rejection, bounded queue and worker-thread
reader contract. Priority applies to **pending** jobs; an already
executing CPU decode is not preempted.

`ModelAssetProvider::requestAsync(source,priority)` forwards priority.
`publishReady(device,maxCount,budgetMs)` adds an advisory time limit:
it checks elapsed wall time before starting each subsequent GPU upload
and never drops a ready model. The first upload may exceed budgetMs,
so this is not a hard GPU upload time guarantee. Count remains a hard
per-call cap. GPU uploads and publication continue to run only on the
owning render thread, never in CPU workers.

The Editor Model window's optional raw static preview now requests
`Interactive` priority and publishes at most one model with a nominal
1 ms budget per frame. Its independent provider uses an advisory
64 MiB *estimated-retirement* limit. This does not change the default
type/character portrait path, nor its authored material, graph or
placement semantics.

### Second real native View consumer

The standalone `aiverse_modelview` tool has an opt-in
`--provider --no-merge` path for ordinary raw static assets with no
requested animation clip and no baked-skinning mode. It loads those
models through its own `render::ModelAssetProvider` instead of the
legacy raw ownership map. Both the opt-in and legacy paths now submit
their existing `ModelDraw` (world transform, imported materials,
variants, team colour and precomputed animation pose) through canonical
`aiverse::View::submitModel` without a second graph tick.
The option intentionally excludes merge and animated-model paths,
which retain their previous import/motion logic. It is a comparable
static source inspection path, NOT a blanket switch of all games to a
new renderer or a claim that `RtsView` is retired.

### Deferred verification

New tests registered through ordinary CMake/CTest targets:
`tests/render/model_estimate.cpp` (`render_model_estimate`) checks
geometry/owned-image/baked-pose estimates and shared-texture exclusion;
`tests/render/model_priority.cpp` (`render_model_priority`, native-only)
checks queue ordering, promotion, bounded priority and non-preemption.
They are **NOT RUN**, along with all native C++ builds, editor/modelview
smoke runs, Metal/Vulkan/GL/WebGL2 tests, sanitizers, screen captures,
GPU heap metrics and performance profiling, per the requested
features-first workflow. No PASS or parity claim is made.

The model footprint is not a driver memory reading; estimated-byte
admission control does not bound global GPU memory. A priority queue
does not preempt the active worker, and upload time budgets remain
advisory. Standalone modelview and raw editor preview are explicitly
separate from full authored graph/appearance simulation and need
real native verification before production use.

## Stage 103 — bounded multi-worker model imports and separate GPU resource owners

### Parallel CPU model preparation

`engine/render/AsyncModelCpuQueue.h` now supports **1–8 parallel CPU
worker tasks** with `setWorkerCount(n)` and `workerCount()` (default 1
for compatibility). Its owner-thread API still accepts
`ModelLoadPriority::Background`, `Visible` and `Interactive`,
with upgrades of queued requests, FIFO for equal priority, bounded
waiting queue, duplicate suppression, source-generation invalidation
and `pollReady(Prepared&)` for render-thread publication.
`pollReady` checks every running task rather than waiting for an older
unready task to finish, so a later independent completion can be
published without blocking. The CPU import uses `std::async` per active
job, while all GPU upload, View packet construction, ECS mutation and
model-cache publication remain outside the worker threads.

After eight consecutive selections favoring later queued work, the
scheduler admits the oldest queued source to prevent starvation of
background loads. It does not preempt active decoders, guarantee job
runtime fairness or exceed the configured concurrency cap.
`AsyncModelCpuQueue::Stats` now includes active/configured worker
counts, total dispatched work and fairness activations in addition
to the existing queued/finished/stale result counters.
`ModelAssetProvider::setCpuWorkerCount`, `cpuWorkerCount` and
`setCpuMaxQueued` expose scheduling through the native project
resource owner. The Editor Model window uses two workers and an
8-request waiting queue in its opt-in raw model preview, with
`Interactive` priority, one GPU upload per invocation and the
existing nominal 1 ms upload budget. CPU worker counts and waiting
requests are shown beside the preview when active.

**Thread-safety:** the caller-provided `FileReader` must be
reentrant/thread-safe when worker count exceeds one. `ModelTextureCache`
guards decoded-image access with a mutex, while GPU methods are confined
to the render owner thread. Calls to request, invalidate, poll and
shutdown are single-owner-thread operations. An invalidated CPU task
may finish but must not be published if its revision is stale. Lowering
worker count does not interrupt work already in flight; project
shutdown joins it before reader and texture-cache destruction.

### Owner-accounted GPU estimates without shared-resource double charging

`render::ModelTextureCache::gpuOwnerEstimate()` walks its unique
GPU texture keys ONCE and reports unique texture count, compressed
texture count and RGBA8 base-mip equivalent bytes. Borrowed
`Model::images` are not billed again because shared GPU textures have
their own resource owner. This equivalent-size estimate is NOT the
actual GPU allocation size for compressed images, mipmaps or
driver-specific alignment.

`render::ModelResourceCache::gpuOwnerEstimate()` combines *separate*
categories: known dedicated allocations of its resident models,
known retired-model allocations awaiting GPU fences, unique
ModelTextureCache GPU texture equivalents, and number of models
referencing the shared geometry pool. The global geometry pool's
allocated/occupied page sizes belong to the *device-wide* owner and
must be queried separately via `geometryPoolStats(device)`.
Do **not** sum the device-global geometry pool once per model/cache,
nor add borrowed per-model textures to the owner texture count.
`RtsView::assetResidencyStats()` and the Editor Statistics window
expose these categories separately. The numbers remain diagnostic,
source-derived approximations, not vendor GPU heap measurements.

### Additional real editor render consumer

`apps/editor/SceneRenderable.h` now routes authored
`SceneRenderable::model` through `aiverse::View::submitModel`
and normal unselected meshes through
`aiverse::View::submitMesh`. Existing `ModelDraw` transform,
tint and selected/hovered model outline fields are unchanged.
Special selected/hovered mesh drawing continues using the
renderer-specific selection silhouette path; pickable debug
edges and world-space selection behavior are retained. This
removes another direct model/mesh submission from the editor
without importing any genre-specific code into the View.
The default animated/complex RTS portrait remains a compatibility
adapter and must not acquire a second MotionWeave graph evaluator.

### Deferred verification and known limits

`tests/render/model_parallel_jobs.cpp` (CTest
`render_model_parallel_jobs`, native-only, Threads) covers bounded
multi-worker concurrency, revision invalidation, duplicate requests,
new priority classes and worker configuration.
`tests/render/model_owner_estimates.cpp` (CTest
`render_model_owner_estimates`) checks model-owned allocation
estimates exclude borrowed shared textures even across independent
retirement records. These are ordinary CMake test targets.
**NOT RUN:** CMake/compiler, CTest/run-tests.sh, desktop/mobile GPU,
parallel source import stress, editor render/selection parity, FPS/RTS,
sanitizers, benchmarks, CI and screen captures, by the user's
features-before-tests request.

The scheduler currently starts a bounded number of `std::async` tasks,
rather than keeping persistent thread-pool workers, and it does not
support live preemption. Memory accounting excludes unknown
driver allocations and must not be treated as a guaranteed budget.
Shared geometry pool accounting is device-wide, not attributable
without additional ownership tags. Remaining RtsView type appearance,
simulation-specific visuals and animated portrait wrappers are still
not retired.

## Stage 104 — persistent native model workers, shared-page identity and generic mesh selection

### Persistent bounded CPU worker pool

`render::AsyncModelCpuQueue` now runs a *persistent*, lazy-started
`std::thread` pool instead of creating a new `std::async` thread for
each imported model. `setWorkerCount(1..8)` is preserved and the
default stays one worker; no GPU devices, ECS worlds or renderer
operations are touched by the threads. The main/host thread owns
`request`, `invalidate`, `pollReady`, `setWorkerCount` and
`shutdown`; workers share a mutex/condition-variable protected
priority queue, active source revisions and a ready-to-publish deque.
Reader callbacks MUST be reentrant when more than one worker runs.

Queued sources retain the Stage 102 priorities
`Interactive > Visible > Background`, same-priority FIFO,
priority promotion without duplicate work and an occasional oldest
pending dispatch after eight priority bypasses. Worker concurrency
is bounded, existing in-flight imports are never preempted, and
source revisions prevent invalidated results from being published.
`pollReady` returns a single valid prepared result without
waiting for a particular worker; stale results can be skipped.
`setMaxQueued` limits waiting plus prepared-but-unpublished sources,
and `setMaxReady` (default 16) applies backpressure to dispatch when
the ready deque is full (up to a bounded number of concurrently
finishing jobs can overshoot it). The pool therefore does not keep
launching imports indefinitely when GPU publication is paused.

`setWorkerCount` joins any in-flight CPU jobs while reconfiguring
the pool and then restarts persistent threads when work is present:
this operation can block if a reader is slow, so it is not for
per-frame tuning. Valid finished results survive this reconfiguration.
`shutdown()` joins all worker threads, clears their results and
releases captured FileReader/texture-cache state before the project
is destroyed. `AsyncModelCpuQueue::Stats` now also reports the
ready count and number of live persistent threads.
`ModelAssetProvider` continues to publish GPU models only on the
render/device owner thread through `publishReady`; its previous
public priority/budget APIs are source-compatible.

### Shared geometry pages: real identity, no double charging

`render::geometryPoolReferencedStats(gfx::Device&,
span<const Model* const>)` counts unique `GeometryPoolPage`
identities referenced by a **combined** set of models, validating
each page belongs to the passed device. It reports allocated
and occupied page bytes once per page (plus model-reference count)
under the device's geometry-pool mutex. The page's GPU buffer is
never released, reallocated or modified by this statistics function.

`render::ModelResourceCache::geometryPoolModelReferences()`
supplies models both currently resident and pending retirement
behind GPU completion tokens. The generic retirement queue exposes
read-only `forEachPending` so a retired page remains represented
until the GPU-safe retirement actually happens.

The Editor Statistics window reports the pages referenced by the
RTS compatibility view as a **subset** of the device-wide
`geometryPoolStats(device)`, not an additional allocation to
sum with it. A page can be shared by more than one project host:
for an accurate combined report, concatenate the hosts' borrowed
Model pointers and invoke `geometryPoolReferencedStats` ONCE.
These are allocator capacity/occupancy bytes, not driver VRAM
heap measurements. Shared texture cache entries are likewise still
counted once by their resource owner, not multiplied per model.

### Canonical View selection/outline packets

`aiverse::view::MeshObject` has two additive opt-in fields,
`selectionOutline` and `selectionColor`. When enabled,
`aiverse::View::submitMesh` forwards the exact legacy selection
silhouette call to `Renderer3D::drawSelectionMesh`; otherwise it
retains the original ordinary mesh submission (including flags,
specular and emissive settings). These are genre-independent
presentation styles and do not introduce gameplay selection
components in core View.

`apps/editor/SceneRenderable.h` now sends **all** ordinary and
selected/hovered mesh submissions through the canonical View.
The original model `ModelDraw` outline remains intact; the
selected/hovered mesh silhouette colors, tint, world transforms and
draw order are unchanged. The existing pickable selection bounds
edge lines now use `View::submitLine`, while the editor still
chooses when those edges are needed based on its selection outline
pass setting. No additional MotionWeave update/evaluation occurs.

### Deferred verification

`tests/render/persistent_model_pool.cpp`, CTest
`render_persistent_model_pool`, checks bounded worker reuse,
CPU result publication, worker downsize/upsize and final shutdown.
`tests/render/geometry_page_owners.cpp`, CTest
`render_geometry_page_owners`, checks shared page identity across
two independent resource caches without creating a fake GPU device.
`tests/view/packets.cpp` has additional assertions on the opt-in
mesh selection packet (CTest `view_scene_packets`).

These tests are authored and registered but **NOT RUN**, per the
user's features-before-tests preference. Native compilation, all
CTest suites, Metal/Vulkan/GL/WebGL2, graphics parity, thread
sanitizers, editor selection and resource-lifetime hardware checks
remain unverified. Do not merge to `main` based only on source review.

**Known limitations:** persistent threads are per provider, not a
process-wide pool; changing concurrency may join a long-running
reader synchronously; the ready queue limit has a concurrency-sized
overshoot; global geometry page identities are reliable only for
models whose page is still owned on the target device. Backend GPU
completion and multithreaded source parsing still require real
platform verification. `game::RtsView` remains a compatibility
host for authored type appearances and advanced animated portraits.

## Stage 105 — cross-host resource group and fully-authored generic portrait rendering

`engine/render/RenderResourceOwnerGroup.h` is an additive, genre-free,
**non-owning** aggregator for several `render::ModelResourceCache` hosts
(e.g., a scene, a portrait, and a raw-model preview). `add(owner)`
deduplicates the same cache registration; `clear()` never shuts down
those caches. `snapshotOwners(gpuHealthy)` is CPU-only, suitable for
source-level diagnostics without constructing a gfx device.
`snapshot(gfx::Device&)` enriches it with the GPU-device-global
geometry pool and uniquely referenced `GeometryPoolPage` allocations.

Model pointers are counted once by identity across all registered
owners. Each `ModelTextureCache` contributes unique GPU image keys
only once by cache identity (not once per Model borrower).
`geometryPoolReferencedStats` counts a page once even when it is
shared by models owned by different providers; the globally allocated
device pool is included for comparison but **not added again** in the
resource group's `estimatedKnownBytes()`. Source-derived model bytes
and RGBA8-equivalent shared texture bytes are approximate and omit
driver allocations, mip/alignment, offscreen targets, unknown morph
texture height and unrelated renderer subsystems.

`RenderResourceGroupStats` reports unique hosts, live and
GPU-fence-retired Models, shared texture owners/textures, compressed
texture count, resident and retained model estimates, geometry
pool ownership, and retirement diagnostic counters. Its health
status distinguishes `Ready`, `PendingGpuRetirement`, and
`DeviceUnhealthy` (from `Device::gpuHealthy()`). Pending GPU fences
are **not** a device error. `setEstimatedBudgetBytes()`/
`mayAdmitNewSources()` provide optional **new-source admission**
control only. They never bypass a GPU completion fence, evict
resident assets forcibly, or claim to recover a lost GPU.

The editor Model window's optional raw-static-model inspection is a
concrete three-owner consumer: `RtsView::modelResources()`,
`Portrait::modelResources()`, and its own `ModelAssetProvider`
are grouped. A 256 MiB **estimated, group-wide** visual admission
budget is used in this opt-in mode, in addition to the raw provider's
existing per-owner 64 MiB estimated-retirement limit. The UI shows
owner/model counts, estimated known bytes, GPU-pending and
device-unhealthy status. If the backend is unhealthy, new raw
CPU requests and GPU publication are paused; resident visuals
remain available when the device can still present them.

**Static portrait migration:** `game::Portrait::renderAppearance(
const render::ModelDraw&,const render::RTSCamera&,float4 background)`
accepts *fully authored, borrowed* model packets, including material
overrides, node-material assignments, geometry policy, team tint,
variant and optionally already-evaluated pose. It submits through
the canonical `aiverse::View::submitModel` exactly once and draws
the offscreen target using a caller-provided camera and clear colour.
Existing native raw-model `renderStaticModel` and precomputed-pose
`renderPose` now delegate to this one generic path, retaining
their original camera/clear-colour/lighting parameters.
The RTS `Portrait::render(RtsView&, ...)` path first resolves
`StaticPreviewModel` via the legacy host but passes the resulting
full `ModelDraw` to `renderAppearance` with the SAME camera for
static cases. Animated/complex cases still use the original
`RtsView::drawPreview` fallback; there is no second MotionWeave
graph writer or extra animation tick.

**Deferred tests:** `tests/render/resource_owner_group.cpp` checks
independent owner registration, resource model identity counts,
borrowed-texture exclusion, source-derived budget threshold and
unhealthy-device admission with no real GPU. New
`tests/view/portrait_appearance_api.cpp` checks the native
fully-authored portrait API surface at compile time. Both are
ordinary registered CTest targets. **NOT RUN**: CMake compiler,
CTest/unified runner, Metal/Vulkan/GL/GLES/WebGL GPU device,
resource/fence lifetime, editor portrait visual parity, FPS/RTS
gameplay, thread sanitizers and CI; no PASS claim or production
readiness is implied.

**Caveats:** group registration is non-owning and must never outlive
any included cache; the host alone owns and shuts resources down.
The reader/GPU device/lifetime scope of all caches in a device
snapshot must match that device. `gpuHealthy()` defaults to true
on unsupported backends and is not an exhaustive GPU error probe.
The estimated budget is advisory and does not replace real backend
allocation/tracking or force release of unresolved GPU resources.
Full authored animated character/RTS view retirement is not complete.

## Stage 106 — safe resource-owner registrations and explicit failed GPU fence diagnostics

### Expiring owner registrations

`render::ModelResourceCache` now exposes a non-owning lifetime
marker via `lifetimeToken()`. `RenderResourceOwnerGroup` stores
`weak_ptr<const void>` markers alongside raw cache addresses and
checks expiration before dereferencing. A cache invalidates its marker
at the start of its destructor. `add` deduplicates *live*
registrations; a newly constructed cache reusing the same address as
an expired one is a distinct owner. `size()`, `snapshotOwners()`
and `snapshot(device)` ignore dead caches, and diagnostics report
`expiredRegistrations` without dereferencing them.

**Important thread boundary:** registration, cache destruction and
group snapshot are still device-owner-thread operations. A weak marker
alone does not provide a thread-safe GPU cache snapshot or guard
against a destructor racing with a snapshot on another thread.
`RenderResourceOwnerGroup` remains an observer, not a cache owner;
it never takes responsibility for GPU shutdown.

### Failure-aware GPU completion

`gfx::ResourceRetirementStatus` is now a three-state
`Pending / Complete / Failed` API, reported through the additive
`gfx::Device::resourceRetirementStatus(token)` virtual method.
The default maps older backends' boolean completion status to
Complete or conservatively Pending. Metal checks its command-buffer
completion callback's atomic status; Vulkan distinguishes
`VK_NOT_READY` from non-success fence errors (including device
loss); GL checks `glClientWaitSync` results and treats
`GL_TIMEOUT_EXPIRED` as Pending, `GL_WAIT_FAILED` as Failed.

`AsyncResourceRetirementQueue::collect` retains its earlier
five-callback API, and now has an overload accepting an explicit
`failed(token)` predicate. A token-confirmed failure is marked
*once* per batch; the queue reports `failedFenceBatches` and
`unresolvedFailedFences` separately from routine
`pendingPolls` or unsupported-token fallback counters. Failed
batches are sticky: they can NEVER be released by a later token
completion or elapsed time. They are kept until the backend
provides a successful full-device `waitForResourceIdle()`
barrier, whereupon `drain` can safely free the models.

For completed tokens, the existing backend
`endResourceRetirement(token)` remains valid. The additive
`abandonRetirementTokenAfterIdle(token)` is a narrowly-scoped
native marker cleanup hook invoked ONLY after a successful full
GPU-idle barrier; Metal discards callback bookkeeping, Vulkan
destroys the now-idle fence and GL deletes the idle sync object.
This does not turn a failed fence into proof of completion.
`ModelResourceCache` uses the explicit three-state probe and
fence-failure predicate when collecting retired models, and
post-idle cleanup when draining shutdown resources.

`RenderResourceGroupStats::health` now distinguishes
`Ready`, `PendingGpuRetirement`,
`RetirementFenceFailed` and `DeviceUnhealthy`.
Backend-unhealthy takes precedence; a real unresolved fence
failure prevents NEW source admission even if the device
reports healthy. The editor shows failures separately from
normal async completion and resource-pressure status; its
statistics panel exposes per-scene failed token batches.

### Additional canonical View consumer

The material-library preview in `game::Portrait::renderMaterial`
now assembles its existing authored `ModelDraw` (preview shape,
material override, team and rotation) and submits through
`Portrait::renderAppearance` / `aiverse::View`, using the same
camera, clear colour and portrait render targets. The actual
`view.applyMaterial` authoring still resides in the compatibility
adapter. This unifies another static preview with native model
and precomputed-pose consumers without introducing a second
MotionWeave animation evaluation or changing gameplay/saves.

### Validation status and caveats

New source fixtures `tests/render/resource_owner_lifetime.cpp`
(CTest `render_resource_owner_lifetime`) and
`tests/render/retirement_failed_token.cpp` (CTest
`render_retirement_failed_token`) cover weak owner expiry/
replacement and explicit failed-token retention followed by
confirmed GPU idle recovery. Normal CMake targets were registered.
All compilation, CTest/`run-tests.sh`, Metal/Vulkan/GL/WebGL2
GPU checks, editor screenshots, memory profiling, device-loss
stress and sanitizer checks are **NOT RUN** by the user's
features-first request. Source updates do not establish green
CI or production GPU lifetime safety.

Remaining limitations: weak lifetime checks do not make
cross-thread destruction safe; explicit failure states only cover
backends that implement the three-state probe; tokens cannot
include commands not yet submitted on their GPU queue; no
automatic lost-device recreation exists; group memory numbers
are source-derived estimates, not measured driver allocations.
`RtsView` is still needed for complex authored animation and
visual effects. The next agent must validate with real build and
hardware before claiming parity or merging.

## Stage 107 — scoped GPU owner registrations, replayable fence telemetry and generic appearance packets

### Explicit device/thread ownership with scoped registration

`render::ModelResourceCache::bindResourceDevice(gfx::Device&)` fixes
the cache's GPU-device identity and owner thread at its first bind.
Repeated binds to another device or thread fail with `logic_error`;
its earlier non-owning `lifetimeToken()` remains available.
`collectRetired`, `invalidateSources`, `replace`,
`trimUnused`, `publishPrepared` and `shutdown` now verify this
identity; `ModelAssetProvider` verifies it before import/upload,
`RtsView::init` before presentation model setup and
`Portrait::init` before preview GPU creation. After successful,
explicit GPU-safe cache shutdown the device binding is cleared, so
the cache may be reused by a different GPU device. Failed shutdown
does NOT clear binding or make still-in-flight GPU allocations safe.

The recommended multi-host API is now
`RenderResourceOwnerGroup::registerScoped(device,cache)`, returning a
move-only `ScopedRegistration` lease. The group binds the cache to
that device, rejects mixed-device registration, remembers its
constructing `std::thread::id` and throws on wrong-thread
`snapshotOwners`, `snapshot(device)`, `add`, `clear` and budget
changes. The lease automatically unregisters on the original thread.
An expired/destroyed group leaves a harmless weak lease; a lease
destroyed on the wrong thread refuses cross-thread mutation and
increments `wrongThreadReleaseAttempts()`. A live cache that
has been shut down/rebound is rejected by a stale scoped device
snapshot. No registration extends the model cache lifetime.

The old `add(const ModelResourceCache&)` remains as a source-compatible
CPU-only observer method for legacy tools/tests, but new GPU-facing
hosts should use scoped registration. The Editor raw-source Model
window now uses three scoped leases — world, portrait and raw-model
provider — for its group-wide estimated budget and device-health
snapshot. All registration, cache destruction and snapshot operations
MUST be serialized on the device owner thread. This does not magically
make a cache safe for concurrent GPU/CPU mutation or detect a Device
destroyed without respecting the host's lifecycle.

### Replayable synthetic GPU completion trace

`render::RetirementStatusReplay` supplies a deterministic,
CPU-only scripted timeline for opaque GPU marker tokens.
`script(token,{Pending,...,Complete/Failed})`, `issue()`,
`advance()`, `status(token)` and `events()` reproduce status
transitions in a fixed order without timers or a real gfx backend.
Unknown tokens conservatively return Failed, not completion.
This class is for testing and diagnostics, **not** a GPU fence or
an authority for actual memory destruction.

`AsyncResourceRetirementQueue` now has an optional diagnostic
observer receiving `RetirementDiagnosticEvent` events:
`Enqueued`, `TokenSubmitted`, `Pending`, `Completed`,
`Failed`, `IdleFallbackFailed`, `RecoveredAfterIdle`.
Instrumentation callbacks cannot interrupt the resource retirement
operation if they throw. A `ModelResourceCache` host may subscribe
via `setRetirementDiagnosticObserver`; no observer is installed by
default and the user owns logging/retention limits. These events are
informational and never replace explicit GPU completion checks.

### Generic authored material and graph appearance provider

New `engine/view/AppearancePacket.h` exposes
`view::AppearanceSpec` and
`view::buildAppearancePacket(spec, world, view::Object&)`.
A native host can resolve a Model pointer, material override vector,
node-material assignment, geometry policy, material variant, tint,
team, flags, source identity/generation and optionally a shared
animation graph/parameters **outside** any RTS/FPS module.
The builder generates the ordinary canonical View packet without
loading sources, evaluating an animation graph or uploading GPU
resources. For precomputed/static appearances,
`submitPrecomputedAppearance` synchronously sends the packet only
when it has no graph; graph-backed instances must instead use
one owning `View::update` followed by `submitOne`, so no graph is
evaluated twice.

`modules/rts/view/RtsStaticPreviewModel.h` now maps the existing
authored RTS static model/placement fields into the genre-independent
`AppearanceSpec`, then asks the generic builder to compose the
materials/variant/geometry/tint/flags packet. RTS still owns the
structure/air/footprint/yaw placement decision and typed resource
lookup, but **not** the common ModelDraw field construction.
The RTS static-preview packet and ghost placement paths still use
their original transforms, flags and material overrides; no dynamic
simulation or save/network state has changed. Full RTS material
library rule parsing and animated graph authoring remain a legacy
adapter pending separate migration and visual parity validation.

### Deferred verification

New CMake/CTest fixtures:
`tests/render/resource_owner_scope.cpp` /
`render_scoped_resource_owner` (native with Threads) tests
device/thread mismatch, scoped unregister, stale binding after
shutdown and safe detached lease after group destruction;
`tests/render/retirement_status_replay.cpp` /
`render_retirement_status_replay` tests deterministic marker
transitions, sticky failure, owner diagnostic events and explicit
idle recovery;
`tests/view/appearance_packet.cpp` /
`view_appearance_packet` checks typed host-authored appearance fields.
All sources are committed but **NOT RUN** under the features-first
test deferral. Native compiler/CMake/CTest, Metal/Vulkan/GL,
GUI/editor, FPS/RTS, sanitizer and GPU fence stress are unverified.

**Limitations:** per-cache GPU-device identity checks only protect
paths that go through this resource owner; bespoke render targets,
terrain/FX, external direct `uploadModel` calls and a destroyed
gfx::Device are still host lifecycle responsibilities. Scoped
registration is thread-confined, not thread-safe. Synthetic timing
is not an actual GPU completion proof; real Metal/Vulkan/GL tests
are required before merging this work. Full `game::RtsView`
removal and full dynamic material/graph host migration remain
future stages.
