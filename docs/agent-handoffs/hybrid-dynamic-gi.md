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


## Stage 3 - 2026-11-19 - sparse GPU upload of dynamic-only probe coefficients (committed, NOT VERIFIED)

Implementation commits:
- 0bd0a8722a514eef2534df2e2bcc6967f33eb2bc - engine/render/GiDynamicProbeTexture.h
- 51b4ec1d5faeb70524f569babb6507e9ce99a0a9 - engine/render/GiDynamicProbeReference.h, optional list of successfully published probe indices
- bc199b50ff180b6288177d47d4f963ac23a07222 - tests/render/gi_dynamic_probe_texture.cpp, FakeDevice-based upload boundary cases
- d532068a0a82a2edcad27c6fb02b30054fb8155b - CMakeLists.txt, registers gi_dynamic_probe_texture as default CTest target

API/behavior: GiDynamicProbeTexture::initialize(gfx::Device&, probeCount) allocates a separate RGBA32F nearest data texture with 384 texels per row (128 probes, 3 float4 RGB L1 coefficient rows per probe). This data is DYNAMIC INDIRECT ONLY, never the existing StaticLightGrid atlas. GiDynamicProbeReference::update(budget,focus,&publishedIndices) appends only successfully published probe IDs. GiDynamicProbeTexture::upload(reference, ids) sorts and deduplicates, merges adjacent ids within a row into one Device::updateTexture call, and reports probes / regions / bytes; uploadAll() rehydrates after device recreation. The maximum volume remains 20,000 probes (~1 MiB raw texture storage); no full-atlas upload on normal incremental updates. Release destroys the GPU texture.

Compatibility: no change to old lightmap or AVGIC005 storage, shader permutations, render quality default, ECS save format or direct light. This only provides upload-ready data. The renderer DOES NOT YET SAMPLE the dynamic texture. It does not implement GPU ray tracing or compute, and the current probe reference still uses bounded CPU BVH rays.

Actual validation: GitHub write/fetch operations succeeded. **NOT RUN:** native C++ build/CTest, GLES/Metal texture update verification, shaders, GPU performance, ./run-tests.sh. The CTest target uses existing tests/cloth/FakeDevice.h to inspect packed coefficients and adjacent-row grouping; it must be compiled and checked in Debug and Release.

Next agent actions:
1. From this branch run cmake --build build/gi --target aiverse_gi_update_scheduler_test aiverse_gi_dynamic_probe_reference_test aiverse_gi_dynamic_probe_texture_test -j8 after configuring with AIVERSE_BUILD_TESTS=ON. Then ctest --test-dir build/gi --output-on-failure -R '^gi_(update_scheduler|dynamic_probe_reference|dynamic_probe_texture)$'. Fix all warnings, failures and native portability issues; append exact tested commit SHA and report in separate handoff commit.
2. Design and test the SINGLE indirect composition point in lit shaders for baked static base (lightmap OR legacy probes OR scalable field), plus dynamic-only L1 texture and optional SSGI near-field term. Explicitly allocate a safe sampler slot for GLSL/MSL, validate Metal 16 sampler slots and shader-graph material variants. Ensure static point direct lighting never leaks into the bounced delta, and no double-addition under the prior LightSystem static grid. Do not silently enable across all materials.
3. Once shaders are verified, publish dynamic source / geometry change events from read-only ECS presentation snapshots and implement capability-backed GPU probe tracing instead of this CPU reference for large-volume 30/60 FPS operation; then hierarchical clipmaps, visibility/relocation, and real Android/iOS profiling.


## Stage 4 - 2026-11-19 - single-owner GI composition contract (source only; NOT TESTED)

Source commits:
- be48911eb40a7ecced5d26f8c3695ceb8bbf6ff0 - engine/render/GiLightingComposition.h
- 68dd085c4446c66642c6f4e0f239c8ba85ad43e2 - tests/render/gi_lighting_composition.cpp
- 945611e8f9db6e069d9425b2e7f893617253e5f7 - CMakeLists.txt: gi_lighting_composition test in ALL/CTest.

Semantics: GiBaseSource selects exactly one of Hemisphere, BakedLightmap, BakedProbes, ScalableProbes; other base values must not be summed. GiIndirectLayers accepts independent dynamicBounce and optional screenBounce as *incident*, linear, nonnegative indirect RGB and indirectOcclusion [0..1]. composeIndirectGi chooses the one owner, adds dynamic and screen delta exactly once, applies AO once. The direct-sun/point path stays independent and must not be sent into this compositor. Dynamic sources present in an immutable baked base must be explicitly excluded from the dynamic delta, or the bake must be regenerated; this API cannot magically subtract historical baked lighting.

