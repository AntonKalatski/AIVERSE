# Replica: generic server-authoritative networking

Status: **core done** (`agent-replica`): schema, quantization, delta snapshots, interest, budget, input,
reliable fields, remote messages, interpolation, prediction helper, tests and benchmark. Second round
(`agent-replica-2`): change versions + incremental relevance (unchanged objects cost nothing), 3D interest,
render clock with rate slewing and adaptive delay, object generations on id reuse, delta-coded numbers, a
create-confirmation fix. Repository skill: `skills/network/aiverse-replica/SKILL.md`. Physics
(`agent-replica-physics`) and the sandbox app (`agent-place`, first milestone done) are separate branches.
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
- **Interest**: a uniform x/y/z grid (`cellSize`, `interest3d`) of object positions (`positionField`); a client
  gets objects within the class radius (a sphere) of any of its viewers, plus `alwaysRelevant` ones, plus what it
  owns. `setVisibleTo` overrides per client (hidden information). Incremental: only changed objects are re-tested
  until a viewer moves.
- **Budget**: `bytesPerSecond` per client. Candidates are sorted by accumulated priority
  (class priority × (1 + snapshots skipped)), distance-weighted; what does not fit waits for the next one.
- **Input**: opaque frames with a sequence number, sent with redundancy (last N frames per packet);
  the server delivers them in order, at most `maxInputsPerTick`. Each snapshot carries the last applied
  sequence for that client.
- **Interpolation**: remote objects are drawn at render time = estimated newest server time − delay
  (`interpolationDelayMs`, or adaptive to jitter and loss); the render clock changes its rate by at most ±5 %
  instead of jumping. `Quat` slerp, `Vec3`/`Float` lerp; no extrapolation beyond `extrapolateMs`.
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
precision cost nothing (and do not even wake the object up for any client). Relevance is a 3D sphere of the
class radius around each viewer (Y up); `interest3d = false` keeps x/z grid columns (same result, more distance
tests in tall worlds).

## Wire format

Every message starts with a type byte. Reliable messages use `Conn::send`, snapshots and input `Conn::sendLatest`
(UDP latest channel: only the newest survives; ordered transports deliver all of them).

| Message | Dir | Path | Content |
|---|---|---|---|
| Hello | c→s | reliable | magic, `kProtocol` (2), schema hash |
| Welcome / Reject | s→c | reliable | slot, tick rate, snapshot rate / reason (protocol, schema, full, timeout, kicked) |
| Input | c→s | latest | newest snapshot seq + 32 ack bits, up to 32 input frames (first seq, u16 size + bytes each) |
| Message / Remote | both | reliable | channel u16, u32 size + payload |
| Field | s→c | reliable | object id, class, generation u8, field, value (String / Bytes / `reliable` fields) |
| Forget | s→c | reliable | object id, generation u8: its removal was confirmed, drop that generation's reliable values |
| Snapshot | s→c | latest | bit stream, below |

Snapshot bit stream: `seq:32 tick:32 lastAppliedInput:32`, removed ids (count, delta-coded), then entries sorted by
id: id delta, create flag, then either `class:classBits` + `generation:8` + every snapshot field + owner, or a
field mask (`fields + 1` bits, the last bit is the owner) + the masked fields. In a mask entry every Int / Float /
Vec3 field of 8+ bits starts with a delta flag: 1 = `width:5` + per component `zigzag(q - confirmed)` in `width`
bits, 0 = the absolute value. Numbers use `6 + width` bit varints.
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
RTT later. An object is written as a full create until any snapshot carrying its create is confirmed (a 32-bit
mask remembers which ones did: re-sending a create on change no longer postpones its confirmation); a removal is
repeated until confirmed, then the server sends Forget and stops tracking. Object ids return to a FIFO free list
only when no client tracks the object any more (every client confirmed its removal or left); every create bumps
the id's 8-bit generation.

**Delta-coded numbers.** When the field's last send is confirmed and no newer send exists, the client holds
exactly that quantized value, so the server may write the difference (5-bit width + zigzag per component) when
it is smaller. A position moving 6 u/s at 0.01 and 20 snapshots/s: 26 bits instead of 58. It applies when the
RTT is below the snapshot interval (LAN), to objects that change every few snapshots, and to objects the budget
starves (their last send was long ago and is confirmed); a steadily moving object on a link with RTT above the
interval goes absolute (its last send is always still in flight).

