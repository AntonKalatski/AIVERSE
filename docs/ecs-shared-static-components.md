# AIVERSE — Shared and Static ECS components

Status: **opt-in implementation**, branch
`feature/ecs-shared-static-components`. Do not claim equal read latency or
production-readiness until native CTest and benchmark results are collected.

## Why both types?

AIVERSE's production ECS is a deterministic Sparse Set. Keep that layout
for mutable data (positions, velocity, HP, transient AI state). Configuration
data, by contrast, is often identical for thousands or millions of entities.
Copying it in each Entity consumes memory bandwidth, pool memory and snapshot
bandwidth without improving gameplay.

| Storage | Authoritative per-Entity payload | Unique payload storage | Mutability |
| --- | --- | --- | --- |
| Ordinary `T` | `sizeof(T)` bytes | None | Mutable per Entity |
| `SharedRef<T>` | 4 bytes | Interned `SharedStore<T>` World resource | Replace the Entity's reference; never mutate interned T |
| `StaticRef<T>` | 4 bytes | Sealed `StaticTable<T>` asset outside ECS | Config values immutable for lifetime, ref bound once via API |

A `SharedRef<T>` references a World-local interned entry. Static table assets
may be reused by several independent Worlds/Simulations through the same
`std::shared_ptr<const StaticTable<T>>`. Values returned by both APIs are
`const T*` / `const T&`. The static table exposes no mutator at all.

**Not a replacement for ordinary ECS components.** For small or rapidly
changing `T`, ordinary components are often faster and/or smaller. For
frequently changing shared values, each distinct value still consumes one
store slot until `compactShared<T>()` runs at a structural barrier.

## Public API

- `engine/sim/SharedStaticComponents.h`: `SharedRef<T>`,
  `StaticRef<T>`, `SharedStore<T>`, `StaticTableBuilder<T>`,
  `StaticTable<T>`, `SharedReadView<T>`, `StaticReadView<T>`,
  `setShared`, `updateShared`, `readShared`, `compactShared`,
  `bindStaticTable`, `setStatic`, `readStatic`,
  `validateShared`, `validateStatic`.
- `engine/sim/SharedStaticPersistence.h`:
  `registerSharedPersistence` and `registerStaticPersistence`.
- Both are native C++ header APIs without FPS/RTS dependencies, additional
  archetypes or changes to `engine/core/ECS.h`.

### Example — mutable assignment, immutable shared value

```cpp
#include "engine/sim/SharedStaticComponents.h"
namespace cs = aiverse::storage;

struct WeaponSettings {
    int32_t magazine, damage, range;
    // ...potentially hundreds of bytes...
};
// Define io(aiverse::Archive&, WeaponSettings&) in WeaponSettings's namespace
// using field-by-field deterministic Archive IO (never raw memcpy).

cs::setShared<WeaponSettings>(world, player, settings);
cs::setShared<WeaponSettings>(world, enemy, settings);
// Both refs point to one interned, immutable settings object.

cs::updateShared<WeaponSettings>(world, player, [](auto& value) {
    value.damage += 10; // ONLY player obtains a new value/ref.
});
// Enemy still reads the old settings.
const WeaponSettings* weapon=cs::readShared<WeaponSettings>(world,enemy);

// For hot loops, resolve the store ONCE:
cs::SharedReadView<WeaponSettings> view(world);
world.eachExistingConst<cs::SharedRef<WeaponSettings>>(
    [&](aiverse::Entity entity,const cs::SharedRef<WeaponSettings>& ref) {
        const WeaponSettings* config=view.get(ref); // indexed O(1)
        if(!config) return;
        // process using *config, plus ordinary mutable components...
    });
```

The World holds `SharedStore<T>` as a normal copyable resource. Immutable
unique values are owned by `shared_ptr<const T>`, so COW and deep snapshots
share them rather than duplicating large configs. The metadata index is copied
when needed. `World::resource<SharedStore<T>>()` detaches the store if a
snapshot still shares it; this is not a per-entity hot-path operation.

### Example — immutable prefab / unit definitions