Scope/compatibility: header-only CPU/reference contract and always-on regression assertions. There is no renderer or shader binding yet; old baked/AVGIC formats, gameplay networking, editor and save formats are unchanged. The next shader stage must mirror these semantics; it must preserve the existing default when dynamic GI is unbound.

Actually executed: read existing model shader and RHI source via GitHub and successfully committed the above files. **NOT RUN:** native compile, CTest, GPU shader validation, ./run-tests.sh, device measurement. This remains unverified until owner or an agent can run native commands in a working clone.

Next exact action: implement opt-in model shader sampling of the independent dynamic delta with correct volume mapping and AO exactly once. CAUTION: model fragment textures 13+ are currently shader-graph textures (Renderer3DModels.cpp: kGraphTextureSlot=13), and Metal has sampler limits. Do not collide with graph samplers or corrupt GPU ICB material bindings. A safe first stage is to enable dynamic GI only for models without graph textures while retaining the old path for all others. Reserve an unused uniform slot carefully, bind dynamic data only for color phases, and never force shader permutation changes in the default GI-off configuration. Add always-on sampling contract and shader-variant checks before broadening to shader graphs / terrain / water.


## Stage 5 - 2026-11-19 - opt-in GLSL/Metal dynamic probe sampling for PBR models (source committed; NOT VERIFIED)

Implementation commits:
- 10e14e62763b02438d8923271ae6739dce9c45a5 - engine/render/GiDynamicShaders.h
- 3b34d758299d44abe7eae3b7c5bd109dba3a83ab - engine/render/ModelShaders.h, dynamic overlay in indirect diffuse path (GLSL + Metal)
- c4d937b7da680b3f75ad5efc02cf849963372184, 731f0a4be5b3e6c3f452cd840ca03ba3af6398a9 - engine/render/Renderer3D.h, borrowed overlay API and shared eligibility gate
- b329f25f6ae2b5c40b53d581d7041f2e941d7619, fac7e8744bfa8a17991e9889ba2e63b8364a0dd6 - engine/render/Renderer3DModels.cpp, shader variant/binding and HDR/Medium+/device eligibility
- 8839e554fa8f1f5747c035cd5350a91ae8336a1e - engine/render/GiDynamicSampling.h, CPU-reference sample math
- d04e94a422073945ef8818b7db698cb141e953b4 - tests/render/gi_dynamic_sampling.cpp
- d482ae8e04b323e0c6c210b7b8d4739723887862 - CMakeLists.txt, gi_dynamic_sampling test
- 7bf5852d07a3855a5680710f8c28264d99272aab - engine/render/GiDynamicProbeTexture.h, exposing the owning gfx::Device to check safe borrowed textures

Public API: Renderer3D::setDynamicGiOverlay(const GiDynamicProbeTexture* overlay, float3 origin, float spacing, int nx,int ny,int nz) validates atlas readiness, matching device, grid dimensions and finite parameters; clearDynamicGiOverlay() disconnects it. Caller MUST retain GiDynamicProbeTexture, keep it on the same RHI, and supply an atlas with coefficients containing only indirect radiance from sources not already counted in the selected static base. This is opt-in; existing no-overlay models are unchanged.

Integration: GLSL/Metal material shader DYNAMIC_GI variant uses tex13 and U6 (Metal buffer(7)), samples the independent 384-wide 3-texels-per-probe atlas by four tetrahedral neighbors, fades at the volume border, adds the dynamic-only indirect result into ambient PBR prior to material AO. The existing scalable-field indirect term is suppressed only in DYNAMIC_GI variants that already use baked lightmaps or baked probes, while direct local lights and sunlight remain. The old GI-disabled shaders remain unchanged. Renderer only generates this variant in color phases 1/2, in HDR at Medium/High material quality, for lit models without active Shader Graph textures, and without GPU ICB instance or instance palette mode. Shader Graph textures begin at slot 13; this gate prevents a sampler collision. Terrain/basic meshes, water, GPU authored draw variants, low/LDR and models with graph texture inputs are NOT integrated yet.

