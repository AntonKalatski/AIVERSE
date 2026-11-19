# LumaWeave — Massive Dynamic Direct Lighting: agent handoff

**Branch:** `feature/lumaweave-massive-lighting`, created on 2026-11-19 from `feature/hybrid-dynamic-gi` (source HEAD `9819baa6b2a480ba183c981a0fe584f3c22c0721`). All work stays here, not on main.
**Ownership:** genre-neutral real-time DIRECT point/spot lighting, high source count, shadow sampling and future temporal denoise. Existing `LightSystem` is the default legacy direct-light path; Hybrid Dynamic GI owns bounced INDIRECT lighting and GPU ray-query geometry. Never count direct or baked radiance twice.
**Mandatory handoff:** after each implementation stage: source commit(s), then a SEPARATE handoff commit. Preserve all entries, list actual tests vs NOT RUN and exact commits.

## Stage 1 — 2026-11-19 — source registry, legacy adapter and anti-double-counting contract

Source commits:
- `fc13974d9235687f6cd7a8aa7e0791cc73b1e5b6` — `engine/render/LumaWeaveLights.h`
- `65e5c84fdb1b1b6d9c71279e1e7c80f40e983435` and `4b71d81146b89e6e84aca85883d7dd15463c1de8` — `tests/render/lumaweave_registry.cpp`
- `68435eae281376158b38477e546f80c3b7523e1b` — `engine/render/LumaWeaveLegacyBridge.h`
- `5564f443a267e7b0839ec215a337cfe5d11fea24` — `CMakeLists.txt` register `lumaweave_registry` target in default AIVERSE_BUILD_TESTS / CTest
- `64465995b66454c95cadd7c230a618e8d98aca02` — `AGENTS.md` ownership and handoff rules.

Public API:
- `LumaLight`: stable **nonzero** 64-bit key (not frame index), kind `Point` or `Spot`, world position, normalized direction for spot, linear RGB, finite radius/intensity, cosine cone, importance bias, castsShadows, contributesToGi. `LumaLightBounds` is conservative radius AABB, even for spot. Importance bias changes only sampling probabilities, never light brightness.
- `LumaWeaveRegistry::publish(vector<LumaLight>)` validates all inputs and duplicate keys BEFORE modifying the existing sorted immutable snapshot. Max 4096 sources. Returns `LumaPublishResult` with stable-key `LumaLightEdit` records of added, removed, moved and edited sources. Both old and new bounds are retained for dirty GI and shadow masks. Revision only changes on a real edit.
- `lumaFromLegacy(PointLight,key,contributesToGi)` adapts pre-existing legacy lights only with a caller-provided stable key. Never auto-register a frame-index (legacy key 0 does not qualify). Preserves cone and overbright color.
- `lumaGiPointSources(registry,maxSources<=64)` explicitly exports only marked GI-enabled POINT lights to the current GiDynamicProbeReference. Throws for marked spot/overflow rather than silently replacing a cone with omni light. Baked static contributors must have contributesToGi=false or be separately removed by caller.

Compatibility and limitations:
- No runtime shader, LightSystem, postprocessing, ICB, lightmap or ECS mutation. All defaults remain exactly the same. No engine ever uses LumaWeave automatically; it is only a new header-only explicit host API.
- Registry capacity is 4096, but the old LightSystem still handles at most 1024 and GI CPU reference 64. The adapters do not magically expand those paths. Producer must choose whether LumaWeave direct or legacy LightSystem direct is authoritative FOR A GIVEN DRAW; do not run both.
- No area lights, GPU clusters, stochastic samples, reservoir, shadows or denoiser yet. This step just establishes stable coherent source identity and safe change notifications.

Tests actually performed: inspected current GitHub source (`Lights.h/.cpp`, `LightShaders.h`, `GiDynamicProbeReference.h`, `GiGpuBvh.h`, `Gfx.h`) and source create/update commits reported success.
**NOT RUN**: native C++ compilation, CTest, Clang/GCC Release/Debug, GLSL/MSL, GPU rendering, benchmarks and full `./run-tests.sh`. Repository worktree is not locally available in this session. The registered Release-safe test source exercises ordering, key stability, invalid update rollback, old/new moved bounds, legacy bridge, GI spot rejection. It MUST NOT be reported as passing without execution.