```cpp
cs::StaticTableBuilder<WeaponSettings> builder;
const auto light=builder.define(lightWeaponConfig);
const auto heavy=builder.define(heavyWeaponConfig);
auto definitions=builder.seal(); // shared_ptr<const StaticTable<WeaponSettings>>

cs::bindStaticTable(world,*definitions);
cs::setStatic(world,firstUnit,light,*definitions);
cs::setStatic(world,secondUnit,heavy,*definitions);

const auto* config=cs::readStatic(world,firstUnit,*definitions);
// *config is const; no set/edit API exists on StaticTable.

cs::StaticReadView<WeaponSettings> view(world,*definitions);
world.eachExistingConst<cs::StaticRef<WeaponSettings>>(
    [&](aiverse::Entity entity,const auto& ref) {
        const WeaponSettings* config=view.get(ref);
        // A single contiguous immutable table lookup.
    });
```

The builder deduplicates identical canonical values and may define each
prefab once. After `seal()`, it cannot define again. Runtime reads are
contiguous table indexed lookups; no hash is computed in the hot path.
Static config versions are *content identities*: changing the config creates
a new table (and a different fingerprint). It does not mutate a live table.
A host can swap/rebuild an entire World with a new table at a migration
barrier; doing so silently on one peer would desynchronize network play.

## Persistence, rollback and deterministic identity

The caller's `T` needs an `io(Archive&, T&)` overload, encoding only
versioned canonical fields (no pointer, padding, OS handles, ephemeral
presentation data). The default `SharedValueCodec<T>` uses this exact
field-wise representation for both stable 64-bit hashing and equality.
Collision buckets compare full encodings, so hash collisions cannot merge
different values. `std::hash` is intentionally not used.

Call during `GameModule::setup`, **before** persistence registration seals:

```cpp
#include "engine/sim/SharedStaticPersistence.h"
aiverse::storage::registerSharedPersistence<WeaponSettings>(
    simulation, "game.weapon", /*schema version*/ 1);
aiverse::storage::registerStaticPersistence<WeaponSettings>(
    simulation, "game.weapon.base", /*schema version*/ 1);
```

The native v10 ECS save/checksum stores shared values **once per unique
entry**, all Entity `SharedRef` indices, Static Entity indices and a
`StaticTableBinding<T>` fingerprint. The immutable static table's bytes
are an external content asset, NOT duplicated in World snapshots or saves.
The content asset must be retained by the GameModule/host and available
before any simulation. Validate after a successful restore:

```cpp
if(!cs::validateShared<WeaponSettings>(simulation.world()) ||
   !cs::validateStatic<WeaponSettings>(simulation.world(),*definitions))
    throw std::runtime_error("invalid/mismatched shared/static config");
```

A static fingerprint is a deterministic 64-bit content checksum, not
cryptographic content authentication; signed/pinned content manifests should
continue to use project asset SHA-256 or equivalent.

### Important COW details

World snapshots copy `SharedStore` metadata but **share the immutable
T objects**. StaticTable data is completely outside the World and reused
across all snapshots. References are 1-based 32-bit indices, never raw
pointers or addresses, so snapshots and saves remain portable.

`compactShared<T>(world)` deterministically finds live refs and reassigns
new dense slots by ascending OLD slot order. It removes dead unique values
from the current World while old snapshots remain self-consistent. Call only
at a structural barrier after `World::flush()`, without concurrent jobs or
cached `SharedRef` values held outside ECS. Cached IDs and `SharedReadView`
from the current World are invalidated by compaction. Do not compact every
tick; use it after a level unload or when dead entries exceed a threshold.

`SharedStore<T>` does NOT automatically reclaim values on an individual
Entity destruction, because live rollback snapshots may still refer to them.
The native v10 persistence registry has a per-resource payload limit
(currently 32 MiB), so enormous dynamic shared tables need segmentation
or bounded compaction before saving.

## Memory model

For `N` entities, `U` unique configurations and a value size `S` bytes,
the *payload-only* comparison is:

- Ordinary: `N * S`.
- Shared/Static: `N * 4 + U * S`, plus registry metadata.

Example: 1,000,000 entities, 100 unique 128-byte configs:
ordinary 128,000,000 bytes versus ~4,012,800 bytes of indexed/config
payload (96.87% reduction). Real resident savings differ because Sparse
Set stores Entity IDs/sparse indexes, shared store owns heap allocations
and hash buckets, and `T` may own out-of-line vectors/strings.

## Performance and threading

**A shared/static read is O(1), not necessarily equal CPU latency.** Ordinary
`each<T>` reads T inline from the dense array. Shared/static reads one
32-bit slot and then one immutable indexed value. This can be faster when
configs are highly reused and cache-resident, slower if refs are random
or the config table misses L1/L2. `SharedReadView`/`StaticReadView`
resolve the registry/binding once before iterating, avoiding per-entity
resource lookup. Never use a cached View after World restore, compaction
or resource rebinding.