Test source: gi_dynamic_sampling compares lattice-centre evaluation and tetra interpolation against GiDynamicProbeReference, checks zero outside and at fade boundary, and checks that both model shader strings have dynamic GI guards/expected bindings. NOTE string checks are NOT a replacement for compiling GLSL/MSL variants on actual devices.

Compatibility: no change to .avgic/lightmap version, active default quality policies, ECS persistence or networking; old rendering stays active when overlay pointer is null. Current dynamic probe reference still traces on CPU, so this does NOT establish mobile-rate dynamic GI. No built-in owner currently creates/configures a GiDynamicProbeTexture or calls setDynamicGiOverlay from game/editor; the hook is intentionally opt-in.

Actual validation: GitHub source read/write succeeded; source-only review of shader slot occupancy and CMake registration. **NOT RUN:** native build, CTest, GPU GLSL/MSL compilation and captures, mobile GPU profiling, ./run-tests.sh --quick. Do not declare compile/visual parity without evidence.

Next precise action:
1. Pull branch. Run native:
   cmake -S . -B build/gi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
   cmake --build build/gi --target aiverse_gi_update_scheduler_test aiverse_gi_dynamic_probe_reference_test aiverse_gi_dynamic_probe_texture_test aiverse_gi_lighting_composition_test aiverse_gi_dynamic_sampling_test -j8
   ctest --test-dir build/gi --output-on-failure -R '^gi_'
   Run ./run-tests.sh --quick when feasible. Fix every compile warning/assertion and record exact SHA. Critically compile real GLSL and MSL opt-in variants with texture slots 13, uniform 6, HDR PBR and LIGHTMAP/PROBES.
2. Add lightweight host-side runtime ownership so a game/editor can allocate/reference the volume, apply changed lights and geometry revisions, update <= budget probes/frame and upload only returned dirty IDs; then call setDynamicGiOverlay() before drawModel. Instrument light-to-indirect latency and upload bytes. Make ownership and teardown explicit.
3. Add GPU compute tracing on Metal behind capabilities and a conservative fallback; integrate terrain, Shader Graph texture-slot remapping, and dynamic occluder BVH/SDF. Never silently enable GPU ICB experimental flags. Full 30 FPS Android claim requires actual device measurements.


## Stage 6 - 2026-11-19 - opt-in dynamic GI host runtime/controller, invalidated geometry clears ghosting (source committed; NOT VERIFIED)

Implementation commits:
- bf654fec4ffa40e34d18ad3a7357b4385ae72755 - engine/render/GiDynamicProbeReference.h clears stale dynamic light coefficients whenever the geometry snapshot/revision changes, instead of retaining transport through a removed obstacle.
- 29feae08ed84953dd2d96f19b0577a7441c77734 - engine/render/GiDynamicRuntime.h, owner of CPU reference and independent GPU probe atlas.
- 197735cfe05bb581ef133e1a658b32c962ad73e5 - tests/render/gi_dynamic_runtime.cpp, FakeDevice test for update budget, geometry reset, upload size and shutdown.
- 3e1198dd9db814864705b0f8d1e9dd87c3c20664 - CMakeLists.txt, gi_dynamic_runtime registered in default ALL / CTest.

Public API:
- GiDynamicRuntime::initialize(gfx::Device&, origin, spacing, nx,ny,nz, raysPerProbe, bounceDistance) configures a reference GI volume and allocates its own independent RGBA32F texture.
- setGeometry(const StaticShadowGeometry*, revision) changes the immutable traceable geometry snapshot; resets stale CPU coefficients and marks the full atlas for zeroing before the next update (even budget=0). Host must rebuild a geometry snapshot when triangles change; incrementing revision of an unchanged BVH alone cannot detect new obstacles. setLights(vector<GiMovingLight>) performs source-key stable dirty-region invalidation. invalidate(bounds,reason,radius) supports explicit terrain/foliage/world-change notifications.
- update(budget,camera) refreshes no more than budget probes using the CPU reference, updates only published probe texels after an optional one-time geometry clear, and reports GiRuntimeStats (probe count, rays, upload bytes/regions, backlog/inflight).
- bind(Renderer3D&) calls setDynamicGiOverlay; unbind(Renderer3D&) clears the borrowed pointer. Caller MUST unbind before runtime/renderer destruction or device replacement. shutdown() releases texture and CPU state. No GUI/automatic ticking or gameplay activation is added.

