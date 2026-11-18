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
