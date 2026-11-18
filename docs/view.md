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
