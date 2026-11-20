# Dynamic indirect GI

The lighting branch provides an explicit, genre-independent, one-bounce diffuse
probe overlay. It composes with existing direct lighting. It does not implicitly
enable itself, replace direct-light ownership, or write simulation/ECS state.

## Native integration

Use `engine/render/GiMainCpuBridge.h` for budgeted CPU tracing and
`engine/render/GiMainGpuBridge.h` for bounded Metal/Vulkan compute. Both consume
immutable `GiChunkedGeometrySnapshot`, stable geometry revisions, linear RGB
`GiMovingLight` sources and optional indirect-only `GiEnvironmentLight`.
`apps/renderbench/DynamicGiHost.h` is a working native host example.

1. Before CPU mesh arrays are released, build immutable opaque caster chunks with
   material diffuse albedo. The benchmark uses bind-pose opaque static geometry;
   animated/deforming casters require newly published snapshots in a game host.
2. Initialize a fixed probe lattice. GPU supports up to 8192 triangles, 16 lights,
   32 probes per dispatch and 8–64 rays per probe. CPU supports 8–128 rays and uses
   an adaptive probe budget. Lattices are capped at 20000 probes. A CPU budget is
   a scheduling target, not a guaranteed frame-time bound for an individual ray.
3. On the render owner thread, publish geometry/light/environment snapshots and
   update before graphics passes. GPU publication requires an active Device frame.
   Retry deferred publications after prior compute **and graphics reader** fences
   complete. Rejected source data must not be presented as current data.
4. Call `renderer.beginScene(...)`, then `renderer.setDynamicGiOverlay(bridge)`
   for every view. `beginScene` clears previous attachment. The bridge must outlive
   all submitted draws. Textured Shader Graph receivers reserve tex13 for GI and
   move graph samplers to tex14+. GPU-instance/ICB uses ordinary draws while GI
   is attached. PBR receivers support High/Low and multiply GI by material AO.
5. Terrain/custom pipelines bind after selecting their shader: built-in full
   terrain uses U6/tex21; compact tier0 terrain uses tex8 only without baked,
   detail or trace features. Generic custom/model samplers use tex13.
6. Detach receivers, establish real GPU idle, then call `shutdownAfterGpuIdle()`
   before destroying the Device. Never substitute CPU frame age for completion.

GPU scheduler completion is fence-backed. `computeDispatchSucceeded()` means
commands were encoded, not that the GPU completed them. Partial checked uploads
keep the result hidden and force coherent geometry recovery. Source rejection
states are independent; updating the environment cannot restore rejected lights.

## Renderbench recipe

Add an opt-in `dynamicGI` object to a native renderbench scene:

```json
"dynamicGI": {
  "backend": "auto",
  "origin": [-12, 0, -6],
  "dimensions": [8, 4, 4],
  "spacing": 3,
  "samples": 32,
  "distance": 20,
  "cpuBudgetMs": 2,
  "sunDirection": [0.35, 1, 0.55],
  "sunColor": [1, 0.96, 0.9],
  "sunIntensity": 3,
  "skyIntensity": 0
}
```

`auto` uses compute only when device and scene limits allow it, otherwise retaining
all captured geometry on CPU. `gpu` fails explicitly if unsupported; `cpu` forces
the portable path. Sources are not silently truncated. Recipe point lights are
converted from the renderer's stored sRGB representation back to linear transport.
Environment colors above are already linear and are indirect-only; no implicit
copy of direct sun/sky settings occurs. Large geometry is split into 65536-triangle
chunks. The benchmark waits for initial convergence before timed frames and reports
`dynamicGiBackend`, `dynamicGiFallback`, `dynamicGiTriangles` and
`dynamicGiConvergenceMs`. It reattaches GI on every camera view.

The benchmark rejects simultaneous `probeGI`/`staticPointBounce` because those
recipes reuse the same sources. Its screen-space diffuse bounce is disabled while
dynamicGI is active. Direct-light rendering and post-processing remain independent.
This benchmark host is a fixed scene demonstration, not an editor persistence/UI
migration or automatic animated-caster tracking.

## Verification and limits

All builds and compiler TMPDIR for this work live on KINGSTON, under
`/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120`.
Run registered checks with `./run-tests.sh --no-build --build-dir <root>/build
--filter '^lighting_'` after building the selected targets. Enable
`AIVERSE_BUILD_HDR_TESTS`, `AIVERSE_BUILD_RENDERBENCH`, `AIVERSE_BUILD_TESTS`;
`AIVERSE_VULKAN` additionally builds real SPIR-V compilation tests.

Actual Metal/desktop GL pixel tests cover model and terrain receivers, per-view
reset, light removal/restoration and CPU/compute parity. Native host acceptance
loads real glTF, verifies JSON completion and compares captured RGB, so native
window exit code zero cannot hide a fixture failure. Vulkan backend and GI SPIR-V
compile locally; Vulkan GPU execution is not verified on this Mac (no loader).
Compact shader testing on desktop GL is not a GLES device performance claim.

This is bounded first-bounce L1 interpolation, not multi-bounce path tracing,
reflection GI, infinite-world streaming, probe relocation or a proof against
all thin-wall light leaks. Performance and mobile/Vulkan hardware acceptance are
separate gates; no production quality defaults were changed.
