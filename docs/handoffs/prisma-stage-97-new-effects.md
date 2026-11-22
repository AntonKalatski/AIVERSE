# PRISMA Stage 97 — new effects (2026-11-22)

Branch `feature/aiv20261122_prisma_effects`; code `3d381e09`, docs `7b8b2808`, merged with
`origin/main` and pushed to `main` as `b8ac8683` (owner's request).

## What was added

- Catalog (`engine/render/PrismaData.{h,cpp}`): `ambientOcclusion`, `screenSpaceReflections`,
  `lensFlare` (text param `dirtTexture`), `smaa`, `lensDistortion`, `anamorphicStreaks`,
  `halation`, `heatHaze`, `fsr`, `dlss`; `autoExposure` + `histogram`, `histogramLow`,
  `histogramHigh` (appended, old profiles unchanged). Plans `aoPlan`, `ssrPlan`, `flarePlan`,
  `streakPlan`, `smaaSearchSteps`, `prismaScaledRenderSize`; cost model for every effect.
  Presets `lens`, `realistic` + generated `toon sketch crt scanlines dither kuwahara`
  (`tools/prisma/make_style_presets.py` -> `engine/render/PrismaStylePresets.h`).
- Shaders: `engine/render/PrismaShadersExtra.h` (AO, AO apply, SSR, flare ghosts, SMAA x3,
  FSR EASU/RCAS, histogram + percentile resolve); composite (`PrismaShaders.h`) bits
  `PF_DISTORT PF_HAZE PF_SHOCK PF_FLARE PF_DIRT_TEX PF_STREAKS PF_HALATION`, texture slots
  10–13, helper `pfDisplay` for the Shader Graph `sceneColor` node.
- Renderer (`engine/render/Prisma.{h,cpp}`): AO + SSR before exposure metering; flare /
  streaks / halation after DoF; post-composite chain SMAA | HDR FXAA -> FSR (EASU+RCAS);
  external upscaler route (`PrismaExternalUpscaler`, `setExternalUpscaler`) with Halton
  jitter in `jitteredCamera`, velocity pass, output storage target; `setShockwaves`;
  `renderSizeForOutput` = temporal first, then DLSS, then FSR; reports for every new effect.
- Shader Graph: postfx node `sceneColor`, postfx `sampleBudget` 1..16 (particle still no).
- RHI: `Device::nativeVulkanContext/Image/ImageBarrier`, `gfx::vulkanExtensionRequests()`
  (Vulkan backend implements them; others return false).
- DLSS: `engine/render/PrismaDlss.h`, `PrismaDlssNgx.cpp` (built only with
  `-DAIVERSE_DLSS_SDK_DIR`, Windows + Vulkan), `PrismaDlssStub.cpp` otherwise; GameClient
  registers the provider on Vulkan.
- Editor: lens-flare text param no longer shows the LUT button.
- Third party: `third_party/smaa` (AreaTex/SearchTex, MIT), `third_party/fsr` (licence of the
  ported EASU/RCAS), `third_party/notices.json` entries.

## Tests actually run

- GLSL ES 3.00 of all 11 new programs and 8 composite variants (each new bit, all bits,
  with underwater depth): `glslangValidator -S frag` — PASS (Linux, cloud).
- `prisma_new_effects` (tests/render/prisma_new_effects.cpp), built with gcc 13 against
  PrismaData/ShaderGraph/Json/Fixed only — PASS. Repeated after merging main — PASS.
- `g++ -fsyntax-only -Wall -Wextra` of Prisma.cpp, PrismaData.cpp, GfxVulkan.cpp,
  GameClient.cpp — clean.

## NOT RUN

- Full CMake build on KINGSTON, Metal (MSL compile of the new shaders and composite),
  `prisma_<fx>_gpu` / `_gl` / `_vulkan`, `shader_graph` compiler test, editor UI.
  Prepared: `_tmp/prisma-effects/run.command` in the owner's checkout (worktree on
  KINGSTON, build `/Volumes/KINGSTON/AIVERSE-builds/prisma-effects/release`, logs + frames in
  that folder). The Mac screen was held by another session, so it was not started.
- DLSS adapter: never compiled (no NGX SDK, no Windows); NGX API names follow the public
  DLSS SDK headers. Jitter sign is a guess (`AIVERSE_DLSS_JITTER_SIGN=-1` flips it).
- Vulkan SPIR-V of the new GLSL, WebGL2, weak Android GPUs, measured GPU cost.

## Risks

- The composite body changed for every variant (edge mask, chromatic block, new helpers):
  an MSL error there would disable the whole composite on Metal. Check `prisma_*_gpu` first.
- SSR/AO reconstruct normals from depth: thin geometry and depth edges can produce noise;
  SSR uses `minNormalY` instead of material reflectivity (no G-buffer).
- SMAA is texture-space; diagonal detection disabled.

## Next step

Run `_tmp/prisma-effects/run.command` on the Mac, fix anything `ctest -R "prisma|shader_graph"`
reports, then publish the devlog entry with the captured frames (`feature/site-devlog`,
system `prisma`, update `systems.json` includes/roadmap).