No locks on read; no mutate to interned values. Concurrent read-only jobs
are allowed when the World and registry are not undergoing structural
mutations. Interning, compaction, and static binding run at deterministic
owner-thread structural barriers. Do not mutate the table from parallel jobs.

In future consider grouping read-only passes by static ID to improve locality
when order does not matter. Never reorder authoritative order-sensitive
systems merely for performance.

## Tests and benchmark

- `tests/ecs_shared_static_components_test.cpp` / CTest
  `ecs_shared_static_components_test`: deduplication, immutable reads,
  static bind-once, independent per-entity update, COW snapshots, compaction,
  native Simulation v10 save/load/checksum/rollback, wrong-table rejection.
- `tests/ecs_shared_static_bench.cpp`, target
  `aiverse_ecs_shared_static_bench`: Release microbenchmark with 200k
  entities and 64 unique 128-byte configs, normal vs shared/static
  checksum-equivalent read loops; prints memory payload estimates and
  median read times. Not a performance gate.

**Native verification (2026-11-19, Mac Studio, AppleClang 17, Release,
commit `11568cd16`):** the targeted `ecs_shared_static_components_test`
compiled and passed (**1/1**, 0.39 sec test, 0.40 sec total). On
200,000 entities with 64 distinct 128-byte configurations, median CPU
read microbenchmark timings were Ordinary **0.309 ms**, Shared **0.089 ms**,
Static **0.096 ms**. Payload-only figures were 25,600,000 bytes vs
808,192 bytes. This corresponds to approximately **3.47x/3.22x** faster
read passes and **96.84% less** config/index payload in this *specific*
high-reuse setup.

These are not measurements of process RSS or whole-frame performance.
Sparse Set and registry overhead, cold-cache cases, unique-value variation,
writes/compaction, broader CTest, cross-platform and RTS/FPS save/replay
parity remain unverified. Keep the feature opt-in until those pass.

```sh
git fetch origin feature/ecs-shared-static-components
git switch feature/ecs-shared-static-components
git pull --ff-only origin feature/ecs-shared-static-components
cmake -S . -B build/ecs-shared-static -DAIVERSE_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/ecs-shared-static --target \
  aiverse_ecs_shared_static_components_test \
  aiverse_ecs_shared_static_bench --parallel 8
ctest --test-dir build/ecs-shared-static --output-on-failure \
  -R '^ecs_shared_static_components_test$'
./build/ecs-shared-static/aiverse_ecs_shared_static_bench
```

## Release COW validation follow-up — 2026-11-19

On Mac Studio, the four requested COW/hierarchy test targets compiled and
CTest reported **4/4 Passed** (1.94 sec total) after updating to
`730d22cf8`. However, two of those targets
(`ecs_cow_snapshot_test`, `ecs_cow_parallel_test`) used standard
`assert` checks which `NDEBUG` removes in a Release build. AppleClang
reported 5+3 unused-argument/variable warnings as evidence. The parallel
test's `assert(exercise(workers)==expected)` also skipped executing
the 1/3/7-worker cases entirely.

This branch now replaces all **33** such checks with
`AIVERSE_TEST_CHECK`, including the worker cases; code commits
`f858af26` and `8b89d584`. **Re-run the two COW tests in Release**
before claiming the new checks have passed. The two SceneHierarchy tests
reported Passed without these warnings. The separate Shared/Static
regression test already used AlwaysCheck and passed 1/1.

## Integration roadmap

1. Compile and correct native test/benchmark, measure cache behavior on
   Apple Silicon and other target platforms; compare real RSS and COW
   allocation counts, not just payload estimates.
2. Use `SharedRef<T>` for identical or nearly identical large runtime
   configuration data, `StaticRef<T>` for immutable prefab/asset-defined
   settings. Leave HP, position, velocity, active cooldown, mutable inventory
   and per-entity combat state as ordinary components.
3. Add typed prefab configuration registries to the SceneAsset instantiation
   system without allocating one copy per scene Entity; preserve rollback.
4. Optimize lookup with a prepared query/view facade only if native
   benchmarks justify it; consider compressed 16-bit indices only for
   well-bounded tables and if save compatibility can be preserved.
5. Validate two independent GameModule hosts, native editor usage and
   legacy FPS/RTS serialization before any production migration.