Next bounded stage: implement a bounded, deterministic 3D light-cluster index that uses **all** registered lights, conservative sphere-vs-AABB culling and explicit per-cell overflow markers. Never silently discard lights if a cluster exceeds its fast-list cap: stochastic direct lighting must use a correct slow fallback over all potentially relevant sources. Add CTest, then a separate Stage 2 handoff. After this add importance sampling / CPU direct + shadow reference using shared GiGpuBvh rather than new ray acceleration. Only after real tests integrate any shader to renderer. 

Native acceptance commands:
```sh
git fetch origin
git switch feature/lumaweave-massive-lighting
git pull --ff-only origin feature/lumaweave-massive-lighting
cmake -S . -B build/lumaweave -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
cmake --build build/lumaweave --target aiverse_lumaweave_registry_test -j8
ctest --test-dir build/lumaweave --output-on-failure -R '^lumaweave_'
```

## Stage 2 — 2026-11-19 — bounded deterministic world-space clusters (SOURCE; NOT RUN)

Source commits:
- 95d38e9f88dba6aa603fcd568db0fbf7c2182364, 503f58be4a15d14fd867fcdc46c6f3a933c24f4d — engine/render/LumaWeaveClusters.h
- 18c0dadc669ebe7b96ebcd506d23efe7604879a3 — tests/render/lumaweave_clusters.cpp
- 8872e868ba7cd6abef6feab1a963b0df41b7e4b1 — CMakeLists.txt, adds lumaweave_clusters target/test to default AIVERSE_BUILD_TESTS.

API: LumaClusterConfig defines world-space origin, per-axis cellSize, nx/ny/nz, maxFastPerCell<=64, maxFastReferences<=1,048,576, max cells 16384. LumaWeaveClusters::rebuild(const LumaWeaveRegistry&,config) transactionally validates params, performs conservative sphere-vs-AABB filtering for point/spot radius against only spatially relevant cells, and stores deterministic sorted source indices in a compressed row storage (CSR) array. BuildStats includes clusters, sources, occupied, overflow count, retained fast references, number of spatial tests, registry revision. query(worldPosition, registry) checks revision, returns span of light indices plus potentialCount and requiresFullScan flag. When a cell exceeds per-cell OR global fast-reference budget, requiresFullScan=true; the next importance sampler MUST use the FULL sorted source registry and exact receiver tests in that case. Outside the indexed volume also triggers full fallback. Missing lights cannot be silently discarded because fast list is capped. Empty spans do not perform null-pointer arithmetic.

Source contract: Registry revision is checked per query. The cluster index does not mutate ECS nor legacy LightSystem and introduces no data texture, shaders or default GPU cost. 3D world-space clusters are a portable CPU reference; eventual forward+/deferred production culling should map screen-space depth slices and material coverage, and support compact GPU workgroups; do not call this pixel-tiled or a GPU implementation.

Tests written but NOT RUN: `lumaweave_clusters` verifies disjoint separated lights, positive/negative positions, outside fallback, 32 lights in one cell with fast cap two and explicit overflow metadata, source revision rejection, invalid configuration and preserving prior valid index on failure. Uses AIVERSE_TEST_CHECK (Release-safe).

ACTUAL verification: source read/create/update commits and CMake test registration through GitHub; no native filesystem repo, thus no C++ compilation, CTest, GPU timing/visualization, Metal compiler, or run-tests.sh has been executed. DO NOT report a PASS.

Next bounded stage: implement deterministic receiver importance sampling for POINT and SPOT sources from either cluster fast span or the exact full-registry slow path, and an unbiased (conditional on candidate set) Monte Carlo direct-light estimator with a FIXED number of shadow rays. Reuse GiGpuBvh::trace for shadow queries — do not duplicate a geometry intersection acceleration system. Keep direct light separate from GiDynamicProbeReference bounced light and existing LightSystem. Add a CPU test comparing mean sampled direct radiance to exhaustive unshadowed/shadowed reference and showing no energy loss on overflow; then another handoff commit. Later hardware GPU passes and temporal/spatial denoising remain absent.

## Stage 3 — 2026-11-19 — bounded importance-sampled direct and shared-BVH shadow CPU reference (SOURCE; NOT RUN)

Source commits:
- 9a4a7f6b4d52cdcbee3a50a9f6c97565d32eabf2 — engine/render/LumaWeaveDirectReference.h
- a1501ae1f54d5b92539151a2790b541aacf2e8e9 — tests/render/lumaweave_direct_reference.cpp
- f378262e85a2d9a9eb3d420cbb3dea5115856155 — CMakeLists.txt, test lumaweave_direct_reference in default CTest.

