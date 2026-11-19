# LumaWeave — experimental massive direct lighting

Branch: feature/lumaweave-massive-lighting. Detailed stage handoff: docs/agent-handoffs/lumaweave-massive-lighting.md.

## Ownership

LumaWeave is DIRECT point/spot/approximated-area lighting and stochastic BVH shadows, NOT indirect GI. Hybrid Dynamic GI owns the separate bounced-light source/atlas, reusing the SAME GiGpuBvh. A renderer must choose EITHER the legacy LightSystem direct-light map OR LumaWeave Metal direct for one view. No effect is automatically enabled.

## Native API

- Stable and transactionally revised source registry: LumaWeaveRegistry, LumaLight
- Actual FX identity: ParticleSystem::collectLightsStable, LumaWeaveParticleBridge; unique host systemNamespace per ParticleSystem
- Multiple FX systems, authored lights, generic effect sources, area emitter samples: LumaWeaveNativeCoordinator::collectScene
- Point/spot candidate culling: LumaWeaveClusters, overflow forces complete source registry scan
- Standalone 1..8 sample Metal direct + shared GiGpuBvh shadows: LumaWeaveMetalDirect
- Depth-reconstructed fullscreen Metal direct + spatial/temporal denoise: LumaWeaveScreenMetal
- Genuine PBR model material receiver (linear base RGB, metallic alpha) and separate depth-matched HDR composition: LumaWeavePostProcess
- Native frame orchestrator (host still owns beginFrame/endFrame): LumaWeaveNativeDriver
- Optional point bounce bridge: LumaWeaveNativeCoordinator::syncGi
- Low-end mobile raster map fallback: LumaWeaveMobileFallback with dropped-source diagnostics
- CPU stress inputs: LumaWeaveStress, aiverse_lumaweave_bench (CPU planning only)

## Example frame order

Create Renderer3D and a working linear HDR PostProcess first. Construct LumaWeaveNativeDriver(device, renderer, existingPost), call initialize() (false on unsupported Metal/mobile is an acceptable fallback). The native game controls its ECS tick and one Device::beginFrame / endFrame. Before LumaWeaveNativeDriver::frame, publish immutable GiGpuBvh geometry with revision, gather authored LumaLight, stable FX system snapshots, and sampled area emitter sources; call ParticleSystem::update after the ECS presentation barrier. Driver.frame(viewInput,screenPass,callback) gathers lights, invokes renderer.beginScene, chooses an exclusive Metal/legacy route, invokes callback(Renderer3D&) so the game submits its opaque/translucent models, then renderer.render and renderer.resolvePostFx. Draw UI afterwards, then device.endFrame. Call luma.gpuPost().pollGpu() on later frames only after Metal beginFrame delivered completed callbacks. Keep Device alive until shutdown() succeeds; shutdown() returns false while GPU work is in flight. The wrapper can be manually used without the driver if the game owns its own frame ordering.

## Correctness limitations

Metal path uses the actual HDR scene depth, reconstructs a geometric normal, then outputs linear DIRECT incident light. The material pass records *actual* PBR baseColor and metallic for OPAQUE model surfaces and a separate depth attachment. Compositor multiplies by diffuse material / pi and rejects receivers whose depth differs from the actual scene depth. Custom terrain, sprites, transparent water and FX geometry have to provide their own material/depth buffer to receive Luma direct; no fake material values are inferred from illuminated scene color. Material normal maps and true direct specular are NOT yet in Luma fullscreen shading. Disk/rectangle/sphere sources are finite point/spot approximations, not analytic soft-area BSDFs. Mobile continues to use old raster source map with explicit lost-source diagnostics. This is NOT an automatically enabled hardware RT/ReSTIR-equivalent Unreal MegaLights replacement.

The GPU producer fences via Metal readBufferAsync after compute and only publishes completed output on a verified GPU callback. No external callback or endFrame call may be mistaken for completion. GPU source/geometry changes while busy are deferred, not silently overwritten. Separate camera/geometry/light revisions invalidate temporal history. Strict work caps shrink low resolution / sample count rather than drop lights on Metal; outside/overflow clusters scan the complete source list. The old LightSystem can still render sun/sky/baked/legacy when Luma is disabled.

## ONE consolidated acceptance pass after code freeze

User explicitly requested implementing all features before running tests. These are commands for a later native Mac Studio run, not observed results:

```sh
git fetch origin
git switch feature/lumaweave-massive-lighting
git pull --ff-only origin feature/lumaweave-massive-lighting
git rev-parse HEAD
cmake -S . -B build/lumaweave-final -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
cmake --build build/lumaweave-final -j8
ctest --test-dir build/lumaweave-final --output-on-failure
./build/lumaweave-final/aiverse_lumaweave_bench 50
./run-tests.sh --quick
```

Additionally validate all MSL shaders on an actual Metal GPU, render moving 100/500/1000 point/spot lights with PBR and scene-depth correctness (including material receiver and denoise), use Metal API Validation, record ACTUAL GPU timings and memory. The CLI benchmark reports CPU timing ONLY and prints GPU time NOT_MEASURED. Never claim it proves GPU FPS. Record tested git SHA, compiler, timing setup, tests/pass-fail logs and hardware in a new handoff before merging main.
