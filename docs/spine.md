# Spine 2D skeletal animation

`engine/spine` is an own implementation of the Spine 4.x runtime (presentation only, floats): skeleton JSON
and libGDX atlases (both layouts, rotated / trimmed regions), every bone transform mode, skins, region / mesh /
linked-mesh attachments (weighted or not), IK (1 and 2 bones, softness, stretch, compress), transform
constraints, path constraints (fixed / percent position; length / fixed / percent / proportional spacing;
tangent / chain / chain-scale rotation; constant-speed and per-curve-length paths, ported from spine-runtimes
4.0 `PathConstraint`), clipping attachments (the clip polygon is ear-clipped into convex pieces, every following
triangle up to the end slot is cut against them, uvs from barycentric coordinates), timelines for bones, slots
(rgba / rgb / alpha / two-color light part), attachments, IK, transform and path constraints (position, spacing,
mix), deform (meshes, clipping and path vertices), draw order and events with stepped / linear / bezier curves,
and an `AnimationState` with crossfades (`defaultMix`, per pair), queues and events. Not drawn (a warning on
load): sequences, dark colors. Rendering goes through the RHI with premultiplied alpha (GLSL + MSL in
`SpineShaders.h`): normal, additive (alpha written 0), multiply (`dst * (src + 1 - a)`) and screen
(`gfx::BlendMode::Screen`: ONE, 1 - srcColor, as the Spine runtimes; GL and Metal).

Binary skeletons (`.skel` / `.skel.bytes`, Spine 4.0) are converted to JSON first — exactly, the whole file is
consumed or it stops:

```sh
python3 tools/importers/spine/skel2json.py hero1.skel.bytes spine/hero1.json
cp hero1.atlas.txt spine/hero1.atlas; cp hero1.png spine/
```

UI views (`<img src="#spine:NAME"/>` in any RmlUi document), declared in game.json or created by UI scripts:

```jsonc
"spine": { "hero1": { "skeleton": "spine/hero1.json", "atlas": "spine/hero1.atlas",   // atlas: default next to it
                      "width": 512, "height": 512, "scale": 1, "skin": "default", "pad": 8,
                      "animation": "hero1_idle1", "loop": true, "mix": 0.2,
                      "idle": ["hero1_idle1", "hero1_idle2", "hero1_idle3"] } }       // random idle after each loop
```

Lua (UI scripts): `spine.create(name, opts)`, `spine.play(name, anim, loop[, track])`,
`spine.add(name, anim, loop[, delay[, track]])`, `spine.skin(name, skin)`, `spine.animations(name)`,
`spine.remove(name)`; Spine events arrive through `game.on("spine", fn)` as `{view, type, animation, name, int,
float, string}`. Each view renders into its own render target every frame (skeleton framed by its first posed
frame). Tests: `aiverse_tests spine_` (tests/tests_clientkit.inc).