**Change versions.** Every quantized change stamps the object and the field with the broadcast epoch. A track
remembers the epoch at which the client's copy equalled the object (`syncVer`) and whether every send is
confirmed (`settled`); a settled track whose object has no newer version is not looked at. Per client a work
list holds what may need sending (changed, unconfirmed, starved); a leaving list holds removals in progress.

### Interest, priority, budget

A uniform grid (`cellSize`; x/y/z with `interest3d`, the default, else x/z columns) is built at most once per
broadcast, only when a client needs a full pass; cells persist between broadcasts and the probed y layers are
clamped to the occupied ones (a flat world probes one). A client's relevant set: objects within the class
`radius` (else `defaultRadius`) of any of its viewers (up to 16), with 15 % hysteresis before an object leaves;
`alwaysRelevant` classes and classes without a `positionField`; objects it owns; `setVisibleTo` on = forced in,
off = forced out (also for owned objects), `resetVisibility` returns to interest.

Relevance is incremental. `create`, `destroy`, a quantized change, `setOwner` and visibility changes append the
id (once per epoch) to a change log; each snapshot re-tests only the logged ids for that client. A full grid pass
runs when the client is new, a viewer moved more than 3.75 % of the smallest class radius since the last full
pass (`setViewers` with the same points costs nothing), the log since the client's last snapshot is longer than
what it tracks, or the client fell behind the trimmed log. Between full passes a moving viewer's relevance
is late by at most that slack; changed objects are always exact.

