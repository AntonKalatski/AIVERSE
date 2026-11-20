# SpectraFX Stage 106 — Editor GPU preview camera and alpha-sort coherence

Date: 2026-11-20
Branch: `feature/spectrafx-generic-cue-queue`
Code SHA: `f12afc07483523b34923f1fc1378c654cf40c9fb`
Previously hardware-validated source: Stage 105 `c4fc948535557f1cc1a00459d1a3cd48257ad6ca`; user-provided Apple M3 Ultra 126/126 full CTest and 4/4 actual MetalGPU PASS. **Stage 106 is NOT native-compiled or Editor-validated**.

## Implementation

Modified only `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`, no genre core changes.

- Preview RT is ensured before building `fx::SpectraFxView` from camera, actual RT width/height and `viewProj=proj(aspect,zeroToOne)*view`.
- Same packet drives exactly one `previewHost_.prepareGpu(dt,view)` and `prepareCpu(dt,view)` per Editor frame.
- Calls `previewHost_.prepareRenderView(view)` outside Renderer3D render encoder, correctly sorting GPU alpha for the orbit camera. On preparation failure, displays an error and skips FX drawing for that frame.
- `previewHost_.render(renderer.lighting,view)` draws GPU and CPU particles with identical view/projection/size metadata. A Reset view button restores preview camera pose. Existing Restart, Pause/Resume, Step and mouse orbit remain.
- No effect JSON or ECS schema changes; Editor owns its renderer/viewport and releases the preview host before the RHI.

## Actual verification

GitHub source inspected; exact-anchor code replacement was committed/pushed. **No native Clang/Metal build, CTest, Editor interaction or visual screenshot run on this SHA.** Prior Stage 105 green logs do not verify this new editor change.

## Next agent

Implement PRISMA HDR/Bloom integrated offscreen fixture with `Renderer3D::render(pass,&post)` → `SpectraFxSceneHost::render` → `Renderer3D::resolvePostFx` order, no resimulation. Validate real GPU pixels and compare baseline/FX. Then multiview sorted alpha and high-load HybridPlayback tests; run one full strict native acceptance after implementation. Keep `main` and parallel camera/PRISMA worktrees unchanged.
