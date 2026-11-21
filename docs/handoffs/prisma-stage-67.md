# PRISMA stage 67: Depth of Field and Motion Blur out of experimental (2026-11-21)

Branch `feature/aiv20261121_vulkan_gi_android`.

## Change

- `engine/render/Prisma.cpp`: `prismaOptIn(key)` — DoF / Motion Blur default on,
  `=0` kill switch; all other opt-ins unchanged (`=1`). Used by
  `wantsObjectMotionVectors`, the motion blur and DoF passes and the effect report
  (reason text "disabled by AIVERSE_PRISMA_DOF=0").
- Docs/skills/editor help no longer tell users to set the flags.
- New GPU acceptance `tests/render/PrismaEffectsScene.h` (`AIVERSE_PRISMA_EFFECT_TEST=dof|motion`),
  CTests `prisma_{dof,motion}_{gpu,gl,vulkan}`.

## Compatibility

Projects that already list `depthOfField` / `motionBlur` now get the effect
without the variable. No template in the repository lists them. `minQuality`
High by default keeps them off on Low/Medium tiers.

## Verified

Header syntax (g++ -fsyntax-only on HdrScene.h, Linux). GPU tests: NOT RUN yet
(queued on the Mac: Metal + GL; Android: Vulkan).

## Next

Run the GPU tests; tune thresholds only from measured values.