Each snapshot, every relevant object with something to send adds `priority / (1 + distance / radius)` to its
accumulator; candidates go in accumulator order (owned objects first, they may use the whole
`maxSnapshotBytes`) while they fit a token bucket of `bytesPerSecond` (burst: two snapshots' worth); sent ones
reset to zero, the rest wait and rise. A few candidates (the budget likely fits all) are evaluated in work order;
many (budget-bound) go through a heap that evaluates fields only for what it pops and stops when the smallest
entry seen cannot fit or after 32 misses in a row. `ServerStats::starved` counts candidates that waited.

### Input

The client sends one packet per poll with the last `inputRedundancy` frames (more if several were queued).
The server delivers frames strictly in sequence order, at most `maxInputsPerTick` per poll; a missing frame is
skipped after `inputGapTicks` polls of waiting (frames older than the next expected one are dropped); frames
more than 64 ahead are rejected unless they start a stream the server never saw. Every snapshot carries the
last delivered sequence for that client (`Client::lastAppliedInput`, `Predictor::reconcile`).

### Client side

Snapshots are parsed completely before anything is applied: a malformed one is dropped (`ClientStats::dropped`).
Each object keeps a short history (≤ 32 samples) stamped with server time (`tick * 1000 / tickRate`) and its
quantized slots (delta baselines). `interpolate` Float / Vec3 lerp and Quat slerp between the two samples around
render time; other fields step. An object absent from snapshots (unchanged or starved) holds its value until one
snapshot interval before its next sample. When snapshots stop, objects in the newest snapshot extrapolate along
their last velocity for at most `extrapolateMs`, then hold. Reliable values live in a per-(id, generation) store
until the Forget of that generation: a create of a newer generation never shows the previous object's reliable
fields, even when its Forget is still stuck in the reliable stream.

**Render clock.** Every snapshot gives a transit sample (arrival − server time of its tick). A leaky minimum of
them (a lower sample resets it, else it rises 4 % of elapsed time) is the fastest path: newest server time = now
− that minimum (`serverTimeMs()` adds RTT / 2). Render time (`renderTimeMs()`, `renderTick()`) chases newest
server time − delay with rate `1 + clamp(Kp·error + Ki·∫error, ±maxClockAdjust)` (5 %; critically damped, ~2 s):
monotonic, no jumps, and no steady error against a server clock running a few % slow; a target more than
`clockSnapMs` (1 s) away is taken at once (`ClientStats::clockSnaps`). `jitterMs()` is the RFC 3550 inter-arrival
jitter of the transit samples, `snapshotLoss()` the recent share of missing snapshot sequence numbers. With
`adaptiveDelay` the delay follows `(1.5 + min(1, 25 × loss)) × interval + 4 × jitter` clamped to
[`minDelayMs` (0: one interval), `maxDelayMs`], smoothed over ~1 s. `ClientStats::underruns` / `underrunMs`
count render time passing the newest snapshot.

## Hostile peers

All decoders bound every count and size before allocating (Input ≤ 32 frames of `maxInputBytes`, messages
`maxMessageBytes` and `maxMessagesPerSecond` with a 2 × burst, strings 4 KiB, object ids < 2^24, lists < 65536),
stop at the first malformed byte and drop the packet; acks of snapshots never sent are ignored. A client never
names an object or a slot: inputs and messages carry the connection's slot. Tested with random bytes, truncated /
bit-flipped / extended real sessions, huge declared sizes, under ASan + UBSan.

## Numbers

`aiverse_replica_tests --bench [near|crowded|latency|all|rest|still]`: 64 loopback clients, 2000 objects
(Vec3 ±2048 @ 0.01 + 10-bit Quat each; moving ones change every tick), 60 Hz ticks, 20 snapshots/s, default
48 KB/s budget; Release, g++ 11, one core of the 4-core Linux aarch64 VM; clients look from moving objects
spread over a 2000 × 2000 area (those 64 objects always move). Before = `main` at e1445cf5 built with the same
bench driver; after = `agent-replica-2`.

| Case | In view | Broadcast per tick, before → after (avg / worst) | Bytes per client per second, before → after | Object updates / s per client (after) |
|---|---|---|---|---|
| radius 128, all moving | 29 | 0.09 / 0.27 → 0.13 / 0.30 ms | 7.5 → 5.0 KB (delta-coded positions) | 569, 8.8 B each |
| radius 128, all moving, 50 ms latency | 29 | 0.09 / 0.23 → 0.10 / 0.21 ms | 8.4 → 7.5 KB (create fix; no deltas: RTT > interval) | 567, 13.1 B each |
| radius 400, all moving | 216 | 0.62 / 0.91 → 0.63 / 0.95 ms | 44.5 (budget-bound, 428 k starved) → 33.7 KB (not bound, 462 starved) | 4207, 8.0 B each |
| everything relevant, all moving | 2000 | 2.98 / 3.67 → 2.08 / 2.57 ms | 47.9 → 47.9 KB (budget-bound) | 5227 (~3000 before), 9.2 B each |
| everything relevant, 13 % moving | 2000 | 1.29 / 1.64 → 0.63 / 0.86 ms | 47.9 (bound) → 39.8 KB (not bound) | 5060, 8.0 B each |
| everything relevant, 4 % moving | 2000 | 0.97 / 1.25 → 0.22 / 0.53 ms | 20.7 → 13.7 KB | 1763, 8.8 B each |

At rest an object costs nothing per client: `unchanged_objects_cost_nothing` (1000 relevant objects at rest:
0 evaluations, 0 distance tests over 40 snapshots; 10 moving: 10.0 evaluations per snapshot). In the 4 % case
the server looks at 4.9 % of the relevant objects per snapshot; what remains is the per-update cost (budget
selection, writing) of the objects that move. Client decode for 64 clients: 0.08 / 0.73 / 2.7 / 1.1 / 0.39 ms per
tick (unchanged except the budget-bound case, which now carries 1.7 × more updates per byte).

3D interest (`interest_3d_towers_and_floors`: two towers of 20 floors × 40 objects, radius 30, floors 40 apart,
a viewer climbing 9 floors one unit per tick, then crossing to the other tower): 10 360 distance tests with `interest3d`, 96 800 with x/z columns,
same relevant sets.

Render clock (`render_clock_*` tests), rate always within 0.96-1.03, render time never decreasing:

| Link | Delay | Jitter estimate | Underruns in 90 s (time in underrun) |
|---|---|---|---|
| loopback 40 + 0-60 ms (ordered) | 100 ms | 20.9 ms | 0 in 30 s; server time estimate within 27 ms |
| SimNet 4g, 3 seeds × 30 s | fixed 100 ms | 13.7 ms | 34 (2.2 %) |
| SimNet 4g | adaptive → 177 ms | 13.7 ms | 8 (0.4 %) |
| SimNet bad (20 % loss) | fixed 100 ms | 13.7 ms | 171 (10.4 %) |
| SimNet bad | adaptive → 182 ms | 13.7 ms | 34 (1.9 %) |
| loopback 30 ms, no jitter | adaptive → 75 ms | | 0 |

A server ticking 3 % slower than real time: newest server time − render time stays 99-102 ms (delay 100) over
60 s, no clock snaps (before: the offset followed the fastest snapshot and drifted, render time jumped forward on
every new minimum).

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
- Broadcast cost is O(changed + unconfirmed objects) per due client, plus a full grid pass when a viewer moved
  past the slack; a world where everything moves still costs O(relevant) per client (2 ms per tick for 2000 all
  relevant to 64 clients, most of it budget selection and writing of what fits).
- Relevance of objects at rest lags a moving viewer by at most 3.75 % of the smallest class radius.
- Delta-coded numbers need a confirmed last send: a steadily moving object over a link with RTT above the
  snapshot interval (4g at 20 snapshots/s, the place sandbox) goes absolute. A per-field baseline history (delta
  against the newest confirmed of the last N sends) would cover it, at ~100 bytes per track and field.
- No entropy coding, no delta against prediction (velocity), Quat fields always absolute.
- Generations are 8 bits: a client whose reliable stream stalls across 256 reuses of one id could mix them.
- The adaptive delay trades latency for fewer underruns (4g: 177 ms); `maxDelayMs` caps it.

## Header changes (agent-replica, from the original contract)

- `ServerOptions::inputGapTicks` (skip a missing input frame after waiting), `maxMessagesPerSecond` (remote
  message rate limit); `maxInputsPerTick` frames above the cap wait, they are not dropped at once.
- `Server::resetVisibility(id, slot)` (there was no way back from `setVisibleTo`), `Server::ownerOf(id)`.
- `ClientOptions::inputRedundancy` default 3 → 5 (measured above); `extrapolateMs` documented as "extrapolate
  only while snapshots are missing, then hold" (extrapolating objects that merely stopped overshoots).
- `Server::set` documented: stores the value as given, compares quantized.

## Header changes (agent-replica-2)

Backward compatible at the source level (`apps/place` builds and passes unchanged); the wire is `kProtocol` 2.
- `ServerOptions::interest3d` (default true).
- `ServerStats`: `entries`, `creates`, `deltaFields`, `evaluated`, `relevant`, `interestChecks`.
- `ClientOptions`: `maxClockAdjust`, `clockSnapMs`, `adaptiveDelay`, `minDelayMs`, `maxDelayMs`.
- `ClientStats`: `underruns`, `underrunMs`, `clockSnaps`.
- `Client::serverTimeMs()`, `renderTimeMs()`, `renderTick()`, `jitterMs()`, `snapshotLoss()`,
  `interpolationDelayMs()`, `rttMs()`.

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

Status: **done** (`agent-place`). `apps/place` (library `aiverse_place`, executable `aiverse_place`),
`examples/place/scene.json` (generated by `make_scene.py`), tests `tests/place` (CTest `place`).

```sh
# build (needs physics; the graphical client comes with desktop builds, like the FPS client / physics demo)
cmake -S . -B build/.temp/place -G Ninja -DCMAKE_BUILD_TYPE=Release -DAIVERSE_ENABLE_PHYSICS=ON   # Mac: + -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/.temp/place --target aiverse_place_app aiverse_place_tests
ctest --test-dir build/.temp/place -R '^place$' --output-on-failure
# play on one machine, dev mode (run from the repository root; production hosting: § Place hosting below)
build/.temp/place/aiverse_place serve --scene examples/place/scene.json --port 27300 --insecure [--ws-port 27301]
build/.temp/place/aiverse_place client --connect 127.0.0.1:27300 --insecure [--player me] [--lang ru]   # more windows = more players
build/.temp/place/aiverse_place bots --connect 127.0.0.1:27300 --insecure --count 8 --seconds 120
build/.temp/place/aiverse_place load --clients 16 --seconds 30 --profile 4g --late 20   # one process, SimNet
```

Client controls: click to capture the mouse, WASD, mouse look, Space jump, Shift run, E push, F spawn a box,
wheel zoom, Esc releases the mouse. The HUD shows RTT (application ping, includes the 60 Hz tick wait on both
sides), snapshots per second, incoming KB/s and the prediction correction (last / mean / count over 1 cm).

**How it works.** The server loads the scene JSON into a `physics::Scene`, replicates every non-static body
(class `body`) and one character per client (class `character`, owned by the client) plus a `game` object.
Each input frame (6 bytes: `CharacterInput` + action bits) steps that client's character once (`stepCharacter`,
`applyPushes`); after the inputs come kinematic movers, `scene.step()`, and `set()` of what changed: bodies
that sleep and slept the tick before are skipped. Viewers are the characters. On join the server sends the
static part of the scene (channel 1); the client builds `Scene::staticCopy` from it, draws the static
bodies from it and predicts its own character with the same `stepCharacter` (`Predictor`), reconciling on
each snapshot; corrections are smoothed (`net/Smoothing.h`, 50 ms half-life, snap above 3 m). Remote bodies
and characters are drawn from `sample()`. Push (E): an input bit; the server applies a mass-scaled impulse to
dynamic bodies in a sphere in front of the character (cooldown). Spawn box (F): remote message (channel 2),
token bucket per player (`perSecond`, `burst`), at most `maxPerPlayer` live boxes per player (the oldest is
removed), refusals answered on channel 3. A client that sends no input for a second is stepped with neutral
input (gravity, platforms). Bodies below `killY` return to their start (spawned ones are removed), characters
respawn.

