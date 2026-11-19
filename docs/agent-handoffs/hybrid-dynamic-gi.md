# Hybrid Dynamic GI - living agent handoff

Branch: feature/hybrid-dynamic-gi (based on main d71c6d47e532b25fb816a8bb1f96c41e5275a165, 2026-11-19).
Scope: shared genre-independent renderer presentation, not authoritative ECS gameplay state.
Before any continuation read docs/probe-gi.md, docs/lightbake.md, docs/sponza-handoff.md, and docs/metal-batching-agent-handoff.md.
MANDATORY: After each bounded implementation stage commit code, then append a documented handoff in a SEPARATE commit to the same branch. Keep all previous entries.

## Existing GI inventory

- engine/bake, BakedLighting, Renderer3DBaked: offline CPU path-traced lightmaps plus a compact L1 probe grid; shader variants LIGHTMAP/PROBES.
- StaticDiffuseField, StaticLightGrid, LightSystem: a separate fixed-scene sunlight/sky transport probe cache; .avgic format AVGIC005; RGBA32F shader atlas.
- ScreenLighting: screen-space color gather and temporal pass, not geometric dynamic GI.
- Moving occluders and arbitrary moving emissive lights do not currently update global transport. gfx::Device compute is optional, present in Metal but not guaranteed on GLES/Vulkan.
- Historical M3 Ultra Sponza Metal 720p full-scene medians from docs/probe-gi.md: GI off 8.41ms, Mobile 13.18ms, High 14.49ms. Not measured in this stage; no Android performance proof.

## Roadmap

1. Unify indirect GI ownership/contracts, static/dynamic composition and sky handling; preserve compatibility and avoid double counting.
2. Add generation-safe dirty bounds, bounded near-camera updates and fair scheduling.
3. Add real dynamic ray-based probe transport (light/geometry proxy, relocation/classification, visibility moments, temporal convergence) that consumes tickets, without rebuilding all probes each frame.
4. Connect dirty events from ECS changes, TerraWeave digging, falling vegetation, destruction, moving lights, and streamed world chunks. ECS publishes events after structural barriers; renderer owns transient floating-point data.
5. Add clipmap/cascade volumes, compact cache after quality tests, quality tiers and fallback, optional SSGI detail.
6. Verify Release/Debug CPU, GL/Metal GPU images, native mobile timings, stability and source-hash invalidation. Never unconditionally rebake a large world during gameplay.

## Stage 1 - 2026-11-19 - deterministic GI invalidation scheduler (source committed, NOT VERIFIED)

Source code commits:
- a1626416cd499566ed4f7d048946b2eed6dad925 - engine/render/GiUpdateScheduler.h
- 87d400c9fd6a762f2ba226694ef39a94d2e04539 - tests/render/gi_update_scheduler.cpp
- a3c484817165272f6e5ce724f9f8990a36465a3a - CMakeLists.txt, CTest registration for gi_update_scheduler in default ALL build
- 4428762ae5601fbf0360bd2b74ee028840abb10e - AGENTS.md, GI ownership and handoff convention

Public API: aiverse::render::GiUpdateScheduler in GiUpdateScheduler.h is header-only and GPU-free. configure(origin,spacing,nx,ny,nz) allocates up to 20,000 probe slots; resetClean() marks an imported valid cache up-to-date; invalidate(bounds, reason, influenceRadius) and invalidateAll(reason) produce dirty regions. claim(budget, focus) returns deterministic GiProbeTicket batches; complete(ticket) only acknowledges the same probe revision; stale completion cannot erase newer edits. abort(ticket) releases work for retry. pending() and inFlight() report backlog. Three focus-priority batches alternate with one oldest-first fairness batch.

Compatibility: Existing lightmaps, GI shaders, .avgic assets, renderer bindings, ECS saves, networking and gameplay have not changed. No runtime GI quality tier is enabled. THIS STAGE IMPLEMENTS ONLY SCHEDULING, NOT ACTUAL DYNAMIC TRANSPORT. No production GPU work has been performed.

Validation ACTUALLY executed: inspected upstream GitHub source and confirmed GitHub source commits. NOT RUN: native CMake configure, C++ build, CTest, full ./run-tests.sh, GL/MSL shader compilation, GPU images, hardware profiling, Android/iOS. Current execution container cannot resolve github.com for a clone and has no mounted AIVERSE repo. New tests are registered and use tests/AlwaysCheck.h, not Release-stripped assert; they are not claimed to pass.

Next precise action:
1. Pull feature/hybrid-dynamic-gi. Configure with cmake -S . -B build/gi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF. Build cmake --build build/gi --target aiverse_gi_update_scheduler_test -j8, then ctest --test-dir build/gi --output-on-failure -R '^gi_update_scheduler$'. Fix any compile or runtime issues and record the tested SHA and result here, in a separate handoff commit.
2. Implement a CPU-reference dynamic indirect probe transport layer that consumes dirty tickets, projects bounce into the existing L1 directional format but stores an independent dynamic-only delta (never overwriting the baked base). Use a bounded light budget, immutable geometry reference and no per-frame full-scene recook. Add tests that a moving colored light changes indirect while direct light stays independent and invalidation during an in-flight job triggers reprocessing.
3. Integrate with LightSystem/Renderer3D only after explicit composition strategy and baseline camera/GL/Metal parity; use optional compute only when supported. Geometry mutation needs an updatable BVH or SDF/proxy acceleration structure; relighting static probes alone cannot handle holes, doors or falling trees.

No merge into main was requested. Experimental Metal ICB flags remain disabled.
