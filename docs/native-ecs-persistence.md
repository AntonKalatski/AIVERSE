# Native ECS persistence (v10)

Call `Simulation::registerPersistentComponent<T>(stableId, version)` or
`registerPersistentResource<T>(stableId, version)` **during setup**. This works
in native C++ GameModule and RTS NativeModule setup without modifying engine core.

```cpp
namespace mygame {
struct Ammo {int32_t rounds=30;};
void io(aiverse::Archive& a,Ammo& v){a(v.rounds);}
}
// During setup:
sim.registerPersistentComponent<mygame::Ammo>("mygame.ammo",1);
```

The overload `io(Archive&,T&)` must be visible through ADL and must serialize
fields explicitly in stable order using `Fixed`, integers, strings, vectors,
maps and Entity handles. Never serialize raw C++ object bytes/padding, pointers,
wall-clock values or non-deterministic containers.

The registration is **per Simulation**, with unique stable ASCII schema IDs and
positive versions, sorted by ID independent of C++ dynamic type-ID allocation.
The registry saves the complete pool (including Entity index+generation and
dense iteration order), singleton-resource presence/value and schema versions;
it also hashes these fields using the same portable Archive encoder.
`SimSnapshot` already copies the World and includes native pools/resources in
normal and COW rollback. Invalid or stale entity references, duplicate entries,
unknown type/version or malformed/oversized chunks fail closed. Custom ECS chunk
loading is transactional; the entire pre-existing module deserialize process is
not yet transactional.

No native types registered: disk format and checksums stay **v9**, unchanged.
Any registered type: version **v10**. Loading v9 into a build requiring native
registered types is rejected rather than discarding data. Versions are strict:
migration callbacks for changed native schemas remain future work. Old RTS
fixed pool checking still refuses **unregistered** extra pools.

Validation: `tests/ecs_native_persistence_test.cpp` (CTest
`ecs_native_persistence_test`) and standalone ECS runner. Run native builds,
macOS/Windows/Linux, sanitizers, real RTS/FPS parity and network replay before
claiming production readiness. These checks have not yet been run for this edit.

## Explicit schema migrations and source checksum verification

A native component's current `io(Archive&, T&)` always writes the latest schema. To read an older schema, register a decoder in the same setup scope, **after** registering the current type:

```cpp
sim.registerPersistentComponent<mygame::Ammo>("mygame.ammo", 2);
sim.registerPersistentComponentMigration<mygame::Ammo>("mygame.ammo", 1,
    [](aiverse::Archive& old, mygame::Ammo& upgraded) {
        old(upgraded.rounds); // v1 had only rounds
        // initialize any new v2 fields explicitly
    });
```

The version mismatch is rejected unless an exact, explicit reader exists. The reader is invoked for every saved value and must consume precisely the old bytes. Resources have an equivalent `registerPersistentResourceMigration<T>` API. A migration is performed in a temporary World. The loader checks the **original v10 checksum using the original encoded component chunks**, not the upgraded values, before accepting the migration. This requires GameModule::hashState to avoid duplicating the hashes of registered native fields (the registry owns them). Schema name/kind, entity generations, duplicate entities, payload bounds and truncation remain fail-closed. Re-save after migrating to emit the new schema.

The full Simulation-owned state (World, tick, RNG, players, queued and executed commands, events) is restored if decoding or checksum validation fails. Arbitrary GameModule private side effects cannot be rolled back by this mechanism; modules must keep authoritative state in World and rebuild derived caches in state-restored hooks. This does not make untrusted gameplay modules safe to execute.
