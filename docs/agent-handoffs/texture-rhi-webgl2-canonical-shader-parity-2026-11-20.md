# AIVERSE Texture RHI — WebGL2 canonical shader sampling oracle
Date: 2026-11-20

## Branch and actual source changes
Branch: `feature/texture-rhi-unification` (main unchanged).
- `tests/render/webgl2_vt_integration.cpp` — `7078411aa9f503301674bd78305eed75ab2faf90`.
- `tests/render/webgl2_context_recovery_browser.mjs` — `f06c2a54a9a1cb962fe46c08726ac361a55dbbb0`.
- `engine/gfx/gl/GfxGL.cpp` — `372a8cf659bc35b55e51183f955201d66b196b2a`.

## Implementation and architecture
The existing standalone Emscripten test uses production `gfx::VirtualTextureStreamer`, two physical atlas slices, an indexed page table and a pinned coarse root. After LRU replacement and exact GPU readback of atlas/table, it now renders a 4x4 RGBA8 offscreen texture through **production `gfx::Device` graphics pipeline** and the actual canonical shader helper `gfx::kVirtualTextureGLSL`.

A full-screen triangle (gl_VertexID) and fixed fragment UV/LOD columns assert:
- first column uses a virtual tile whose fine page was evicted and must resolve pinned root RGB (28,75,98)
- second column samples resident replacement tile RGB (11,205,47)
- third column samples the same replacement at fractional virtual mip LOD=0.5 and should blend with the coarse fallback RGB around (20,140,73)
- fourth column uses invalid logical layer and must return explicit fallback RGB (9,12,18)

Independent authored RGBA expected values are compared against all 16 offscreen pixels with ≤1 8-bit channel tolerance. This tests trilinear LOD transition and basic page mapping, not nonuniform bilinear seam interpolation. The opt-in Emscripten RHI `readTextureAsync` path now also accepts a 2D RGBA8 render target and attaches it via `glFramebufferTexture2D` for this oracle, while keeping the existing array layer readback. Unlike native backends, this path performs **synchronous** `glReadPixels` and queues the callback for next beginFrame; it exists only with `AIVERSE_WEBGL_CONTEXT_ACCEPTANCE=ON`.

The production VT fixture now exports `_aiverse_web_vt_shader_verified()` and only advances to completion stage 5 after the offscreen oracle succeeds. Playwright `--vt` requires stage=5, three physical atlas/table reads, shader_verified=1 in the initial generation and after each of two simulated WEBGL_lose_context recoveries.

## Validation actually performed
Source inspections and GitHub code edits only. **No C++/Emscripten build, WebGL2 browser execution, shader compiler, GPU readback, Metal/Vulkan/GLES acceptance or CTest run has been performed for these commits.** The results must not be described as a passing shader oracle before hardware/software WebGL driver execution.

## Known limitations / follow-up
- Validate the GLSL ES 3.00 compilation of the production helper embedded into the RHI shader, the full-screen triangle, 2D RT readback, output alpha and exact color/LOD GPU differences on a real browser. This is not yet proven.
- Add nonuniform authored texture data and explicit bilinear seams, Repeat/MirrorRepeat address modes, several fractional LOD values, and actual rendered shader samples during eviction/replacement overlap.
- Current old Device/context-lost destruction path needs explicit lifecycle verification in a browser; GPU resources recreated by app callbacks may not include all user-owned texture caches.
- Run the Emscripten target `aiverse_webgl_vt_acceptance` with `-DAIVERSE_WEBGL_CONTEXT_ACCEPTANCE=ON`; serve its resulting HTML and invoke `node tests/render/webgl2_context_recovery_browser.mjs http://localhost:8000/webgl-vt-acceptance.html --vt`. A real nonzero failure is required if prerequisites or GPU oracle fail.
- Complete native GLES3 opt-in, Vulkan and Metal GPU tests, 2048-cycle mock streaming stress, full-engine Mac Studio acceptance and exact SHA manifest. PR #21 stays Draft, experimental GPU ICB stays default OFF.

Each stage must record architecture/API, files, branch and commits, tests truly executed/results, limits and next step in a separate handoff.