Safety/compatibility:
- The runtime is *opt in* and presentation-side, not authoritative ECS state. Baked/AVGIC005 formats and existing default renderer paths are untouched.
- Geometry change currently clears the WHOLE dynamic atlas once (avoid ghost illumination through destroyed walls); later stages should support sparse invalidation per clipmap brick and incremental zero upload instead of whole atlas.
- CPU reference remains a small-scene correctness fallback, not a mobile-performance solver; no GPU ray tracing compute or SDF geometry acceleration yet.
- Dynamic lighting for shader-graph textures, basic lit meshes, water and TerraWeave terrain still requires dedicated integration. Merely providing invalidate() does not establish an ECS bridge.

Actual verification: Read source, GitHub writes reported success; **NOT RUN**: native build, CTest, shader compile, visual screenshots, hardware timings, ./run-tests.sh. No tests have passed in this execution context.

Next:
1. Pull branch and configure/build all GI tests including gi_dynamic_runtime, run ctest -R '^gi_' and full ./run-tests.sh --quick; compare CPU references to GPU frames on Metal/GLES; fix any warnings or assertion failures. Record results and exact SHA in the next handoff.
2. Create an optional simulation-to-render read-only world-change subscription with stable event types (light old/new bounds, destructible changes, terrain, vegetation). The presentation side calls runtime.invalidate with conservative transport influence and republishes a rebuilt geometry snapshot when blockers change. Do not add genre-specific state to GI.
3. Replace high-cost CPU tracing with bounded GPU compute on supported backends and a robust fallback. Progressive update/clipmap/relocation and material system integration are still needed before claiming production dynamic GI.


## Stage 7 - 2026-11-19 - bounded, genre-independent world-change dirty-region bridge (source committed; NOT VERIFIED)

Implementation commits:
- 42aa7fc2047f9a0c14e440e42c2365135a33692a - engine/render/GiWorldDirtyAccumulator.h
- 077a5e7fbb17a2b3d33cbc5909496c973f27996d - engine/render/GiDynamicRuntime.h, applyWorldChanges accumulator flush
- 81188175152aa8dc1eab49689cd522ef2365a63e - tests/render/gi_world_dirty_accumulator.cpp
- 586b75b57cc536e3a2846b790003e10b6068e292 - CMakeLists.txt, registers gi_world_dirty_accumulator in default ALL and CTest

Public API: GiWorldChange carries a generic GiDirtyReason, current and optional previous world bounds, and conservative transportRadius. GiWorldDirtyAccumulator::submit validates events, expands influence bounds, merges intersecting regions (including transitive overlaps) and retains reason categories. At most 128 independent regions are stored. If the batch exceeds that limit (e.g. thousands of grass changes), it switches to a full-volume dirty mark; no unbounded per-grass memory growth. flush(GiUpdateScheduler&) sorts resulting regions for deterministic invalidation order, marks the right probe cells, clears queued events and reports events, coalesced region count, probe marks and saturation. GiDynamicRuntime::applyWorldChanges(accumulator) is an opt-in convenience bridge; host owns the ECS source and event ordering.

Invariants/limitations: This accumulates visual invalidation events; it DOES NOT update traceable world geometry, create a new BVH, identify occlusion changes, or supply an ECS subscription itself. After digging terrain, opening a door or falling trees, the producer must publish new immutable traceable geometry and revision using setGeometry(), then apply any other affected material/light edits. This currently triggers a full dynamic-atlas clear on geometry revision; later chunked/proxy-BVH updates are necessary for big worlds. Moved objects should submit BOTH previous and current bounds rather than a long connecting AABB. The 128-region overflow degrades conservatively to a whole-volume update, which may take several frames under a bounded per-frame probe budget.

Compatibility: no ECS runtime or save schema changes, no genre-specific logic, no legacy/AVGIC005/cache format changes, no new default renderer quality cost.

Verification ACTUALLY executed: connected GitHub file create/update returned successful commit SHA. **NOT RUN:** compilation, CTest, ./run-tests.sh, GPU captures, mobile timing. New test source covers moved-object old+new bounds, merging categories, saturation under 129 changes and invalid reason/region rejection; it is not a PASS until executed.

Next exact steps:
1. Pull feature/hybrid-dynamic-gi and run:
   cmake -S . -B build/gi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
   cmake --build build/gi --target aiverse_gi_update_scheduler_test aiverse_gi_dynamic_probe_reference_test aiverse_gi_dynamic_probe_texture_test aiverse_gi_lighting_composition_test aiverse_gi_dynamic_sampling_test aiverse_gi_dynamic_runtime_test aiverse_gi_world_dirty_accumulator_test -j8
   ctest --test-dir build/gi --output-on-failure -R '^gi_'
   Run ./run-tests.sh --quick and GPU shader variant checks if possible. Record actual exact tested source SHA and results in another handoff stage.
