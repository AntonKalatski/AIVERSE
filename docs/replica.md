# Replica: generic server-authoritative networking

Status: **core done** (`agent-replica`): schema, quantization, delta snapshots, interest, budget, input,
reliable fields, remote messages, interpolation, prediction helper, tests and benchmark. Physics
(`agent-replica-physics`) and the sandbox app (`agent-place`) are separate branches.
Header contract: `engine/net/replica/Replica.h`; library `aiverse_net_replica` (links only `aiverse_net`: no
game, physics, RTS or FPS code); tests `aiverse_replica_tests` (`tests/replica`, `--bench` for the benchmark).

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

## Using it

```cpp
Schema schema;                                   // both sides build the same one (hash checked in the hello)
ClassId ball = schema.add({"ball", {{"pos", FieldKind::Vec3, -1024, 1024, 0.01, 0, false, true},
                                    {"rot", FieldKind::Quat, 0, 0, 0, 0, false, true},
                                    {"name", FieldKind::String}}, 1.0f, 0, false, 0});
// server, every tick
server.poll(now);        // hello, acks, onInput (in order), onMessage
/* step the world, server.set(id, field, value), server.setViewers(slot, {...}) */
server.tick();
server.broadcast(now);   // reliable field changes now, snapshots to clients that are due
// client, every frame
client.poll(now);
client.sendInput(frame);                          // returns its sequence number (Predictor::local)
Value p = client.sample(id, 0);                   // interpolated, interpolationDelayMs in the past
```

`set()` stores the value as given (`get()` returns it); what travels is its quantized form, so changes below the
precision cost nothing. `positionField` positions are on the x/z plane for the interest grid (Y up); distance
is 3D.

## Wire format

Every message starts with a type byte. Reliable messages use `Conn::send`, snapshots and input `Conn::sendLatest`
(UDP latest channel: only the newest survives; ordered transports deliver all of them).

| Message | Dir | Path | Content |
|---|---|---|---|
| Hello | c→s | reliable | magic, `kProtocol`, schema hash |
| Welcome / Reject | s→c | reliable | slot, tick rate, snapshot rate / reason (protocol, schema, full, timeout, kicked) |
| Input | c→s | latest | newest snapshot seq + 32 ack bits, up to 32 input frames (first seq, u16 size + bytes each) |
| Message / Remote | both | reliable | channel u16, u32 size + payload |
| Field | s→c | reliable | object id, class, field, value (String / Bytes / `reliable` fields) |
| Forget | s→c | reliable | object id: its removal was confirmed, drop its reliable values |
| Snapshot | s→c | latest | bit stream, below |

Snapshot bit stream: `seq:32 tick:32 lastAppliedInput:32`, removed ids (count, delta-coded), then entries sorted by
id: id delta, create flag, then either `class:classBits` + every snapshot field + owner, or a field mask
(`fields + 1` bits, the last bit is the owner) + the masked fields. Numbers use `6 + width` bit varints.
Quantization: Bool 1 bit; Int `width(max - min)` bits (min = max = 0: 32-bit signed); Float / Vec3 axes
`width(ceil((max - min) / precision))` bits, clamped, non-finite → min; Quat smallest-three (2 bits index +
3 × `bits`, default 12 ≈ 0.1° worst case); owner 8 bits. Example: a Vec3 in ±2048 at 0.01 (19 bits per axis) +
a 12-bit Quat ≈ 13 bytes per moving object.

### Deltas and acks

Each client acks the newest snapshot it applied plus a 32-bit field of the ones before it (on every input
packet; an ack-only packet goes out at least every 50 ms). The server remembers, per client, object and field,
the last value it sent and in which snapshot. That send resolves lazily to confirmed (acked), lost (a newer one
was acked, or it left the 32-snapshot history) or in flight. A field is written when its quantized value differs
from the last send or that send was lost; values are absolute, so the client simply overwrites in sequence
order and drops older snapshots. Unchanged fields in flight are not repeated: a lost one is repaired about one
RTT later. An object is written as a full create until a snapshot carrying its create is confirmed; a removal is
repeated until confirmed, then the server sends Forget and stops tracking. Object ids return to a FIFO free list
only when no client tracks the object any more (every client confirmed its removal or left).

### Interest, priority, budget