Public API: LumaDirectQuery{worldPosition,worldNormal,randomSeed}, LumaDirectResult{linearRGBdirect,LumaDirectStats}; LumaWeaveDirectReference::exact(registry,receiver,optional GiGpuBvh,optional clusters) builds a full unshadowed direct-light candidate list, traces ALL selected shadow rays, and returns a correctness reference. estimate(registry,query,1..64 fixed sampleBudget,optional shadowBvh,optional clusters) draws bounded with-replacement importance samples via deterministic integer SplitMix64 RNG. Probability p_i = positive luminance(unshadowed Lambert/point-spot attenuation)*importanceBias / totalWeight; estimator accumulates contribution_i * visibility_i / (p_i * sampleBudget). Therefore it is unbiased for candidate set assuming input lights and visibility hold fixed. Each chosen candidate casts at most 1 BVH shadow ray, for at most sampleBudget rays. Point/spot distance and cone factors follow the new registry; no static sun, ambient, GI, baked, history or indirect light appears in this path.

Reuse: shadow visibility uses **GiGpuBvh::trace** from feature/hybrid-dynamic-gi. Do NOT copy GI triangle/BVH code into LumaWeave. Both systems may use the same immutable geometry snapshot and source IDs, but execute independently budgeted direct vs indirect rays. On cluster overflow or receiver outside grid, importance candidate generation scans ALL LumaWeaveRegistry lights before exact per-receiver tests; finite list truncation does not silently lose valid sources. On valid clusters, only their exact sphere-AABB conservative candidates are considered. A source marked castsShadows=false does not consume a shadow ray.

Tests authored but NOT RUN: lumaweave_direct_reference creates 32 equal lights in an intentionally overflowing 2-entry cluster and verifies 1-sample importance equals exhaustive direct radiance; then alters sampling bias and checks 8192 deterministic seeds converge in mean to exhaustive direct; tests spot cone front/back and shared GI GPU-BVH wall occlusion with exact and 8-sample bounded shadow rays; rejects zero/>64 budget. All use always-on AIVERSE_TEST_CHECK.

Compatibility: Header-only portable CPU reference, no new renderer pass or default light switch. **NOT** actual MegaLights/ReSTIR: no GPU screen-space tiles, hardware ray queries, spatio-temporal reservoirs, denoiser, area lights, visibility buffer, motion-vector reprojection or frame performance tests. The 'importance' score changes only sampling variance; it does NOT alter energy and is not a baked GI replacement. High-energy sources and rapid movement may still produce noise until temporal filtering is implemented.

ACTUAL validation: GitHub source commits and CMake registration. No native clone mounted for this session, so native Clang/Debug/Release compilation, CTest, Metal shader compile, real shadow rays on GPU, performance, and ./run-tests.sh are all NOT RUN. Do not declare sample convergence passed unless test actually runs.

Next bounded stage: implement a controlled **opt-in** GPU lighting producer using current gfx::Device Compute (Metal only initially) or a renderer-independent dispatch/forward pass with texture budgets, preserving the direct-vs-indirect ownership rule. First inspect how to access world positions/normals/depth and motion vectors in the existing HDR/postprocessing chain; do NOT silently invent GBuffer fields or enable LumaWeave beside legacy LightSystem. Wire explicit quality caps and a baseline report. For mobile, keep traditional clustered/raster fallback and do not promise 4096 shadowed lights in one pass without profiling. After code commit append its own handoff. Before integration, run all lumaweave_* tests plus gi_* tests and ./run-tests.sh --quick; record native log and exact SHA.

## Stage 4 — 2026-11-19 — packed GPU source/cluster ABI and stable identity (SOURCE; NOT RUN)

Code commits:
- eee1155a3a04c5940597693c58986d1139d2d4e9 — engine/render/LumaWeaveClusters.h adds exportCsr() with GPU cluster fields: 16-byte {first,count,total,flags}, flags bit0=overflow/full-scan required.
- 5aefe18a0ccc3355c08eb539c96cb1af90d58ec5, 70b407e6d5c1e1b1388c3e9643a4a0a1e97381d9 — engine/render/LumaWeaveGpuPacket.h, structured-buffer packet and stable 64-bit source key preservation.
- 071a8676140d6935c3fff33374654c17128f1ca0 — tests/render/lumaweave_gpu_packet.cpp.
- f27eac09895e5beb544f87ee71333914749d9864 — CMakeLists.txt, CTest lumaweave_gpu_packet.

