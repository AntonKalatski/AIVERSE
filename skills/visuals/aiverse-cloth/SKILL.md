---
name: aiverse-cloth
description: Add AIVERSE XPBD visual cloth (flags, capes, curtains, sails) with the optional `AIVERSE::cloth` CPU solver and `AIVERSE::cloth_render` RHI view - pins, colliders, wind, sleep and LOD, CPU or GPU - without touching the simulation.
---

# Visual cloth (XPBD)

Use for presentation-only cloth. It never changes RTS/FPS simulation, checksums or saves, does not depend on Box3D
(`skills/gameplay/aiverse-physics`) and is not wired into the client, editor, Lua or clothing import automatically -
the host (a native module or app) creates and steps it.

Authority: `docs/cloth.md`. Code: `engine/cloth/Cloth.h` (solver), `engine/cloth/ClothRender.h` (view),
`engine/cloth/ClothGpu.h` (GPU path).

1. Configure with `-DAIVERSE_ENABLE_CLOTH=ON` in your own build tree (never the shared `build/`).
2. Link `AIVERSE::cloth` (+ `AIVERSE::cloth_render` for drawing); create cloths with pins and plane / sphere /
   capsule colliders, step them on the view clock, read positions for the mesh.
3. Use sleep and LOD for many objects; the persistent thread pool distributes whole cloths, results are bit-equal
   across worker counts.
4. Check: `ctest -R '^cloth_'` (pins, no penetration, no drift at rest, wind, stretch limit, thread equality),
   the demo `aiverse_cloth_demo --objects 256 --cells 16 --workers 8`, `examples/cloth/run_bench.py`.

Verified: macOS arm64 CPU and GPU. Not verified: Windows, Linux/GL, Android, iOS, web runtime.

Workflow recipe over an existing engine module; no new library.
