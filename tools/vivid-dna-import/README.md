# MetaHuman DNA → VIVID diagnostic import

This **offline authoring adapter** reads real binary MetaHuman DNA using an
externally supplied official Epic DNA Calibration reader. It exports one LOD's
neutral geometry and optionally one exact source blendshape target to a normal
glTF + VIVID JSON manifest. It never treats MetaHuman binary DNA as VIVID JSON.
Neither Epic's SDK nor character data is vendored or linked into the game runtime.

Verified sample: Epic's `data/dna_files/Ada.dna`, upstream revision
`8297ff1bd6f1692a6e221aa8395a66486e5a53c9`, SHA256
`e764cfab938283231bcb3e5af1c7bcbec5154bab4804aafa1b5b7dc6e400ca5e`.
Source: https://github.com/EpicGames/MetaHuman-DNA-Calibration
The sample's internal descriptor says `Archtype`; the filename is Ada.

## Build outside the repository

Supply your separately obtained SDK checkout and licensed character file.
The SDK has its own license; the current general MetaHuman license and the older
Calibration repository license are distinct documents. Do not redistribute the
SDK or sample with AIVERSE. No automatic download or license acceptance is built
into this tool. The pinned reader supports legacy DNA from UE 5.5 and earlier;
UE 5.6+ DNA and arbitrary third-party DNA variants have NOT been verified.

```sh
cmake -S tools/vivid-dna-import -B "$BUILD/import-build" \
  -DDNA_SDK_ROOT="$SDK" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD/import-build" -j 8
"$BUILD/import-build/vivid_dna_decode" "$ASSET/Ada.dna" "$BUILD/Ada.json" 0
python3 tools/vivid-dna-import/package.py "$BUILD/Ada.json" "$BUILD/ada-front"
AIVERSE_VIVID_CREATOR_CAPTURE="$BUILD/ada-front.ppm" \
  "$NATIVE/aiverse_vivid_creator" --smoke --still \
  --asset "$BUILD/ada-front/manifest.json"
```

Set BUILD, SDK, ASSET and NATIVE on KINGSTON for this project. A decoder output or
package destination must not already exist. Interrupted decoder output is marked
`.partial`; inspect/remove only that failed output before retrying.

For a second view, package with `--yaw -0.5` (radians). For a source morph, inspect
`meshes[].targets` in the neutral export, then pass its exact name as the decoder's
fourth argument, for example `brow_raiseIn_L`. Package with `--channel browInnerUp`
to explicitly route that *one* target through VIVID's GPU/CPU morph stream. This is
a diagnostic mapping, NOT automatic ARKit or RigLogic semantic equivalence.
Set VIVID `expressionRest[2]` to 1 in that package to exercise this specific brow
channel; `--smoke --still` snaps channels and freezes MotionWeave for comparison.
Creator's jaw/blink/smile demo controls override their own rest channels.

`--weight 1` instead bakes the selected delta into geometry. Combining baked and
live deformation is rejected. Original source position IDs expand to every UV/
normal-layout duplicate, preserving seam consistency. The glTF import order is
the VIVID sparse-morph vertex order, checked on the real Ada test. Source-to-preview
center/scale and baked weights are saved in `provenance.json`.

## Deliberate limits

- This stage exports Y-up reference geometry in a normalized preview frame; it is
  not a production unit/coordinate conversion pipeline. Other coordinate systems
  require inspection before use. Mesh polygons are fan-triangulated.
- No joint hierarchy, skin weights, RigLogic behavior, corrective combinations,
  animated maps, body, groom or complete facial solver is imported yet.
- DNA contains no skin/iris texture maps or groom assets. Diagnostic flat colours
  are used. Untextured eyelashes, eye-edge/tear shells and saliva stay in the asset
  but are made transparent; they require their real opacity/shader assets later.
  The eyelash material tag prevents Creator from adding its procedural wig.