Architecture/ABI: LumaGpuLight is a packed 80-byte record with position+radius, linear color+intensity, spot direction+cosOuter (point sentinel -2), cosInner/importanceBias/shadow flag and uint4 identity (stable key low/high, kind and explicitly authorized indirect GI flag). LumaGpuReceiver is 32 bytes (world position+validity and normal). LumaGpuClusterEntry is 16 bytes: CSR first,count,total,overflow flags. LumaGpuGridUniforms is 64 bytes: float4 origin, float4 cellSize, uvec4 dimensions and uvec4 limits. LumaGpuInputPacket::packLumaSources(registry,clusters) requires the same source revision and validates all CSR indices. The packed snapshot carries no gfx::Device pointers and is immutable for uploads. Counts and byte lengths are explicit, so GPU code can fail closed instead of sampling truncated or stale light lists.

Critical correctness: A cluster with flags bit0 set is not a smaller substitute for all sources; the GPU shader MUST scan the entire registered source buffer for that receiver to avoid losing direct light energy. Outside grid is also an all-sources fallback. Before/after light revisions require a new cluster packet. Importance bias is a sampling PDF factor, NOT brightness. The GPU array contains stable source identity so temporal reservoirs can invalidate on source changes later. New data is DIRECT-only even though a source also advertises explicit independent bounced-GI eligibility.

Tests authored but NOT RUN: lumaweave_gpu_packet checks ABI sizes, input ordering, stable 64-bit key retention, Spot flags, 1/2 CSR overflow, byte counts and revision rejection/rebuild. The previous lumaweave_registry/clusters/direct_reference tests remain registered. No native build/test execution was performed.

ACTUAL verification: GitHub file read/update commits and CMake registration acknowledged success. Native compiler/CTest, GPU compute, Metal shader validation and full ./run-tests.sh: NOT RUN. Do not present this packet as GPU rendering/lighting production code yet.

Next: Build an opt-in Metal Compute renderer for externally supplied receiver positions/normals using this packet, 1..8 importance-selected shadow rays per receiver, shared GiGpuBvh node/triangle ABI and a separate linear RGBA16F output. No implicit GBuffer currently exists; do not infer one from the RHI. Explicitly gate real-world pixel counts and total ray budgets; this is a correctness prototype prior to eventual screen-space depth/normal/velocity resolve. Add FakeDevice ABI/submit regression and actual Metal shader compile/readback later. Keep legacy LightSystem default until visual/perf acceptance.

## Stage 5 — 2026-11-19 — experimental Metal GPU direct-light producer with shared ray-query shader (SOURCE; NOT RUN)

Implementation commits:
- b507dd8c8385259555011896cf662da6b1049eb5 — engine/render/GiGpuBvhMetalShader.h, extracts the single shared Metal BVH triangle/node/ray-query code.
- 47f1756010de7b95621dde86c8836360b1ec9baf — engine/render/GiGpuSmallSceneShaders.h now injects the SAME BVH Metal shader literal. GI shader interfaces, tri/node ABI, traceBvh name and output remain as before; this is a refactor of source assembly, not tested numerical parity.
- 414cf701005a401a18fb960a822d429767519fb4 — engine/render/LumaWeaveMetalShader.h, LumaWeave Metal cs_main shader.
- 27fc7ec3a4b2c3c7a44ec3a18464992014b244d9, 7c21f1dc0ac682de2cea527f0ed569564840dd68 — engine/render/LumaWeaveMetalDirect.h, standalone opt-in GPU submission/fence, input validation and transactional source publication.
- 21166e39f4b347ada4aa21c6f6222e31982b1421 — tests/render/lumaweave_metal_direct.cpp.
- bd5d0248023b817019f059bfdc743357f6e8b3dc — CMakeLists.txt, lumaweave_metal_direct registered in default ALL/CTest.

