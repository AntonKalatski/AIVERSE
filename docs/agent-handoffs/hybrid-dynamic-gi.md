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


## Stage 2 - 2026-11-19 - CPU reference for dynamic one-bounce diffuse GI (source committed, NOT VERIFIED)

Implementation commits:
- 5e3c94be248cd02dcde17b3a6a790da343207aeb - engine/render/GiDynamicProbeReference.h
- 4b9a4183525f3ea0c790ca30d73cc4393c9dfe62 - tests/render/gi_dynamic_probe_reference.cpp
- a41a391526470a21af0bb1bfa55155961e35daf2 - CMakeLists.txt, registers gi_dynamic_probe_reference in the default ALL/CTest test runner.

New API: aiverse::render::GiMovingLight has stable key, world position, linear color, influence radius and intensity. GiDynamicProbeReference::configure(origin,spacing,dims,samples,traceDistance) creates an independent dynamic-only L1 probe coefficient layer. setLights(sorted-by-key internally) compares per-light inputs and invalidates old/new influence bounds, including an indirect transport margin. setGeometry(const StaticShadowGeometry*, revision) forces all probes dirty when the geometry generation changes; setting null clears the overlay. update(probeBudget,camera) consumes a bounded number of scheduler tickets and traces deterministic directions from each chosen probe to static BVH surfaces; incident moving-light radiance is visibility checked, modulated by surface albedo and projected to L1 directional coefficients. coefficients() and coefficient(index) return **only bounced indirect light**: they must be added once by a future shader, never mistaken for direct point light.

Current intentional limits:
- CPU single-threaded reference for validation/small volumes; NOT a GPU compute implementation or real mobile performance path.
- Only one bounced-light order, no dynamic-geometry tracing unless host republishes an up-to-date BVH revision, no skinned meshes, no reflection probes, no lightmap/renderer binding yet.
- Still no production GI activation; current baked GI assets and shaders are untouched. No ECS/serialization/network schema changes.
- Indirect influence uses an expanded AABB, not a precise per-ray dependency graph. Large lights may mark an entire volume dirty, but update budgets prevent all probes from processing at once.
- No stale-cache content hash defined yet for moving light / geometry snapshots. Host must supply correct revisions.
- CPU reference produces a dynamic L1 **delta**. Temporal convergence, visibility moments, relocation and GPU texture layout are upcoming work.

Tests ACTUALLY executed: GitHub fetch/write operations only. **NOT RUN**: C++ compilation, CTest, full run-tests.sh, GPU tests, native device timings. Added Release-safe regression asserts using AIVERSE_TEST_CHECK for colored indirect bounce, light on/off, no-op unchanged inputs, geometry generation changes, small update budgets and invalid light IDs. These are source tests; do not label PASS without native execution.

Required next steps:
1. Verify both GI targets in a local cloned worktree:
   cmake -S . -B build/gi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
   cmake --build build/gi --target aiverse_gi_update_scheduler_test aiverse_gi_dynamic_probe_reference_test -j8
   ctest --test-dir build/gi --output-on-failure -R '^(gi_update_scheduler|gi_dynamic_probe_reference)$'
   Record exact tested SHA and any compiler warnings/failing inputs in the NEXT handoff; run ./run-tests.sh --quick when feasible.
2. Build a shared explicit GI composition contract, including baked base versus dynamic delta and sky occlusion, so static lightmaps and dynamic transport never double-count point lights, sun or ambient. Add test-only CPU compose fixtures before hooking it to material shaders.
3. Implement GPU update/upload via a capability-checked backend, beginning on Metal. Need independent dynamic probe texture, per-frame budget and asynchronous-safe publish; do NOT upload the entire RGBA32F static atlas each frame. Add device-free RHI fake tests, then GL/MSL visual parity. Only after that integrate renderer, ECS dirty events, TerraWeave and cascade clipmaps.