Scene: the physics format (docs/physics.md) plus an optional `"place"` block: `spawns` (points), `relevance`
(replica radius, m), `killY`, `movers` (`{"body", "offset": [x,y,z], "period"}`: a kinematic body shuttles
between its position and position + offset), `spawnBox` (`size`, `perSecond`, `burst`, `maxPerPlayer`,
`maxBodies`), `push` (`speed`, `radius`, `reach`, `maxMass`, `cooldownTicks`). Errors name the key.

**Numbers** (Linux aarch64 VM, 4 cores, g++ 11 Release, one physics worker; indicative, the Mac is faster).
Example scene: 500 dynamic bodies + platform, bots pushing and spawning (~20-70 boxes added).

| Run | Server tick avg / worst | Received per client | Convergence (resting bodies) | Late join |
|---|---|---|---|---|
| test: 16 clients 4g 30 s (15 bots + probe), late join at 20 s | 0.52 / 1.3 ms | 26.6 KB/s | 7 mm worst, 0 missing | same objects, 7 mm |
| `load --clients 16 --seconds 30` | 0.47 / 1.2 ms | 28.7 KB/s | 7 mm | same objects |
| `load --clients 64 --seconds 20` | 1.5 / 2.6 ms | 35.3 KB/s | 8 mm | same objects |
| real UDP on localhost: `serve` + `bots --count 8` | 0.4-1.0 / 2.7 ms | 29-35 KB/s | | |

