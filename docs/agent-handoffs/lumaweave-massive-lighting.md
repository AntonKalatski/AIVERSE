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
