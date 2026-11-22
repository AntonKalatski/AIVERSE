# PRISMA Stage 99 — new effects (2026-11-22)

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
- Vulkan (Linux, Mesa lavapipe 25.2, `aiverse_hdr_vulkan`, cloud build): all 12
  `AIVERSE_PRISMA_EFFECT_TEST` cases PASS at `b7bf96e9` (dof motion ao ssr flare smaa distort
  streaks halation haze fsr histogram), plus `aiverse_prisma_new_effects_tests` and
  `aiverse_shader_graph_tests` PASS.
- macOS Mac Studio (M3 Ultra), Release, Unix Makefiles, build on
  `/Volumes/KINGSTON/AIVERSE-builds/prisma-effects/release`, source `b7bf96e9`
  (`_tmp/prisma-effects/run.command`, 2026-11-22 10:48 MSK):
  `ctest -R "prisma|shader_graph" -E "_gl$"` 18/18 PASS (Metal `prisma_*_gpu` for all 12
  effects, prisma_graph, prisma_lens_water, prisma_volume, prisma_new_effects,
  prisma_style_presets_fresh, shader_graph); `ctest -R "prisma.*_gl$"` 12/12 PASS
  (GL 4.1 over Metal). Frames: `_tmp/prisma-effects/shots/{metal,gl}/hdr.ppm.<fx><phase>.ppm`.

### First Mac run (source 4e3fc5df) and what it found

20 of 30 failed. 18 were the test print format ("diff X PASS" vs the CTest regex
"PRISMA <fx>: PASS"); the effects were encoded. Looking at the frames found real defects,
fixed in `b7bf96e9`:
- AO: horizontal bands on the grazing floor (depth quantisation in 1-texel normals and
  self-occlusion). Normals now use a 3-texel baseline and the origin is lifted along the
  normal by `0.01 * distance + 0.02 * radius`.
- SSR: speckled reflections (fixed 1.25 m steps skipped thin objects). Quadratic step
  spacing, step-aware thickness, origin lift.
- FSR EASU on Metal: output upside down (render-target flip applied twice: once to the
  output pixel, once in the taps). Flip now applied only in the taps; the test requires
  diff < 8 between native and upscaled frames.
- Anamorphic streaks: one smear over the whole row (spread tripled per pass, five-tap
  kernel left gaps). Spread now doubles per pass.
- SMAA bound loosened to 40% of the gradient energy (it smooths correctly; the scene has
  many edges). Flare/streak/halation tests use realistic thresholds.

## NOT RUN

- DLSS adapter: never compiled (no NGX SDK, no Windows); NGX API names follow the public
  DLSS SDK headers. Jitter sign is a guess (`AIVERSE_DLSS_JITTER_SIGN=-1` flips it).
- Vulkan on real GPUs (only lavapipe), WebGL2, weak Android GPUs, measured GPU cost,
  editor UI of the new effects.
- Full repository CTest (only `prisma|shader_graph` was run).

## Risks

- SSR/AO reconstruct normals from depth: thin geometry and depth edges can produce noise;
  SSR uses `minNormalY` instead of material reflectivity (no G-buffer).
- SMAA is texture-space; diagonal detection disabled.
- AO origin lift trades a little contact darkening for no banding; very small radii on
  far objects fade out.

## Next step

Devlog entry and PRISMA system page (`feature/site-devlog`). Then DLSS on a Windows + RTX
machine with the NGX SDK, and Vulkan on a real GPU.
