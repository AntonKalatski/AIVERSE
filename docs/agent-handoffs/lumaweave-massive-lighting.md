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