Metal shader: Includes the exact shared GI GPU BVH ray query as a raw MSL fragment. Uses compute read-only structured buffers: slot0 packed LumaGpuLight (80 bytes), slot1 explicit LumaGpuReceiver jobs (32 bytes), slot2 16-byte cluster CSR cell, slot3 uint candidate indices, slot4 shared GiGpuBvhTri (64 bytes), slot5 shared GiGpuBvhNode (48 bytes). Uniforms in compute buffer(8): LumaGpuGridUniforms (64 bytes) plus uint4 control (16 bytes) = 80-byte ABI. Output is an independent RGBA16F storage texture on compute texture0; kernel has one thread per supplied receiver (position + normal). Candidate selection uses finite sphere/spot source testing, luminance times importanceBias probability, Xorshift32 per frame/receiver, at most 1 shadow BVH trace per sample, accumulates direct-only estimator. Per-cluster overflow or out-of-volume receiver falls back to the full 4096-source registry; never truncate without explicit fail-closed work-budget rejection. There is no indirect contribution, baked map, sun sky, postprocess, temporal filtering, or default renderer integration. The GPU RNG is intentionally not bit-identical to the SplitMix64 CPU reference; validate statistical parity on real devices.

Host API LumaWeaveMetalDirect::initialize(Device&) succeeds ONLY for Metal with compute and RGBA16F; explicit setSources(LumaGpuInputPacket) and setGeometry(GiGpuBvh,revision) validate and upload inputs outside render/compute passes with transaction-safe allocation; setSources validates consistent config/source counts/CSR index ranges. dispatch(span<const LumaGpuReceiver> receivers, samples in 1..8, frameSeed) accepts 1..256 receivers, total at most 2048 nominal shadow rays and 524288 worst-case candidate evaluations. It refuses excessive work instead of dropping lights. DispatchResult reports source/geometry revisions, actual receiver/sample counts and upper bounds, NOT real GPU time. Metal readBufferAsync() called AFTER the compute dispatch enqueues a marker blit in the SAME command buffer; only a successful completed callback makes completedOutput() and completedReceiverCount() valid. If async readback is unavailable, a separate genuine GPU completion signal must be passed through confirmExternalCompletion(serial,success); endFrame/dispatch is not completion. shutdown() refuses while a job is pending. The host must retain Device and this runtime until the callback arrives, and never bind output after sources or geometry have changed until a fresh completion. Renderer3D is not modified or automatically enabled.

Tests authored but NOT RUN: lumaweave_metal_direct uses FakeDevice Metal backend to inspect 6 bound storage buffers, 80-byte params, 1..8 sample budget, candidate work guards, backend rejection, unready output before callback, geometry/source swaps refused while pending, GPU-error failure callback, explicit external confirmation when async readback rejected, and resource cleanup. Shader-source string checks are NOT an MSL compiler. Since shared GiGpuBvhMetalShader.h was extracted from the previously used GI compute kernel, also rerun ALL gi_* tests and compare GI metal shader compilation and output to the old branch before integration.

Warnings/limitations: This is a bounded receiver-buffer GPU prototype, NOT a full-screen deferred/forward+ MegaLights renderer. There is no existing normal/position/motion GBuffer automatically feeding this compute path, no on-screen resolve/composition, no temporal/spatial reservoir or denoiser, no area lights, no GLES/Vulkan compute path, no production mobile FPS claim. No double light contribution: producer must choose legacy LightSystem direct OR LumaWeave direct for each view, not both. The new API is NOT silently replacing LightSystem. A source may explicitly be exported for separate GI bounce, but the compute output is direct only. Compute output cannot be used until a confirmed RHI command-buffer fence. Host must complete pending work before object/device destruction, consistent with GI runtime contract.

ACTUAL validation: GitHub commit success, inspection of current gfx::Device, Metal readBufferAsync and existing GI compute BVH source. Native CMake/C++ compilation, CTest, Metal shader compilation, GPU readback/imagery and frame timing, GL/GLES and full ./run-tests.sh: NOT RUN. Do not claim compiled or production-ready. CI status was not confirmed.

Next exact step: Pull branch, build the five lumaweave_* test targets in Debug/Release and all gi_* tests, run ctest --test-dir build/lumaweave --output-on-failure -R '^(lumaweave_|gi_)' and ./run-tests.sh --quick, compile both Metal shaders with real Xcode/Apple GPU and compare pixel radiance/visibility with CPU reference. Repair any source/ABI errors and add a separate verification handoff with exact tested SHA. Then introduce an explicit HDR geometry/normal/velocity producer (not assumed GBuffer) and a compositing pass that selects LumaWeave direct INSTEAD OF the legacy LightSystem direct path. Add shadow history with per-light IDs, disocclusion/normal-depth rejection, variance clamping and spatial resolve; gate quality tiers, light/triangle/work budgets, and mobile fallback. Do not merge main or claim Unreal MegaLights parity before profiling.