2. Integrate concrete read-only scene events from TerraWeave/NEREID/vegetation/building systems after their respective commits land in main; avoid guessing proprietary unpublished APIs. Surface changes must rebuild a geometry proxy for visibility.
3. Add partial geometry-region update, probe relocation/classification, GPU tracing on Metal and portable graphics fallback, then real performance and visual acceptance. No work was merged into main.


## Stage 8 - 2026-11-19 - incremental geometry-revision invalidation and sparse GPU clearing (source committed; NOT VERIFIED)

Code commits:
- 7732a17614d0da005e71041d2aebde124bd436a9 - engine/render/GiUpdateScheduler.h: optional affectedIndices output in invalidate()
- d3c101156d898e52971cbca94e92b6fafe457938 - engine/render/GiDynamicProbeReference.h: local geometry snapshot revisions, conservative dirty radii and immediate CPU coefficient clearing
- 1f7d43663170c70151bee209c4aae1c4e74380ef - engine/render/GiDynamicRuntime.h: setGeometryRegion(), pending regional zero uploads, dedup across overlapping revisions
- 8271dce623e21b48da4841dc0da467fba1a14f82 - tests/render/gi_regional_geometry.cpp
- 2c76aa59d4bdd8e2bf5533836810036f98de890b - CMakeLists.txt: GI regional geometry added to default ALL and CTest

API:
1. GiUpdateScheduler::invalidate(bounds, reason, radius, std::vector<uint32_t>* affectedIndices = nullptr) retains existing overload behavior while optionally reporting every invalidated cell.
2. GiDynamicProbeReference::setGeometryRegion(const StaticShadowGeometry* rebuiltImmutableSnapshot, uint64_t revision, GiBounds changed, float influenceRadius = 0, std::vector<uint32_t>* cleared = nullptr) requires **an existing and a new non-null** geometry snapshot, validates the region, then atomically switches the reference to the new scene geometry, resets only affected CPU L1 coefficients and schedules those probe indices. Effective radius is conservatively at least traceDistance + maximum source radius: an edited blocker can change both probe-to-surface transport and the subsequent surface-to-light shadow ray.
3. GiDynamicRuntime::setGeometryRegion(geometry, revision, bounds, radius) forwards the updated immutable BVH snapshot, accumulates zeroed probe indices until the next frame, and returns affected probe count. update(budget, camera) uploads *only those cleared texels* before tracing, even with budget=0; then uploads only successfully retraced probes. Multiple changes before update are deduplicated by GiDynamicProbeTexture::upload. A full setGeometry still performs a full invalidation/zero upload and supersedes pending regional clears.

Compatibility: no changes to baked lightmaps, AVGIC005, shader layouts, ECS save/networking, or existing default GI. Calls are opt-in. Geometry reference snapshots are borrowed; owner must ensure lifetime. Merely bumping a revision without rebuilding the BVH does not make moved geometry traceable; this stage optimizes invalidation, **not** the BVH rebuild itself.

Performance intent and limitations: Local changes avoid full-volume CPU coefficient reset and GPU upload, but the geometry BVH is still rebuilt by the host; CPU reference tracing remains bounded and slow compared with a GPU solver. If traceDistance and light radii cover the whole world, the conservative affected region will still include all probes, which is correct. This is a fixed dense probe volume (not yet clipmaps). Each region is axis-aligned in world metres. Revision changes while old tickets are in flight invalidate them; CPU reference update is currently synchronous single-threaded.

Regressions added (source only): gi_regional_geometry checks GPU texture row preservation for a distant independently lit region, affected count < total, zero upload with no tracing, incremental retracing bounded by budget, coalesced overlapping regional edits, invalid range rejection, and rejecting partial edits without an initial geometry snapshot. It uses always-on AIVERSE_TEST_CHECK.

Validation ACTUALLY performed: GitHub fetch, source edits and successful commit acknowledgements only. **NOT RUN:** compilation, CTest, shaders, device metrics, ./run-tests.sh. No PASS assertions about the regression or Android/Metal performance are justified yet.

