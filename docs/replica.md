# Replica: generic server-authoritative networking

Status: **in progress** (branches `agent-replica`, `agent-replica-physics`, later `agent-place`).
Header contract: `engine/net/replica/Replica.h`.

## Why a third model

| Model | Code | Who simulates | Good for |
|---|---|---|---|
| Lockstep + rollback | `engine/net/server`, `Rollback` | every client, deterministic Fixed | RTS, small PvP |
| Genre hosts | `engine/net/authority` (FPS actors), `engine/net/city` (RTS city mirror) | server | one genre each, fixed wire structs |
| **Replica** | `engine/net/replica` | server only (float OK: Box3D, Lua) | Roblox-like places: many players, physics, join any time, user scripts |

The FPS and city hosts stay as they are; they can move onto replica later.

## Model

- **Schema**: classes of objects with typed, quantized fields (`Bool`, `Int`, `Float`, `Vec3`, `Quat`,
  `String`, `Bytes`). Both sides build the same schema; its hash is checked in the hello.
- **Snapshots**: per client, `snapshotRate` per second over `Conn::sendLatest` (UDP latest channel; ordered
  transports fall back to reliable). Each one is a delta against the newest snapshot the client acked
  (acks ride on input packets); no ack yet = full state. Objects that left relevance or were destroyed are
  listed as removed. `String`/`Bytes`/`reliable` fields go through `Conn::send` on change.
- **Interest**: a uniform grid (`cellSize`) of object positions (`positionField`); a client gets objects
  within the class radius of any of its viewers, plus `alwaysRelevant` ones, plus what it owns.
  `setVisibleTo` overrides per client (hidden information).
- **Budget**: `bytesPerSecond` per client. Candidates are sorted by accumulated priority
  (class priority × (1 + snapshots skipped)), distance-weighted; what does not fit waits for the next one.
- **Input**: opaque frames with a sequence number, sent with redundancy (last N frames per packet);
  the server delivers them in order, at most `maxInputsPerTick`. Each snapshot carries the last applied
  sequence for that client.
- **Interpolation**: remote objects are drawn `interpolationDelayMs` in the past between two snapshots;
  `Quat` slerp, `Vec3`/`Float` lerp; no extrapolation beyond `extrapolateMs`.
- **Prediction**: `Predictor<State, Input>`: the owner runs the same step as the server (character
  controller), reconciles on each snapshot, the correction is smoothed with `net/Smoothing.h`.
- **Remote events**: `send(slot, channel, payload)` / `onMessage`, reliable, size- and rate-limited.

## Physics (agent-replica-physics, `engine/physics`)

`World` keeps its fixed manifest (save/restore). New, alongside it:
- `physics::Scene` (`engine/physics/Scene.{h,cpp}`): bodies created / destroyed at run time by a stable
  `uint32_t` handle; transform / velocity get / set; impulses; static geometry from JSON; raycast /
  overlap; sleeping flag; contact begin events.
- `physics::Character` (`engine/physics/Character.{h,cpp}`): kinematic capsule controller on
  `b3World_CastMover` / `b3World_CollideMover` / `b3SolvePlanes`: walk, run, jump, gravity, steps,
  slopes (`maxSlope`), moving platforms (ride the ground body velocity), pushes dynamic bodies with a bounded
  impulse. `stepCharacter(world, params, state, input, dt)` is a pure function of its arguments so the
  client can run it for prediction against a local copy of the static geometry.
- `CharacterInput` packs into a few bytes (`pack` / `unpack`, hostile input clamped) for replica input frames.

## First milestone: physics sandbox (agent-place)

`apps/place`: `aiverse_place serve` (Box3D scene + characters, replica server over UDP / WebSocket)
and `aiverse_place client` (renders boxes / spheres / capsules through the RHI, WASD + mouse, own character
predicted). `aiverse_place bots` drives N headless clients for load. Example scene: `examples/place`.
Done when: 16 clients over the SimNet "4g" profile push boxes around, remote motion is smooth, the own
character does not jitter, a client joining late sees the same world, server tick < 5 ms at 500 bodies.

## Later

Lua server scripts and client local scripts with remote events (sandboxed, CPU/memory limits); character
models with animation parameters as fields; place server process with tickets (`MatchTicket`, `SecureUdp`)
and orchestration; editor template + "play with N clients"; network ownership of physics bodies near a
client; moving the FPS and city hosts onto replica.
