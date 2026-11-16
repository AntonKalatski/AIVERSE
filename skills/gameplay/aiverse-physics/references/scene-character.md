# Scene, Character and client prediction

Headers: `engine/physics/Scene.h`, `engine/physics/Character.h`. Target `AIVERSE::physics`
(`-DAIVERSE_ENABLE_PHYSICS=ON`). Details and numbers: `docs/physics.md` (Scene, Character),
`docs/replica.md` (place sandbox, § Prediction against bodies).

## physics::Scene

- Runtime world for one owner thread: `create(BodyDesc)` / `destroy(handle)`; a `BodyHandle`
  (slot + generation, 0 = `kNoBody`) never points at another body after a destroy.
  `create` returns `kNoBody` for invalid descriptions or at `maxBodies`; it never throws.
- `fromJson(json, Load::All | Load::StaticOnly)` uses the docs/physics.md format (+ `mesh` for
  static bodies, `userData`, `maxBodies`); errors throw `JsonError` naming the body and key.
  `staticCopy(json)` = static bodies only: the base of a client prediction world. Ids resolve with `find()`.
- `step()` = one fixed tick (`dt()`, substeps). Events are copied after the step and live until the
  next one: `contactBegins()`, `sensorEvents()`, `moved()` (sorted by handle). Destroying a sensor or
  a visitor ends its overlaps: those end events come first in the next step's list, with the dead handle.
- Queries (`raycast`, `raycastAll`, `overlapSphere/Box/Capsule`) skip sensors unless asked, take a
  category mask and an `ignore` handle, and return sorted results.
- Categories: `kCategoryStatic/Dynamic/Kinematic/Sensor` by motion, `kCategoryCharacter` for
  character proxies, `kCategoryQuery` for the queries themselves (a shape's mask must include it to
  be seen by queries and the controller).

## physics::Character

- `stepCharacter(world, params, state, input, dt, &pushes, self)`: kinematic capsule on Box3D's mover
  API (CastMover / CollideMover / SolvePlanes). Position = feet, +Y up, `yaw = 0` looks along -Z.
  Depends only on its arguments and the world geometry: the server and a predicting client run the
  same call. Invalid params / dt / non-finite state leave the position and zero the velocity.
- Walk / run acceleration, air control, jump, own gravity, `maxSlope` walls, `stepHeight` step-up (only
  a real climb: higher and further along the walk), ground stick, ceilings, sweep against tunnelling.
- Ground that moves carries the character (point velocity; on rotating ground the feet follow the
  arc and `state.groundTurn` reports the yaw turned: add it to the look yaw of the next input).
- Other characters are solid capsules; overlapping ones separate at most ~1 cm per plane solve,
  coaxial ones along a direction fixed by the own handle.
- Pushes: shapes in `kCategoryDynamic` the character walks into are reported (`Push`: body, point,
  direction, speed, maxImpulse); the host calls `Scene::applyPushes` (impulse up to the character's
  speed, clamped to `pushImpulseLimit` per tick and body). Proxies have no body contacts, so pushing
  is only this explicit, bounded impulse. Downward planes of dynamic shapes are ignored (a box on a
  head does not press the character into the ground).
- Host order per tick: every character steps against the pre-step world, then `applyPushes`,
  `moveCharacter(proxy, state)`, other kinematic motion, `scene.step()`.
- `CharacterInput::pack()` is 5 bytes; `unpack` of hostile bytes never fails. Predict with
  `input.quantized()` / the unpacked frame, exactly what the server sees.

## Client prediction world

`Scene::staticCopy` alone predicts walls and floors only; next to boxes, players and platforms every
reconcile corrects. Build the world the server's step meets (`apps/place/PlacePredict.h` is the
reference, `PredictionWorld`):

1. Static + kinematic bodies from the join message; kinematic bodies whose motion the scene defines
   from the tick (movers) are moved with the server's own formula: exact.
2. Replicated bodies near the own character (a few metres + what pending inputs and thrown bodies
   can cover): either kinematic stand-ins with `category = kCategoryDynamic` (the controller treats
   the category, not the body type, as "dynamic": same pushes, same box-on-head rule), or local
   dynamic bodies restarted from every snapshot with the server's velocity / density / friction and
   sleep state, pushed by the own character, then `Scene::step`. The bodies drawn stay the server's.
3. Other characters as `createCharacter` proxies, stepped with `stepCharacter` on a guessed input
   (keep moving / turning as in the snapshot) so their pushes act on the local bodies.
4. Timing: the replay must meet each input's server tick. Apply one input per tick on the server from
   a short buffer and replicate the last applied sequence with the owner's state; snapshot S with last
   applied L puts input L + j on the world of tick S + j - 1. Rewind the world to S before the replay,
   advance one tick per predicted step.
5. Smooth corrections (`net/Smoothing.h`); give the camera a slower smoother than the drawn character.

What it buys (place, SimNet 4g): platform rider 70 % -> 0 % of reconciles over 1 cm; one character
pushing piles 1.7 %; 15 bots crowding the same piles 16.7 % -> ~16 % but the mean correction halves
(5.9 -> 2.5-3 cm): other players' future inputs stay unknown. Going further needs network ownership.

## Verify

```sh
cmake -S . -B build/.temp/place -G Ninja -DCMAKE_BUILD_TYPE=Release -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/.temp/place --target aiverse_physics_scene_tests aiverse_physics_tests aiverse_place_tests
build/.temp/place/aiverse_physics_scene_tests examples/physics/scene.json          # CTest physics_scene
build/.temp/place/aiverse_physics_tests examples/physics/scene.json                # CTest physics_runtime
build/.temp/place/aiverse_place_tests examples/place/scene.json                    # CTest place
build/.temp/place/aiverse_place load --clients 16 --seconds 30 --profile 4g --late 20
```