Next bounded stage:
1. Verify the exact branch HEAD in a native worktree, compile the now **eight** gi_* targets and run CTest:
   git fetch origin && git switch feature/hybrid-dynamic-gi && git pull --ff-only origin feature/hybrid-dynamic-gi
   cmake -S . -B build/gi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
   cmake --build build/gi --target aiverse_gi_regional_geometry_test -j8
   ctest --test-dir build/gi --output-on-failure -R '^gi_'
   ./run-tests.sh --quick
   Preserve logs and exact tested SHA; fix any compiler/test failures before claiming tested.
2. Add generation-aware geometry chunk sourcing (terrain tiles, vegetation tree proxies, buildings) with a deterministic index and host BVH rebuild/publishing. Only geometry that is actually in a rebuilt BVH affects ray visibility.
3. Add scrolling/clipmap probe volumes for worlds larger than a single dense <=20,000-probe lattice, then a true GPU compute GI backend (Metal first, Vulkan/other backend fallbacks) plus per-frame GPU timing checks.


## Stage 9 - 2026-11-19 - immutable chunked geometry BVH snapshots (source committed; NOT VERIFIED)

Code commits:
- 19ca1b3a4be81a987d84d8aed0e4becc6e46278b - engine/render/GiChunkedGeometry.h
- e979be0dd7192f9f794f646b32caf0bcfea4de14 - tests/render/gi_chunked_geometry.cpp
- 21fd0d3f631566ce74a7261b0abc8a6b2b6d7511 - CMakeLists.txt, gi_chunked_geometry in the standard ALL / CTest suite.

API and architecture:
- GiChunkedGeometryBuilder::replace(uint64_t stableId, vector<StaticShadowGeometry::Triangle> worldTriangles, vector<float3> albedos) builds a NEW StaticShadowGeometry BVH for that chunk only, rejecting nonfinite geometry, invalid surface IDs and unreasonable triangle counts before replacing the previous chunk. remove(id) removes the chunk; publish() builds a lightweight top-level AABB BVH over all active chunks.
- GiChunkedGeometrySnapshot is immutable, owns std::shared_ptr<const StaticShadowGeometry> per chunk, provides distance(origin,direction,limit,normal,albedo,optional QueryStats) with closest-hit semantics identical to the existing StaticShadowGeometry reference, findChunk(id), chunkCount() and triangleCount(). Unchanged chunks retain the same triangle-BVH allocation across published snapshots. Old snapshots remain usable while an updated world snapshot is being published; there is no shared mutable BVH.
- Top-level AABB hierarchy is rebuilt each publish (O(chunk count log chunk count), not an incremental top-level tree). This avoids re-BVHing all triangles on edits, but is not yet a sparse GPU proxy or SDF/voxel cache.

Compatibility: This is a new optional presentation-only module. Existing StaticShadowGeometry, CPU GI, legacy baked lightmaps, AVGIC005, model shaders, ECS saves/network and renderer defaults remain unchanged. The GiDynamicProbeReference/Runtime still accept StaticShadowGeometry, not this new snapshot. **Do not claim chunked geometry is already integrated into the dynamic GI tracer**; that adapter is the next stage.

Test added (source only): gi_chunked_geometry creates two overlapping hit candidates with different albedo, verifies nearest hit, per-chunk BVH pointer sharing after replacement, unchanged old snapshot results, deletion and bounded broad-phase chunk tests with forty irrelevant chunks, plus rejection of nonfinite vertices and negative colors without mutation. It uses always-on AIVERSE_TEST_CHECK.

Validation ACTUALLY executed: source reads + successful GitHub file/commit operations only. **NOT RUN:** native C++ build, CTest, full ./run-tests.sh, GPU shader compilation, hardware timings. No compiler or runtime PASS may be inferred.

Next exact step:
1. Compile/run gi_chunked_geometry + gi_regional_geometry and all other gi_* CTests in native Debug and Release, repair any compile errors or logic failures, and record tested SHA + logs.
2. Introduce a narrowly scoped ray-query adapter so GiDynamicProbeReference can consume GiChunkedGeometrySnapshot without changing the public static-geometry call sites. GiDynamicRuntime must retain snapshot ownership or mandate borrowed snapshot lifetime. Demonstrate a door/wall opening changed only one chunk BVH; unaffected probe texture values remain unchanged while local visibility/radiance updates.
3. Only after regression/performance checks: incremental top-level tree refinements, clipmaps, and GPU tracing/compute with capability checks (Metal first). Keep O(chunks) CPU allocations and mesh ownership out of authoritative ECS simulation.
