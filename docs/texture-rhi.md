# AIVERSE texture resource unification — implementation stages

Base: `feature/universal-entity-inspector-fps` at `dd711f5e2a7f66ed25bd7330875e6e87b7109e55`.
Implementation branch: `feature/texture-rhi-unification`.

## Status

Stage 1 defines descriptor contracts and ensures existing backends **reject** unsupported
dimensions rather than silently allocating Texture2D. The new view, sampler and subresource
methods are intentionally unsupported defaults until the corresponding backend lands.
Do not advertise them as functional yet. Existing Texture2D clients retain legacy defaults.

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

- `gfx::TextureDesc` extended fields are appended; `Texture2D` remains the default.
- `gfx::Device` new methods are virtual with safe invalid/false defaults and need backend overrides.
- No change to serialization, game save data or ECS simulation.
- Texture arrays/cube/3D/MSAA are not yet available at runtime.
- The old `TextureCache` and `render::RenderTexture` still use 2D pathways.

## References and handoff

- `engine/gfx/Gfx.h` — public resource contract and descriptor validation
- `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/metal/GfxMetal.mm`,
  `engine/gfx/vulkan/VkResources.cpp` — fail-closed allocation until backend support
- `tests/render/texture_contract.cpp` — CPU-only descriptor checks
- The append-only stage log is `docs/agent-handoffs/texture-rhi-2026-11-18.md`.

Keep code stage commits separate from handoff commits, record exact SHA and test statuses
in the handoff **after every stage**, and preserve upstream inspector/ECS changes.
