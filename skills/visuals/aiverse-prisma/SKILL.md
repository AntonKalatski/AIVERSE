---
name: aiverse-prisma
description: PRISMA — universal AIVERSE post-processing, HDR, tone mapping, bloom, temporal/screen-space effects and camera volumes; migration and agent handoff.
---

# PRISMA — AIVERSE universal image post-processing

**Read first:** `AGENTS.md` (mandatory PRISMA vs SpectraFX ownership matrix), `docs/prisma.md`, `docs/postfx.md`, `docs/hdr.md`, the latest `docs/handoffs/prisma-stage-XX.md`.

## Choose the right owner

- PRISMA owns **image-space processing**: HDR scene resolve, tonemapping, color grading, LUTs, bloom, DoF, motion blur, auto exposure, TAA/FXAA/upscaling, screen-space GI/AO/SSR, camera volumes and full-screen `postfx` Shader Graph domain. `PrismaScreenLighting` is part of PRISMA.
- SpectraFX owns **simulation/presentation of 3D VFX**: emitter graphs, sprite/mesh/ribbon particles, smoke/fire/explosions/trails, ECS FX events. It adds linear HDR pixels to the scene BEFORE PRISMA processes the result.
- Shared Renderer3D/RHI, generic Shader Graph compiler and ECS transport adapters are engine infrastructure, not a second postprocess implementation. **Do not duplicate features or couple them to genres.**

## Native C++ integration

```cpp
#include "engine/render/Prisma.h"
#include "engine/render/PrismaScreenLighting.h"

aiverse::render::Prisma prisma;
prisma.init(device);
prisma.configure(gameManifest["postfx"], mapDocument["postfx"]);
prisma.setQuality(aiverse::render::PrismaQuality::High);

// Optional per-camera blend; currently host-controlled (not auto-wired to ECS):
aiverse::render::PrismaVolumeStack cameraVolumes;
prisma.applyCameraVolumes(cameraVolumes, camera.eye());

// Existing Renderer3D flow: scene, particle emissions, resolve, then UI.
renderer.render(pass, &prisma);
drawSceneVfx();
renderer.resolvePrisma();
// Draw HUD/UI after the image resolve.
```

Use `aiverse::render::PrismaSettings`, `PrismaEffect`, `PrismaType`, `parsePrisma`, `prismaCatalog`, `prismaCostReport` for GPU-free data workflows. Link `AIVERSE::prisma_data` (alias of `aiverse_postfx_data`) for pure data/quality logic; renderer integration uses `aiverse_runtime` as before.

## Editor and automation

Existing external identifiers are stable by design: `postfx` JSON key, `postfx_catalog`, `postfx_get`, `postfx_add`, `postfx_set`, `postfx_preset`, `postfx_estimate`, `view_postfx` MCP operations and `postfx` saved panel id. The implementation now lives in `apps/editor/core/PrismaOps.{h,cpp}` and `apps/editor/windows/PrismaWindow.cpp` (title PRISMA). Do not rename persisted schemas or duplicate editor registration.

Common workflow: inspect `postfx_catalog` → choose preset or add effects → check device-tier budget from `postfx_estimate` → compare `view_postfx` tiers and `view_screenshot`. The `postfx` Shader Graph domain is post-processing, not the SpectraFX particle graph.

## Source tree and compatibility

- Canonical core: `engine/render/Prisma.{h,cpp}`, `PrismaData.{h,cpp}`, `PrismaShaders.h`, `PrismaVolumes.h`.
- Temporal/screen-space: `PrismaScreenLighting.{h,cpp}`, `PrismaScreenLightingShaders.h`.
- Legacy `PostFx*.h`, `ScreenLighting.h` and old ops header are compatibility-only; legacy .cpp shims have no definitions. New engine changes MUST go in PRISMA files.
- PRISMA's volume stack is currently opt-in and GPU-free. **RenderGraph, auto exposure, physical depth of field, motion vectors, generalized motion blur and temporal upscaling are not yet implemented**. Never label these as working until code, build and rendering tests prove it.

## Mandatory validation and handoff

Run `./run-tests.sh --quick` plus targeted HDR policy/GPU pixel tests once a native checkout is available; on macOS build `aiverse_hdr_policy`, `aiverse_hdr_scene`, `aiverse_hdr_gl`, editor/client and check the Metal path. Report actual commands and PASS/FAIL/NOT RUN, never infer success from source files or queued CI.

After **each** bounded code stage, commit `docs/handoffs/prisma-stage-XX.md` on the same branch: exact SHA, architecture and API, all changed paths, actual tests/results, limitations, and the next actionable step. Keep PRISMA handoffs separate from `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` maintained by SpectraFX.
