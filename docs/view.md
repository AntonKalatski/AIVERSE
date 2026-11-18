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

