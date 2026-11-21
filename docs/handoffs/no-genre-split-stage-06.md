# No genre split — stage 06 handoff: stage C0a, game code out of the RTS core (2026-11-21)

Branch `feature/aiv20261121_no-genre`. Plan: `docs/no-genre-split-audit.md` ("Stage C, revised order").
Design of the direct-control step that follows: `docs/handoffs/no-genre-split-stage-05-design.md`.

**Status: built and tested on the Mac (KINGSTON, 74c8e98cc + link fix):** CTest 397/398 (only `skill_audit`, known
from main); template checksums all equal except `boss_arena` seed 2 (known non-determinism from main).
Link fix: `aiverse_terraweave_preview_test` now compiles `engine/world/AsyncChunkStream.cpp` and `ChunkStreamer.cpp`
(TerraWeaveHorizonScene from main uses them; main has the same missing link).

## Code
- 4aac6d009 merge of origin/main.
- e7717e98a Bunker Wars out of the core: AI controller registry `modules/rts/RtsAIControllers.h/.cpp`
  (`registerAIController` / `findAIController`, game code registers itself in `RtsGameExtensions.cpp`);
  rules `bwAI` -> `aiController` (default `"builtin"`); hypnosis sends in `RtsHypnosis.cpp`.
- 8832de38a `RTSCamera` alias deleted, 109 files use `render::Camera`.
- 056468160 CTest `genre_free_engine` (`tools/genre/check_engine_genre_free.py`); debt left: engine/net/authority
  Session.cpp/.h, engine/net/city CityNet/CityMirror (52 names).
- b264ffb96, 0abb89532 Kingston (GTA2) out of the core:
  - `modules/rts/kingston/` deleted; the traffic, pedestrian and driving code calls the library functions
    `trafficWideExit`, `trafficLocalDetour`, `sweptBoxCircle`, `polylineBounds`;
  - data instead of code: `traffic.wideRoads` (types that keep to wide streets; was `"fire_engine"`), map
    `traffic.noParking` ([x0,z0,x1,z1] cells), `traffic.services.<id>.officers`, `traffic.incidents.patient`,
    `traffic.incidents.unarmed`, type `walls` ([x,z,w,d] cells) with weapon `hitsWalls` and `wrecks` (tag)
    (was the 816-line rocket wall table and the weapon id `"rocket"`);
  - `Kingston*.inc/.h` -> `RtsServiceCarry.inc`, `RtsServiceCustody.h/.inc`, `RtsPedestrianVehicles.inc`.

## Behaviour changes
- `trafficWideExit` measures the carriageway from the kerbs: same as before on straight pieces, lane-based on
  curved pieces (the copy used `width()`).
- A game without the new data loses the Kingston behaviour (fire engine on wide roads, no-parking zones,
  rocket walls, `cop` as an officer). GTA2: run `_tmp/gta2-c0a-data.py /Volumes/KINGSTON/GTA2` once
  (walls table in `_tmp/gta2-walls.json`); not run yet, the GTA2 tree has someone's uncommitted work.
- The GTA2 patch scripts that insert `Kingston*.inc` includes / swap `kingston/` headers now fail: retire them.

## Saves that break
- bot state of Bunker Wars (the controller is saved by kind name);
- pedestrian crossing state (`resident_crossing_*` trigger vars are now `crossing_*`).

## Left for C0a
- trigger-variable protocol between the core and GTA2 scripts (`police_*`, `resident_*`, `tow_*`, `ambulance_*`,
  `vehicle_beacon_*`, `drive_*_phase`, `tow_gate_x/z`): to components or a GTA2 native module;
- `RtsView` friends `kingston::TowHost`, `BulletFxHost`, `CabinResidentDraw`; `RtsArsenal.cpp` `"fists"`.
Then C0b (data model and combat to `engine/game`), C1..C7, D, E, F, G.
