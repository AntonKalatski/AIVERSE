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
