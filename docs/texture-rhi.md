# AIVERSE texture resource unification — implementation stages

Base: `feature/universal-entity-inspector-fps` at `dd711f5e2a7f66ed25bd7330875e6e87b7109e55`.
Implementation branch: `feature/texture-rhi-unification`.

## Status — staged source implementation (not GPU-validated)

Stages 1–4H have source commits on this feature branch. **No native C++ build,
GPU validation or CTest result has been confirmed passing for these changes.**
Do not call the entire texture stack production-ready merely because a C++ method exists.
Read the append-only `docs/agent-handoffs/texture-rhi-2026-11-18.md` for per-stage
SHA, unverified platforms and the next exact task.

- **Dimensions:** Metal/Vulkan have native sampled 1D/2D/3D/2DArray/Cube/CubeArray
  paths; the portable GL33/GLES3 subset has 2D/3D/2DArray/Cube, not 1D/CubeArray.
- **Texture views/samplers:** Metal and Vulkan have subresource views and independent
  samplers; GL has independent samplers but only *whole-resource identity* views.
- **Offscreen passes and MRT:** `RenderTargetPassDesc` selects mips/layers and
  1–4 color attachments, each with its own clear operation, plus optional depth.
  Metal, GL33/GLES3 and Vulkan now have source implementations for same-format
  multi-target rendering. Metal and Vulkan graphics pipelines expose explicit
  color output counts and additional per-slot formats. GLSL/MSL shaders must
  explicitly write the corresponding fragment output locations. Native
  cross-backend GPU validation is still outstanding.
- **MSAA:** the shared descriptor/resolve contract and Metal allocation, pipeline
  sample counts and per-color resolve operations are implemented in source.
  GL/Vulkan still reject explicit MSAA resolves; none of these paths has a
  verified GPU result. Tests in `tests/render/vulkan_tests.cpp` include
  MRT clear/output and nonzero mip/layer rendering, but remain unexecuted.
- **Reusable native owner:** `engine/gfx/RenderTargetSet.h` provides a
  genre-agnostic owner of color/depth targets and optional MSAA resolve targets.
  `begin`/`end` wrap an explicit offscreen pass; allocation failure rolls
  back all created handles. `tests/render/render_target_set.cpp` is a CPU-only
  mock-device lifetime and rollback test registered as `render_target_set`.
  This is separate from the existing Lua `render::RenderTexture` paint canvas.
- **Import:** KTX2/DDS source importers still drop extra array layers and cube
  faces; the new GPU resources are not yet exercised through the asset pipeline.


| Stage | Deliverable | Acceptance |
|---|---|---|
| 1 | Public TextureDimension/TextureDesc/TextureViewDesc/SamplerDesc/TextureUploadDesc, validation and backend fail-closed | Header compiles, descriptor tests pass, existing texture rendering unchanged |
| 2 | Native Metal + GL/GLES + Vulkan 1D/2DArray/3D/Cube/CubeArray allocation, upload per subresource, sampled bind | Real per-backend device tests; shader sampling on every requested dimension |
| 3 | Native views and independent sampler objects with matching bindings | Create/destroy/bind view, anisotropic and compare samplers; preserve existing setTexture |
| 4 | RenderTexture abstraction: layer/mip targets, MRT, depth, MSAA/resolve, storage images | Render pass and readback test for each backend; explicit resource transitions |
| 5 | Additional typed pixel formats (R/RG float/int, BC4/5/6H), full sRGB and HDR/EXR | Format capability checks and cross-platform fidelity tests |
| 6 | Complete KTX2 and DDS array/cubemap/3D payloads, transcoding, import rules | Container roundtrip, bounds/fuzz tests, material/glTF integration |
| 7 | Async texture upload/readback, streaming/budget/residency, optional virtual textures | Resource-lifetime stress tests, no read-after-free, deterministic fallback |
| 8 | Editor resource inspection + feature tests and docs | GPU validation (Metal/Vulkan/GL), mobile/web compatibility gate, README translations |

