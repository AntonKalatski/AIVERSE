# NEREID Stage 46 — native test fixes: Far shoreline LOD and four JSON fixtures

**Date:** 2026-11-19
**Branch:** `feature/nereid-water-core`; PR #23 Draft
**Parent SHA:** `07b67ec8016d97afdb651a55ea7cf5bb9f6bccaf`; **implementation:** same atomic commit as this handoff.



## Stage 46 — fix two failures observed in the native NEREID CTest run

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`, Draft PR #23. **Base:** `07b67ec8016d97afdb651a55ea7cf5bb9f6bccaf`. **Actual user-reported failures:** `ecs_nereid_surface_mesh_test` aborted with `shore factor missing`; `ecs_nereid_renderer_contract_test` aborted with `JsonError: JSON error at 5:5: expected ',' or '}'`. Separately, VFX failure from the same earlier run still reports the old `gameplay event did not play` text, which was removed in commit `07b67ec8`; rebuild that test binary after pulling.

**NEREID surface test fix:** `WaterSurfaceMesh.h` deliberately suppresses shoreline foam at Far LOD because far river meshes only have perimeter vertices (all would turn white). The test incorrectly required a nonzero river shore factor on Far LOD. It now checks that the Far river keeps zero shore and builds an independent Medium river to assert that shoreline contribution is nonzero, retaining the original Far topology/winding expectations.

**Renderer contract test fix:** Source inspection with JSON parsing of all raw string fixtures found exactly four objects with a missing final root `}`, at original source lines **263, 423, 460, 591** (LOD, refraction, depth optics, underwater visuals). The test expected these JSON examples to load. Added one closing brace to each and verified all extracted raw JSON literals parse using JavaScript `JSON.parse`. No shipping water mesh, renderer or gameplay code was changed.

**Actual verification:** reviewed generator branch, all test JSON strings scanned/parsed, code patch/commit via GitHub connector. **NOT executed:** native macOS C++ rebuild, CTest rerun, GPU shader compiler or genre regressions. The fixes are source-level and not yet proven passing. **Next:** resolve the separately reported duplicate NEREID skill ID/catalog regeneration; then rerun targeted CTest and full suite. Keep PR Draft.


## Files

- `tests/ecs_nereid_surface_mesh_test.cpp` — test Far vs Medium foam policy correctly
- `tests/ecs_nereid_renderer_contract_test.cpp` — close four raw JSON fixtures
- `docs/nereid/HANDOFF.md`, `docs/nereid/ARCHITECTURE.md`, this document

## Later verification on macOS

```sh
cd ~/Projects/AIVERSE
git pull --ff-only origin feature/nereid-water-core
cmake --build build/nereid-test --target aiverse_ecs_nereid_surface_mesh_test aiverse_ecs_nereid_renderer_contract_test -j 4
ctest --test-dir build/nereid-test --output-on-failure -R '^ecs_nereid_(surface_mesh|renderer_contract)_test$'
```

Only claim a passing result when the terminal/CTest output confirms it; there may be subsequent assertions after the malformed JSON and shore checks.