A uniform x/z grid (`cellSize`) is rebuilt on broadcasts that send something. A client's relevant set: objects
within the class `radius` (else `defaultRadius`) of any of its viewers (up to 16), with 15 % hysteresis before an
object leaves; `alwaysRelevant` classes and classes without a `positionField`; objects it owns; `setVisibleTo`
on = forced in, off = forced out (also for owned objects), `resetVisibility` returns to interest.
Each snapshot, every relevant object with something to send adds `priority / (1 + distance / radius)` to its
accumulator; candidates go in accumulator order (owned objects first, they may use the whole
`maxSnapshotBytes`) while they fit a token bucket of `bytesPerSecond` (burst: two snapshots' worth); sent ones
reset to zero, the rest wait and rise. `ServerStats::starved` counts candidates that waited.

### Input

The client sends one packet per poll with the last `inputRedundancy` frames (more if several were queued).
The server delivers frames strictly in sequence order, at most `maxInputsPerTick` per poll; a missing frame is
skipped after `inputGapTicks` polls of waiting (frames older than the next expected one are dropped); frames
more than 64 ahead are rejected unless they start a stream the server never saw. Every snapshot carries the
last delivered sequence for that client (`Client::lastAppliedInput`, `Predictor::reconcile`).

### Client side

Snapshots are parsed completely before anything is applied: a malformed one is dropped (`ClientStats::dropped`).
Each object keeps a short history (≤ 32 samples) stamped with server time (`tick * 1000 / tickRate`). Render time
= local time − clock offset − `interpolationDelayMs`; the offset follows the fastest snapshot (minimum latency)
and drifts slowly upward. `interpolate` Float / Vec3 lerp and Quat slerp between the two samples around render
time; other fields step. An object absent from snapshots (unchanged or starved) holds its value until one
snapshot interval before its next sample. When snapshots stop, objects in the newest snapshot extrapolate along
their last velocity for at most `extrapolateMs`, then hold. Reliable values live in a per-id store until Forget,
so an object that leaves and comes back keeps its name without a resend.

## Hostile peers

All decoders bound every count and size before allocating (Input ≤ 32 frames of `maxInputBytes`, messages
`maxMessageBytes` and `maxMessagesPerSecond` with a 2 × burst, strings 4 KiB, object ids < 2^24, lists < 65536),
stop at the first malformed byte and drop the packet; acks of snapshots never sent are ignored. A client never
names an object or a slot: inputs and messages carry the connection's slot. Tested with random bytes, truncated /
bit-flipped / extended real sessions, huge declared sizes, under ASan + UBSan.

## Numbers

`aiverse_replica_tests --bench`: 64 loopback clients, 2000 objects moving (Vec3 ±2048 @ 0.01 + 10-bit Quat each,
all changing every tick), 60 Hz ticks, 20 snapshots/s, default 48 KB/s budget; Release, g++ 11, one core of the
4-core Linux VM; clients look from moving objects spread over a 2000 × 2000 area.

| Case | Objects in view | Server broadcast per tick (avg / worst) | Bytes per client per second | Client decode (64 clients) |
|---|---|---|---|---|
| radius 128 | 29 | 0.10 / 0.20 ms | 7.5 KB | 0.08 ms/tick |
| radius 400 | 216 | 0.66 / 1.8 ms | 44.5 KB | 0.74 ms/tick |
| everything relevant | 2000 | 3.2 / 6.5 ms | 47.9 KB (budget-bound, starvation-rotated) | 2.2 ms/tick |

Input over the secure UDP transport on SimNet, 30 s of 60 Hz frames, 3 seeds, frames delivered (always in order):

| Preset | redundancy 1 | 3 | 5 (default) | 8 |
|---|---|---|---|---|
| 4g (3 % loss, bursts of 4, 40 ± 30 ms) | 66.1 % | 97.7 % | 99.8 % | 100 % |
| bad (20 % loss, bursts of 5, 75 ± 40 ms) | 53.3 % | 93.8 % | 98.3 % | 99.9 % |

Redundancy 1 is far below the raw loss because the latest channel keeps only the newest packet when two arrive
between server polls (jitter bunches them): redundancy is required, not optional.
Budget test: 300 moving objects at 8000 B/s → 8000 B/s used, every object updated at least every 550 ms.
Interpolation over 40 ± 20 ms: mean speed exact, worst single-frame speed error 27 % (0.01 quantization at
16 ms frames), no holds; Predictor corrections 0 except a server-side teleport (exactly one correction).

## Limits and follow-ups

- One snapshot per client per interval, capped at `maxSnapshotBytes` (16 KB; the latest channel limit is 128 KB).
  Removals per snapshot ≤ 4096, objects < 2^24, classes ≤ 65535, fields per class ≤ 63, viewers ≤ 16, slots 1..255.
- The interest grid is x/z only; vertical worlds (towers, space) get no culling on Y (distance is still 3D).
- Broadcast cost is O(relevant objects) per due client: thousands of objects all relevant to 64 clients cost
  ~3 ms per tick. Per-object dirty tracking (skip unchanged objects without comparing) is the next optimisation.
- Reliable field values after a reused id may show the old object's values until Forget arrives (ordering of
  the reliable channel vs snapshots); the client clears them when the class differs.
- No compression beyond quantization and deltas (no delta-of-position against prediction, no entropy coding).
- No client-side clock rate adjustment: render time follows the fastest snapshot; a server that ticks slower than
  real time slowly increases the delay.

## Header changes (agent-replica, from the original contract)

- `ServerOptions::inputGapTicks` (skip a missing input frame after waiting), `maxMessagesPerSecond` (remote
  message rate limit); `maxInputsPerTick` frames above the cap wait, they are not dropped at once.
- `Server::resetVisibility(id, slot)` (there was no way back from `setVisibleTo`), `Server::ownerOf(id)`.
- `ClientOptions::inputRedundancy` default 3 → 5 (measured above); `extrapolateMs` documented as "extrapolate
  only while snapshots are missing, then hold" (extrapolating objects that merely stopped overshoots).
- `Server::set` documented: stores the value as given, compares quantized.

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
