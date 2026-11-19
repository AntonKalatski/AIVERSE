# NEREID Stage 45.3 — runtime water header dependency (macOS Clang)

**Date:** 2026-11-19
**Working branch:** `feature/nereid-water-core`
**Parent branch commit:** `72197142051e2f411c4d78e882f231f89d3bc2dc`
**Implementation commit:** code and this handoff are committed atomically; resolve via `git log -1`.
**PR:** #23 (Draft, unmerged).

## Actual failure

User supplied the first native macOS build error after Stage 45.2:

```
engine/render/NereidWaterRenderer.h:6:10: fatal error:
'aiverselib/water/NereidWaterSurfaceMesh.h' file not found
  in engine/world/WorldScene.cpp.o, target aiverse_runtime
```

## Root cause and fix

The actual file is `libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h`. `NereidWaterRenderer.h` includes it using the public `<aiverselib/...>` package prefix. CMake's `aiverse_runtime` OBJECT target compiles `engine/world/WorldScene.cpp`, but previously linked only engine/model/postfx/etc. It did **not** consume the NEREID header-only interface library and its `libraries/include` usage requirement. Merely linking the final editor or client executable does not supply compiler includes while an OBJECT library compiles its own sources.

`libraries/CMakeLists.txt` creates `AIVERSE::lib_nereid_water` from the catalog before `aiverse_runtime`; the generated target exports `libraries/include` and root include dirs and links `aiverse_engine`. Fixed `CMakeLists.txt` at `aiverse_runtime` definition:

```cmake
target_link_libraries(aiverse_runtime PUBLIC AIVERSE::lib_nereid_water)
```

This is an ownership-safe target-level dependency, not a hard-coded include path in arbitrary C++ files. PUBLIC is intentional: the headers of `WorldScene` and NEREID are consumed from editor/client and other hosts, and an OBJECT target's public interface must carry the include usage requirement.

## Exact changed files

- `CMakeLists.txt`: adds only the runtime NEREID interface dependency and CMake explanation.
- `docs/nereid/HANDOFF.md`: appends this agent handoff pointer.
- `docs/nereid/stage-45-3-runtime-include-fix.md`: this independent file.

## Actual verification

- Verified: checked remote `CMakeLists.txt`, actual `libraries/CMakeLists.txt` INTERFACE target registration, the missing path's include site, and repository branch ancestry; uploaded the change through connected GitHub with a lease on expected branch head.
- **Not yet run:** CMake regeneration/compile, native tests, GLSL/MSL validation, GPU captures, FPS/RTS/PHYSIS regressions. This does not establish successful build or test PASS.

## Immediate next command on macOS

```sh
cd ~/Projects/AIVERSE
git switch feature/nereid-water-core
git pull --ff-only origin feature/nereid-water-core
cmake -S . -B build/nereid-test -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_TESTS=ON
cmake --build build/nereid-test --parallel 4 2>&1 | tee build/nereid-test/logs/build-retry.log
```

When build completes, run `ctest --test-dir build/nereid-test --output-on-failure --parallel 4`. Report any new first compiler error; keep PR Draft until native build and regression evidence are available.