## Current compatibility constraints

- `gfx::TextureDesc` and `PipelineDesc` new fields are appended with legacy 2D
  and 1x-sample defaults. Existing `PassDesc`, `beginPass`, `setTexture`
  and `render::RenderTexture` remain available.
- A successful `beginRenderTargets` is ended with `endPass()`. It is offscreen
  only. Check its boolean return before issuing draw commands; unsupported
  MRT/resolves must fail explicitly, never downgrade.
- Render-target textures need `renderTarget=true`; color/depth attachments must
  have matching extent and sample counts at the requested mip.
- Metal MSAA pipelines explicitly set `PipelineDesc.sampleCount` to match
  multisample attachments; default 1x pipelines cannot draw into MSAA targets.
- New GPU texture handles are presentation state only, not ECS-authoritative.
  No changes to serialization, game saves or replication.
- **Known missing pieces:** mixed-format MRT and independent blend policies,
  GL/Vulkan MSAA creation/resolve, virtual/streamed textures, expanded formats
  and file import/transcoding, and real-device validation.
- The old `TextureCache` and `render::RenderTexture` still use 2D-only paths.

## Example: selected array layer and mip

```cpp
gfx::TextureDesc tex;
tex.width = 512;
tex.height = 512;
tex.dimension = gfx::TextureDimension::Texture2DArray;
tex.arrayLayers = 4;
tex.mipLevels = 3;
tex.renderTarget = true;
gfx::TextureHandle target = device.createTexture(tex);

gfx::RenderTargetPassDesc pass;
pass.colorCount = 1;
pass.colors[0].texture = target;
pass.colors[0].mipLevel = 1;   // 256x256
pass.colors[0].arrayLayer = 2;
pass.colors[0].clearColor[3] = 1.0f;
if (device.beginRenderTargets(pass)) {
    // Set a compatible pipeline and issue draw calls.
    device.endPass();
}
device.destroyTexture(target);
```

The client must keep the texture alive until all GPU work completes and must
never sample a subresource while simultaneously writing to that subresource.
Use a matching `TextureViewDesc` and independent sampler for post-pass reads
where supported. The code is a usage example, not a claim of verified GPU
execution on this branch.

## Native offscreen render target owner

```cpp
#include "engine/gfx/RenderTargetSet.h"

gfx::RenderTargetSet targets;
gfx::RenderTargetSet::Config cfg;
cfg.width = 1024;
cfg.height = 512;
cfg.colorCount = 2;                  // two same-format color targets
cfg.depth = true;
cfg.mipLevels = 1;
if (targets.init(device, cfg)) {
    if (targets.begin(0, 0, true, true, {0, 0, 0, 1})) {
        // Use a matching PipelineDesc.colorAttachmentCount=2 and a shader
        // writing both fragment color outputs at locations 0 and 1.
        targets.end();
    }
    // Sample via targets.sampledColor(0/1) in later render passes.
    targets.shutdown();  // required before the Device is destroyed
}
```

For MSAA, choose `cfg.sampleCount = 4`; the owner allocates a matching
single-sample resolve target per color. If the backend cannot implement
the selected render pass, `targets.begin` returns false, not a fallback.
Always check `init` and `begin` results. These APIs are under native
verification and should not be assumed to run on every device.

## References and handoff

- `engine/gfx/Gfx.h` — public resource contract and descriptor validation
- `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/metal/GfxMetal.mm`,
  `engine/gfx/vulkan/VkResources.cpp` — fail-closed allocation until backend support
- `tests/render/texture_contract.cpp` — CPU-only descriptor checks
- The append-only stage log is `docs/agent-handoffs/texture-rhi-2026-11-18.md`.

Keep code stage commits separate from handoff commits, record exact SHA and test statuses
in the handoff **after every stage**, and preserve upstream inspector/ECS changes.