Resting-body error is the 0.01 m position quantization; bodies still moving after the bots stop (rolling
spheres) differ by latency (mean 3-4 cm). Prediction: a steady walker on static ground over 4g: 0 corrections
over 1 cm in 30 s, mean 2.8 mm per reconcile (position / velocity quantization); same on a 60 ± 20 ms
loopback for 15 s with runs and jumps (max 5 mm). Bots that push piles all the time: 17 % of reconciles over
1 cm (mean 5 cm), see limits. Example scene with nobody in it: settles and sleeps (0.015 ms per tick).

Two fixes in the layers below came out of this milestone (separate commits for review):
- replica: owned objects resend every field the client has not confirmed (`ReplicaServer.cpp`). Before, a lost
  snapshot followed by an unchanged value left a stale owned state next to a newer `lastAppliedInput`, so the
  predictor rewound several steps and jumped back a snapshot later (test
  `owned_state_matches_last_applied_input_under_loss`).
- physics: `stepCharacter` ignores downward planes of dynamic bodies; a box that landed on a head (proxies have no
  body contacts) pressed the character through the ground (test in `physics_scene`).

Limits: the client predicts against static geometry only: walking into boxes, other characters or onto the
moving platform is corrected by reconcile (smoothed, visible as a short slide when pushing piles); a server
that skips a lost input frame (all redundant copies lost) costs one corrected step; the client uses input
redundancy 8 (~4 KB/s up); interest is 3D (`interest3d`, on by default). (Tickets, encryption, reconnect and the frame-rate bound
snapshot rate are solved in § Place hosting.) Verified on Linux (tests, load, real UDP, client rendered under Xvfb + Mesa);
macOS / Windows client builds, the WebSocket path and web / mobile are not verified. Not built for the web
(SecureUdp and listeners are native).

