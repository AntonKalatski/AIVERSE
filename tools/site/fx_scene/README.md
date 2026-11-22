# SpectraFX website scenes

Two frames for the SpectraFX system page, rendered by `tools/site/fx_showcase.cpp`
(`AIVERSE_BUILD_SITE_SHOWCASE=ON` → `aiverse_fx_showcase`). Website frames only: the host and
these files change no engine behaviour.

Effects (`fx/`, the ordinary SpectraFX JSON format, CPU particle backend):

| Effect | What it is |
|---|---|
| `campfire` | flame body + upright flame tongues, embers with trails, smoke, one warm light |
| `arcane_portal` | two counter-rotating spark rings, a rim line, an inward swirl and a cold light; effect Y = portal axis (play with a direction) |
| `arcane_motes` | slow glowing motes rising from the portal base |
| `fireflies` | drifting warm motes in a box, a few with small lights |
| `floor_mist` | low alpha mist cards |

PRISMA: `post-night.json` (HDR, bloom, colour grade, vignette) and `post-sunset.json` (the same
plus `physicalSky`: atmosphere, clouds and god rays). Volumetric fog comes from `FX_FOG`
(PRISMA local fog boxes).

## Night in Sponza (`sponza-night.jpg`)

Content folder: the Khronos glTF Sponza (glTF-Sample-Assets) with its textures, `logs.glb` from
`templates/showcase/models`, `fx/` and the two files from here. The capture used Sponza split
into six parts (`sp0.gltf`…`sp5.gltf`, the STRATA test split); `scene-sponza.json` lists them.
For normal rendering one `Sponza.gltf` instance is equivalent.

```sh
cd <content> && AIVERSE_CONTENT=$PWD FX_SCENE=scene-sponza.json \
FX_PLAY="campfire@-4.2,0.3,0.95,1.4,0;campfire@-7.4,0.3,-0.95,1.4,0.3;arcane_portal@-10.0,1.85,0,0.9,0,1,0,0;arcane_motes@-10.0,0.1,0,1,0;fireflies@-6,1.8,0,1,0;floor_mist@-6,0.15,0,1,0" \
FX_ADVANCE=6 FX_CAM="-8,1.9,0,9,90,3,55" FX_W=1920 FX_H=1080 \
FX_SUN=0.1 FX_SUNCOLOR=0.45,0.6,1.0 FX_SUNDIR=0.3,1,0.2 \
FX_AMBIENT=0.03,0.045,0.09,0.012,0.012,0.02 FX_SKY=0.015,0.02,0.05 \
FX_POST=post-night.json FX_FOG_AMBIENT=0.25 FX_FOG="-6,1.2,0,8,1.2,2.6,0.05,0.55,0.6,0.8,1,0.7" \
AIVERSE_SCREENSHOT=sponza-night.ppm AIVERSE_EXIT_AFTER_FRAMES=31 <build>/aiverse_fx_showcase
```

## Sunset portal (`sunset-portal.jpg`)

Content folder: `ground.gltf`/`rock.gltf` from `tools/site/strata_showcase_scene.py`, `fx/`,
`scene-sunset.json` (the STRATA scene without the four nearest rocks) and `post-sunset.json`.

```sh
cd <content> && AIVERSE_CONTENT=$PWD FX_SCENE=scene-sunset.json \
FX_PLAY="arcane_portal@0,2.3,-4,1.2,0,0,0,1;arcane_motes@0,0.6,-4,1.1,0;campfire@-3.5,0.45,1,1.5,0;campfire@3,0.4,0.5,1.4,0.4;fireflies@0,1.4,0,1.4,0" \
FX_ADVANCE=6 FX_CAM="0,1.2,-2.5,12.5,0,15,52" FX_W=1920 FX_H=1080 \
FX_SUN=1.1 FX_SUNCOLOR=1.0,0.6,0.35 FX_SUNDIR=0.2,0.11,-1 \
FX_AMBIENT=0.10,0.085,0.10,0.04,0.03,0.025 FX_SKY=0.3,0.25,0.2 \
FX_POST=post-sunset.json FX_FOG="0,0.8,-26,45,1.6,16,0.05,0.95,0.78,0.62,4,0.6" \
AIVERSE_SCREENSHOT=sunset-portal.ppm AIVERSE_EXIT_AFTER_FRAMES=31 <build>/aiverse_fx_showcase
```

Both were captured on Linux, OpenGL through Mesa llvmpipe (Xvfb), 1920×1080, 6 s of simulation
in fixed 1/60 steps, then frozen.

## Found while building the scenes (not fixed here)

- **Particles over the physical sky disappear.** The apps draw particles into the open scene pass
  before `resolvePrisma()`; PRISMA then draws `physicalSky` over every far-depth pixel, including
  those already covered by particles (they write no depth). Embers above the horizon and the top
  of a portal against the sky vanish. The sunset camera looks down so effects stay below the horizon.
- **Trails of orbiting particles.** With `velocityOverLife.orbital*` and zero start speed, `trail`
  ribbons render as vertical streaks instead of following the orbit. The portal uses no trails.