- A source target's position deltas are copied exactly (with uniform preview scale).
  Morph normal/tangent reconstruction and joint-driven expression parity remain.
- The native reader accepts only files within a 512 MiB input cap; exported LODs
  are capped at one million layout vertices. This is not a hardened untrusted-file
  service or a compatibility claim for every DNA revision.

## Actual acceptance, 2026-11-20

Apple Silicon SDK/adapter and Creator builds passed. Ada LOD0: 9 meshes, 34625
layout vertices; 681 source target names on the head. Four package tests passed
(UV seam expansion, invalid index, no double deformation, output preservation).
Reader rejected invalid DNA signature, out-of-range LOD and `0junk` LOD.

Actual Metal captures passed for front, ¾, live brow GPU and live brow CPU. The
live source target changes 200 viewport pixels at total RGB threshold 4 versus
neutral. GPU vs CPU differs at **one** of 586500 sampled pixels (max RGB sum delta
8); strict bit-exact image equality FAILED and is not claimed. Both paths rendered
successfully. Full RigLogic, texture quality, newer DNA, and other backends remain
NOT TESTED. All artifacts/logs are under the external build root's `metahuman/`.

## Full-character companion export (2026-11-20)

The legacy Epic `Ada.dna` sample contains the head/neck, not a full body. A full
character cannot be reconstructed from that file alone. For full-body acceptance
we separately imported the published Ada UE 5.6 GLB from
https://github.com/creategamecharacters/metahuman-to-glb (CreateGameCharacters).
Source URL: https://creategamecharacters.github.io/metahuman-to-glb/characters/ada/ada.glb
SHA256: `70ad5d190e3cdfb54e88d405b75d7b4731547f2bd93d5cbad0ce4720946d99de`.
This is a companion-export test, NOT evidence that the binary DNA adapter imports
body DNA, clothing, original RigLogic or skinning from the old head-only sample.

`prepare_full_character.cjs` uses external packages `@gltf-transform/core@4.2.1`,
`@gltf-transform/extensions@4.2.1`, `draco3dgltf@1.5.7` installed into a separate
KINGSTON tooling directory. It decodes Draco offline because the native importer
requires uncompressed geometry. The 7 meshes, 15 primitives, 5 skin palettes and
16 texture images are retained; duplicate garment bone names are made unique.
The face skin's own head bone becomes `vivid_face_head`. A parent frame uniformly
scales/centers the model for Creator without rewriting skin inverse-bind matrices.

```sh
node tools/vivid-dna-import/prepare_full_character.cjs \
  "$TOOLING" "$BUILD/ada-source.glb" "$BUILD/ada-full" 0 "$BUILD/mh_materials.json"
AIVERSE_VIVID_CREATOR_CAPTURE="$BUILD/ada-full.ppm" \
  "$NATIVE/aiverse_vivid_creator" --smoke --still --asset "$BUILD/ada-full/manifest.json"
```

Use yaw `-0.5` for the ¾ view. Destination must be new. `mh_materials.json` comes
from the same published character directory. The source UE cloth material uses a
packed region mask in the GLB base-colour slot: the adapter removes that colour
binding and applies the author's `diffuse_color_1`, roughness and metallic values.
This is an explicit **uniform-cloth fallback**, not full mask/detail shader parity.
Original skin and other texture bindings stay in the asset. Hair uses VIVID's
material profile; exact UE hair/eye appearance has not been established.

Creator idle animation now adds its demo bob to the head's bind translation:
absolute zero translation would collapse a real imported head toward its neck.
Actual Metal front, ¾ and CPU captures all passed. Both final GPU images were
visually inspected, including hands, feet, clothes, head and hair. Native source
has 119916 position vertices; after importer skin-palette splitting Creator shows
125302 vertices. The full body is in its rest pose; complete rig animation and
original facial solver parity have NOT been tested.

All assets, package-lock, builds, provenance and captures are external under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/`.
No character asset or external dependency is vendored in the repository.
