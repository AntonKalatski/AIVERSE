# Native ECS Inspector extension registration

The editor Inspector reads components from the **actual ECS World**, not from map-authoring `DocEntity` placeholders.

## Existing reader

`apps/editor/core/WorldInspect.cpp` calls `inspectEntity()`, which queries `World::has<T>` for built-in ECS pools and serializes the fields under `{"components": {"ComponentId": {...}}}`. The common `DetailsPanel` draws one card for each **present** component. `DocEntity` metadata, when relevant, appears separately.

## Opt-in reader registration for additional C++ ECS components

A native C++ module **linked into the editor process** can register an inspector reader after its component type is available and before the editor begins inspecting objects:

```cpp
#include "apps/editor/core/WorldInspect.h"

void registerVehicleInspector() {
    using namespace aiverse;
    using namespace aiverse::editor;

    registerWorldInspectorExtension({
        "VehiclePhysics",
        "Vehicle Physics",
        {{"speed", "Speed", WorldFieldKind::Number},
         {"engineOn", "Engine On", WorldFieldKind::Boolean}},
        [](const World& w, Entity e) {
            return w.has<VehiclePhysics>(e);
        },
        [](World& w, Entity e) {
            // The registry calls read only for a live entity that has this pool.
            const auto& v = *w.tryGet<VehiclePhysics>(e);
            Json fields = Json::object();
            fields.set("speed", Json::number(double(v.speed), 3));
            fields.set("engineOn", Json::fromBool(v.engineOn));
            return fields;
        }
    });
}
```

`VehiclePhysics` above is **an example component type**, not an AIVERSE built-in. Its declaration and actual numeric conversions must be supplied by the project; do not copy this example into the engine as an imaginary component. For Fixed fields use the correct `toDouble()` conversion instead of a raw cast.

**Registration contract**

- `id` must be nonempty and unique. Built-in ECS component IDs cannot be replaced.
- `has` performs a real `World::has<T>()` check. `read` returns only the real component's current fields as a JSON object.
- Field IDs must be unique and map to keys returned by `read`. Unknown fields still display with the default JSON renderer; supported registered field kinds are used for labels/formatting.
- Readers are copied into a thread-protected snapshot before inspection, allowing safe registration changes. **Module unloading requires an explicit synchronization barrier**: unregister the extension and ensure in-flight snapshots/reads are complete before unloading its code. Unregister alone does not wait for callbacks in flight.
- This is a **C++ in-process registration API**, not yet an exported stable C ABI for separately loaded native game plugins. Connecting existing native-module lifecycle hooks remains future work.
- Read callbacks run against the editor's current World; they must be observational, even though `World::tryGet<T>` currently requires non-const `World&`.
- Current editor Hierarchy and world index bitsets are still based on the fixed RTS descriptor list; these extensions add real components to Inspector snapshots but do **not** automatically add new Hierarchy component filters.
- An unavailable or uncompiled native module cannot expose its runtime component pools.

## Editing and persistence

The presence of a field in an ECS snapshot does **not** imply that editing is safe. The preview can be regenerated from project data. The current `EditorEcsWriteback.h` explicitly allows only `Transform.pos` (authored RTS X/Z) and `Owner.player` (authored RTS owner), persisted back to the map. All other fields remain read-only until their owning module supplies a validated writeback/persistence strategy and undo/redo.

The built-in RTS `Transform` stores `Vec3 pos`, `Vec3 prevPos`, and `Fixed yaw`, **not** a generic Euler rotation or Scale. Do not invent Rotation/Scale values if the registered component does not have them.

## Verification checklist

1. Build AiverseEditor on macOS with the extension registered in-process.
2. Confirm an entity with the native ECS pool shows exactly one correctly titled component card and its real fields.
3. Confirm an entity without that pool shows **no** card, and built-in component cards remain unchanged.
4. Confirm duplicate IDs are rejected and unregister/re-register works when no inspector reads are in flight.
5. Inspect multi-selection, filtered fields, and the unavailable native-module case.
6. Keep native fields read-only unless explicit persisted setter and Undo tests exist.

**Current validation status:** API source edits were reviewed; no successful macOS build, runtime extension test or visual inspection has been recorded for this stage.
