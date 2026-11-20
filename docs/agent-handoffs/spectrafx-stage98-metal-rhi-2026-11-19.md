# SpectraFX Stage 98 — real Metal RHI offscreen views and 3-frame GPU retirement

Date: 2026-11-19
Branch: `feature/spectrafx-generic-cue-queue`
Code commits:
- `59605eea446f0c26533ea839b6074b7729d3d73f` — real Metal RHI offscreen multi-view test, diagnostics, CMake and CI.
- `96954451dd1bd317079098bfe2ca02b823fa89ed` — remove incorrect standalone CommonCrypto framework link; the existing runtime uses system linkage.
- `fe530ceb33fd0edcb1c7c2b52113c838178bbce8` — build isolated Objective-C++ RHI test using ARC.
Parent validated SHA (Stage 97): `4e363db3a6fc0d1631a574165febb7d26e08c3b8` (user Apple M3 Ultra **8/8 acceptance steps PASS**, including native Metal 2/2; **these results do not verify Stage 98**).

## Scope and implementation

1. `engine/gfx/Gfx.h` adds an optional `gfx::Device::readColorRGBA8(TextureHandle, std::vector<uint8_t>&)` diagnostic API (default returns false). The method reads *completed* RGBA8 render-target output to tightly packed, top-down RGBA bytes. It is explicitly synchronous and forbidden during active frames, for tests/debug tools only. **Normal SpectraFX rendering remains entirely GPU-resident**.
2. `engine/gfx/metal/GfxMetal.mm` implements this API via a temporary Metal Shared staging buffer, 256-byte-aligned row stride, `MTLBlitCommandEncoder`, command-buffer completion check and BGRA→RGBA conversion. It refuses readback when command/compute/render encoding is active. Other backends inherit a non-disruptive unsupported stub. Existing real-time drawing and resource ownership remain unchanged.
3. `tests/spectrafx_metal_rhi.mm`: a headless Objective-C++ fixture using the real production `GfxMetal.mm` and `gfx::createMetalDevice`, **not direct raw-Metal rendering**. A tiny stub supplies only the platform content-reader used by Metal shader archive discovery; the RHI, GPU evolution and GPU billboard rendering are actual engine code.
   - Creates two independent RGBA8 offscreen color targets plus a depth target.
   - Compiles the actual production `SpectraFxGpuEvolution` compute program and `SpectraFxGpuBillboardRenderer` RHI shader/pipelines.
   - Encodes compute before two RHI render passes in the same frame, with two `SpectraFxView` camera matrices/orientations (second camera rolled 90°), and reads both offscreen outputs.
   - Asserts first view's particle appears left only, second view appears right only, and untouched corners remain clear; verifies readback is denied during an active frame.
   - Repeats 24 allocation/compute/draw frames; old GPU storage is kept alive for at least 3 future `frameSerial()` values and released only at a begin-frame safe point. Drains the pending retire queue after 4 final frames and checks `gpuHealthy()`.
   - Returns 77 (CTest SKIPPED, not PASS) if no Apple Metal device or supported compute.
4. `CMakeLists.txt`: new macOS target `aiverse_spectrafx_metal_rhi_test` and CTest `spectrafx_metal_rhi`, labels `SpectraFX;MetalGPU;RHI`. It links a standalone copy of production `GfxMetal.mm` without importing the `PlatformMac.mm` application entry point. Uses ARC and the existing native framework imports.
5. `tools/spectrafx/acceptance.sh` now builds the RHI target in the SpectraFX core acceptance group before all CTests. `.github/workflows/spectrafx-validation.yml` watches RHI source files as triggers.

## Compatibility / boundaries

- Core remains genre-independent. `SpectraFxView` is the only camera input of the test; no FPS/RTS/Editor classes, asset IDs, ECS gameplay components or authoritative world writes.
- Metal remains the only enabled GPU compute backend. Vulkan/GL/GLES retain CPU fallback. `readColorRGBA8` is an optional debugging method; no existing backend must implement it.
- `gfx::Device` and Metal resources are borrowed by the SpectraFX objects and stay alive until after test emitter, renderer, and shared compute handles are released.
- Test fixture is intentionally narrower than production `Renderer3D::render` / Editor GPU Preview: it validates compute→RHI pass→render target/readback, **not** full scene renderer composition, screen UI integration, actual editor screenshots or HybridPlayback's internally managed retire queue.
- 24 frames are a bounded *acceptance smoke*, not high-volume leak testing, ASan, or quantified performance. The fixture owns a separate three-serial retirement queue and does not assert the real HybridPlayback retirement policy under arbitrary failure.

## Actual tests and evidence

- GitHub connector source inspection and checked-exact-anchor edits succeeded. The code was committed and pushed to `feature/spectrafx-generic-cue-queue`.
- Source-only API/structure review performed.
- **NOT RUN for Stage 98**: CMake configure, native Objective-C++ compilation, standalone RHI test, full CTest, actual Metal pixels, GPU lifetime stress or Editor visual QA. No local access to the Mac Studio GPU is available in this conversation.
- The validated **Stage 97** run of 8/8 acceptance stages and **2/2 raw-Metal GPU tests PASS** predates this new code; do not transfer its test status to Stage 98.

## Next commands (clean, isolated Mac Studio worktree)

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue

cmake -S . -B build/spectrafx-validation \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/spectrafx-validation --parallel 4 \
  --target aiverse_spectrafx_metal_rhi_test
ctest --test-dir build/spectrafx-validation \
  -R '^spectrafx_metal_rhi$' -V --output-on-failure
```

If the focused test passes, run **one full acceptance**:

```bash
bash tools/spectrafx/acceptance.sh build/spectrafx-validation
```

Gather exact SHA, configure/compiler output, RHI pixel values, `gpuHealthy` and complete acceptance summary. If the new test fails, fix root cause in the *actual* Metal backend/test or correct assumptions; never weaken assertions without evidence. Add a new agent handoff after a code fix. The production Renderer3D integrated viewport and Editor GPU Preview screenshot tests plus rollback/async callback + actual HybridPlayback emitter lifetime stress remain separate next stages.

## Handoff for next agent

Start from this file, `docs/agent-handoffs/spectrafx-current.md`, and the actual source. Do not reintroduce RTS camera references into `engine/fx` or merge unrelated concurrent AIVERSE branches. Finish Stage 98 native verification, then add a real Renderer3D/Editor multi-view integration fixture (with pixel/camera parity), stress `SpectraFxHybridPlayback::resetPresentation()`, 3-frame retirement and GPU impact feedback generation invalidation, and record *real* logs.