## Place hosting (agent-place-secure)

Status: **done** for one server process. Code: `apps/place/PlaceAdmission.*` (tickets, `PlaceGate`),
`PlaceServer::attach(conn, player)` (reconnect window, newest wins), `apps/place/PlaceLink.*` (client connection
with reconnect), `apps/place/main.cpp` (`serve`, `ticket`, `keygen`, `client`, `bots`). Tests: CTest `place_secure`
(`tests/place/place_secure_tests.cpp`). Recipe: `skills/network/aiverse-place-server`.

```sh
# build: as above (Mac: cmake ... -DAIVERSE_ENABLE_PHYSICS=ON -DCMAKE_OSX_ARCHITECTURES=arm64; the client opens a window)
P=build/.temp/place/aiverse_place
$P keygen > place.key && chmod 600 place.key                      # 32-byte server key, hex; keep it off the command line
$P serve --scene examples/place/scene.json --key-file place.key --port 27300 --ws-port 27301 --reconnect-seconds 30
$P ticket --key-file place.key --player alice --seconds 3600 --server game.example.com:27300 > alice.ticket
$P client --ticket-file alice.ticket                               # encrypted UDP to the ticket's server
$P client --connect ws://127.0.0.1:27301 --ticket-file alice.ticket # WebSocket (wss:// through a TLS proxy)
$P bots --connect 127.0.0.1:27300 --key-file place.key --count 8 --seconds 120 --drop-at 20 --drop-count 2
$P bots --connect 127.0.0.1:27300 --tickets tickets.txt --count 8  # one ticket per line, issued elsewhere
```

