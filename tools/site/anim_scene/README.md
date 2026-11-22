# MotionWeave website frame

`tools/site/anim_showcase.cpp` (`AIVERSE_BUILD_SITE_SHOWCASE=ON` → `aiverse_anim_showcase`) puts two
armies of the showcase golem (`templates/showcase/models/golem.glb`, clips Idle / Walk / Run /
Breathe / Attack / Death) on the sunset ground from `tools/site/fx_scene`. Every actor has its own
`AnimationGraphInstance` of one graph: a state machine (march → fight → dead), a walk/run blend, an
additive breathing layer and the attack masked to the upper body (`spine`), so fighters keep their
legs in idle or walk. Per-actor parameters and clock offsets come from a seeded hash. The sword
(`sword.glb`) follows the evaluated right-arm node every frame; the team colour goes on the
`TeamCloth` material. Website frames only: no engine behaviour changes.

Content folder: `ground.gltf`/`rock.gltf` (`tools/site/strata_showcase_scene.py`),
`scene-sunset.json`, `post-sunset.json` (from `tools/site/fx_scene`), `golem.glb`, `sword.glb`.

```sh
cd <content> && AIVERSE_CONTENT=$PWD ANIM_SCENE=scene-sunset.json ANIM_POST=post-sunset.json \
ANIM_W=1920 ANIM_H=1080 ANIM_CAM="0,1.2,0,11.5,0,10,42" ANIM_SUNDIR="-0.5,0.45,0.8" ANIM_SUN=1.3 \
ANIM_ADVANCE=3 AIVERSE_SCREENSHOT=motionweave-battle.ppm AIVERSE_EXIT_AFTER_FRAMES=4 \
<build>/aiverse_anim_showcase
```

The clip is the same command with `ANIM_ADVANCE` from 3.0 in steps of 1/30 s, one run per frame,
at 1280×720. Captured on Linux, OpenGL through Mesa llvmpipe (Xvfb): 90 actors, 63 marching,
21 fighting, 6 fallen.
