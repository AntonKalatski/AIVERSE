---
name: aiverse-derived-keys
description: Remove per-tick string building and map lookups from AIVERSE native modules that keep per-entity values in string-keyed maps (trigger variables, NativeState values) with the derived-keys caches, without changing results.
---

# derived-keys

Header: `libraries/include/aiverselib/runtime/DerivedKeys.h` (`AIVERSE::lib_derived_keys`). Tests: `tests/libraries/city_tests.cpp`.

1. Profile first (`aiverse_headless --profile` or the module's own timers): this helps when a system builds `"<prefix><id><suffix>"` strings or looks the same keys up for many entities every tick.
2. Dense ids (population index): `thread_local runtime::DenseKeys key("resident_", "_home"); vars.find(key(i))`.
3. Entity handles: `runtime::SparseKeys` (bounded, dropped whole past its limit).
4. Node pointers: `runtime::NodeCache<N>::lookup(map, id, keyOf)` only for node-based maps that expose `erasures()` / `inserts()`; any erase, rebuild or load must bump `erasures()`.
5. A cache is never saved, hashed or rolled back. Prove it: checksums and saves equal with and without the cache (same seed), then keep the cheaper path.