| Flag | Meaning |
|---|---|
| `serve --key HEX64` / `--key-file F` | tickets required; UDP is `SecureUdp` (encrypted, authenticated) |
| `serve --insecure` | plain UDP; without a key everyone enters as a guest (`guest-<name>`); dev only, logged as a warning |
| `serve --ws-port N` | also accept WebSocket (ws://); tickets go through the PlaceGate |
| `serve --reconnect-seconds S` | a dropped player's character waits this long (default 30; 0 = leaves at once) |
| `client` / `bots --ticket T`, `--ticket-file F`, `--tickets F` | the player's ticket (`aivt1.` text, a secret) |
| `client --player NAME`, `bots --prefix P` | guest name for a keyless `--insecure` server |
| `client --suspend-on-focus-loss` | app lifecycle like battles (mobile builds) |
| `bots --key-file F` | test convenience: bots issue their own tickets `<prefix>-<n>` (never give clients the key) |
| `bots --drop-at S --drop-count K` | drop K bots' links at S s: they reconnect and get their characters back |

`serve` without `--key` and without `--insecure` refuses to start.

**Security model.**
- Identity is the ticket: `net::MatchTicket` (player id, expiry, session keys) sealed with the 32-byte server key
  (XChaCha20-Poly1305), match id `kPlaceMatch`. A login service that shares the key issues them; `aiverse_place
  ticket` is the test issuer. The expiry also bounds reconnects with the ticket: give it the session length.
- UDP: `SecureListener` opens the ticket, the client proves it holds the keys, then everything is encrypted with
  per-handshake keys and a replay window; NAT rebinding moves the session (docs/battle-net.md § Secure transport).
  Rejected tickets get no answer (no oracle): such a client just keeps retrying with backoff.
- WebSocket / plain UDP: `PlaceGate` runs on the raw stream before the replica sees it. Hello = sealed part (+ guest
  name); the server opens it and sends a random 32-byte challenge; the client answers `BLAKE2b(c2s key, label |
  challenge)`. A copied sealed part alone gets nobody in, but the stream itself is not encrypted: on the Internet
  terminate TLS in a proxy (nginx / Caddy `wss://` -> `ws://127.0.0.1:N`). Rejections carry a reason
  (invalid / expired ticket, ticket needed, wrong server, rate limit, malformed hello, timeout).
- Limits: hellos 2/s per source address (burst 6), at most 256 pending, 5 s to finish; SecureUdp handshakes 1/s
  per player id (burst 5), 256 half-open; replica limits for inputs and messages as before.
- Same player twice: **newest wins**. The older connection gets `kNoticeReplaced` (its client stops and says why)
  and is kicked 0.5 s later; the character moves to the new connection. Over SecureUdp the listener closes the
  older session silently (the notice cannot reach it), so a ticket used on two devices would bounce: after 3
  takeovers in 60 s the id is refused for 60 s (`PlaceServer::admits`) and the current holder keeps playing.

**Reconnect.** When a connection drops (bye, silence 10 s on SecureUdp, a kick), the character stays for
`--reconnect-seconds`: nobody owns it, it receives no input (after a second it is stepped with neutral input:
it stands, falls, rides platforms), its spawned boxes stay. The same player id attaching again gets the same
replica object: `setOwner(object, newSlot)` with the kept position and velocity, the scene again, and
`kNoticeResumed`. After the window the character is removed. The client (`PlaceLink`) treats closed, rejected,
kicked or 4 s without snapshots as lost, draws the last world under a "reconnecting" line and dials again after
0.5, 1, 2, 4, 8 s (+-25 % jitter); `suspend()` / `resume()` follow battles (after more than 3 s away resume dials
at once). The server logs `player X dropped, character kept 30 s at (...)` / `back on slot N, same character`.

**Client polling.** The graphical client runs the network on its own thread: poll every 2 ms, input and
prediction at the scene tick rate, independent of the frame rate (the latest channel keeps only the newest
packet between polls, so one poll per frame lost snapshots at low FPS). The HUD shows RTT as the best ping of
the last 5 s and the raw ping, which includes waiting for the server's 60 Hz tick (up to 17 ms) because the
server reads messages once per tick.

**Measured** (Linux aarch64 VM, Release): `serve --key-file` + 8 bots with tickets over SecureUdp + 1 over
WebSocket + a bot killed with `kill -9` and restarted 6 s later: all 20.0 snapshots/s, ping 5-18 ms, server
tick 0.3-1.3 ms avg with 10 players; `--drop-at 20 --drop-count 2` and the restarted bot came back to the same
characters (`resumed 3`), a forged and an expired ticket over WebSocket were refused (`bad ticket 2`); a bot
that never came back was removed after 30 s. Graphical client under Xvfb at ~6 FPS (software GL): 20
snapshots/s on the HUD; server frozen (SIGSTOP) for 12 s: "Connection lost. Reconnecting...", then
"Reconnected: your character was kept". Tests: SimNet 10 s blackout -> same character, walking on from where
it stood; reconnect window expiry; ticket reuse; suspend / resume; native `ws://` client end to end.

**Still missing.** Orchestration (allocating place servers, health checks, draining, restarts: one process,
no persistence of the world or the kept characters across a restart), accounts / login service issuing tickets
(the key is shared out of band; `aiverse_place ticket` is a test tool), a WebSocket-over-TLS listener in the
engine (use a proxy), browser clients (the place is not built for the web), protection of plain ws:// against an
on-path attacker (who can read and take over the stream after admission; TLS solves it), per-account rate limits across servers, spectators. macOS / Windows
client builds and real mobile suspend are not verified here.

## Later

Lua server scripts and client local scripts with remote events (sandboxed, CPU/memory limits); character
models with animation parameters as fields; place server orchestration (tickets and the process: § Place hosting); editor template + "play with N clients"; network ownership of physics bodies near a
client; moving the FPS and city hosts onto replica.
