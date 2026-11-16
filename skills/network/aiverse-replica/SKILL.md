---
name: aiverse-replica
description: Build a server-authoritative multiplayer host on AIVERSE replica (engine/net/replica) - schema and quantization design, interest radius / 3D grid / byte budget tuning, input frames with Predictor reconciliation, remote messages, render clock and adaptive delay, testing over loopback and SimNet - and decide between replica, rollback lockstep and the city / FPS hosts.
---

# Replica: server-authoritative objects for many players

This is an existing-engine reference recipe. Runtime library provided by this skill: none; link the engine target
**`aiverse_net_replica`** (depends only on `aiverse_net`). Contract: `engine/net/replica/Replica.h`. Behaviour,
wire format, numbers and limits: `docs/replica.md`. Tests: `tests/replica` (`aiverse_replica_tests`,
`--bench` for the load bench). A complete host: `apps/place` (physics sandbox; schema in
`apps/place/PlaceProtocol.cpp`, server loop in `apps/place/PlaceServer.cpp`, prediction in
`apps/place/PlaceClient.cpp`).

## Choose the model first

| Need | Use |
|---|---|
| RTS / small PvP, every client simulates deterministic `Fixed` state | lockstep + rollback: `engine/net/server`, `engine/net/Rollback.h`, `skills/network/aiverse-online-battles/SKILL.md` |
| Top-down city mirror of an RTS world | `engine/net/city`, `skills/network/aiverse-city-server/SKILL.md` |
| FPS actors with lag compensation | `engine/net/authority`, `docs/fps.md` |
| Many players, server-only float simulation (Box3D, Lua), join any time, objects of game-defined classes | **replica** |

Replica never runs game code on clients except the owner's prediction. Do not replicate RTS sim state with it.

## Design the schema

1. Both sides build the same `Schema` (hash checked in the hello; any field change is a protocol change). Keep
   the game's field indices as constants next to the schema and assert them (`schema.field(cls, "name")`).
2. Pick the quantization from what the player can see: positions `Vec3` with a range that covers the world and
   `precision` 0.01 (1 cm), rotations `Quat` with `bits` 10-12 (yaw-only characters: a `Float` angle or a Quat
   of yaw), counters `Int` with a tight `[min, max]` (bits come from the range), flags `Bool`. A change below
   `precision` costs nothing; a range wider than needed costs bits on every create.
3. `interpolate = true` only for fields drawn smoothly (pos, rot). Names, colours, models: `String` /
   `reliable` fields (sent on change over the reliable stream, kept per object generation).
4. Per class: `positionField` (the Vec3 used for interest; -1 = always relevant), `radius`, `priority`
   (players above props), `alwaysRelevant` for game state. Classes ≤ 65535, fields per class ≤ 63.

## Run the server

```cpp
using namespace aiverse::net;            // Bytes, Conn
using namespace aiverse::net::replica;
Server server(schema, opts);                      // opts: ServerOptions (below)
server.onJoin = [&](uint8_t slot) { auto id = server.create(player); server.setOwner(id, slot); };
server.onInput = [&](uint8_t slot, uint32_t seq, const Bytes& in) { /* step that slot's character once */ };
// each tick (tickRate):
server.poll(now);                                 // hellos, acks, inputs in order, messages
/* step the world; server.set(id, field, value) for what changed; server.setViewers(slot, {camera, ...}) */
server.tick();
server.broadcast(now);                            // reliable fields now, snapshots to clients that are due
```

- Attach only authenticated connections (`server.attach(conn)`): the host checks tickets
  (`engine/net/MatchTicket.h`, `engine/net/SecureUdp.h`). A client never names a slot or an object.
- Call `set()` only for real changes and skip sleeping bodies; unchanged objects then cost nothing per client.
  `setViewers` every tick is fine (a viewer moving less than ~4 % of the smallest radius costs no grid pass).
- Hidden information: `setVisibleTo(id, slot, false)`; `resetVisibility` returns to interest.

## Tune interest and budget

- `defaultRadius` / class `radius`: what a player must see; `cellSize` ≈ radius / 2. `interest3d` (default on)
  for towers, multi-floor buildings, space; off keeps x/z columns (same result, more distance tests).
- `bytesPerSecond` per client (default 48000) and `snapshotRate` (20): what does not fit waits and rises by
  accumulated priority × distance; read `ServerStats::starved`. Owned objects go first.
- Measure, do not guess: `aiverse_replica_tests --bench [near|crowded|latency|all|rest|still]` and the stats
  `evaluated / relevant` (work share), `entries`, `creates`, `deltaFields`, `snapshotBytes`.

## Input, prediction, messages

- Client: `sendInput(bytes)` returns the sequence; pack input small (a few bytes per tick). `inputRedundancy`
  5 (default) .. 8 for bad links; server delivers frames in order, at most `maxInputsPerTick` per poll.
- Owner prediction: `Predictor<State, Input>` with the same step function the server runs; on each new
  snapshot (`latestTick()` changed) `reconcile(latest owned state, lastAppliedInput(), diff)`; smooth the
  correction with `engine/net/Smoothing.h`. Predict against static geometry only unless the client has more.
- Remote events: `client.send(channel, payload)` / `server.onMessage` (size- and rate-limited, reliable);
  `server.send(slot or 0, channel, payload)`. Validate every payload on the server; reply with refusals.

## Client rendering

- Draw remote objects from `sample(id, field)` (render time), the owner from the predictor, game state from
  `latest()`. React to `onCreate` / `onDestroy` / `onReliableChange`.
- Render time is a slewed clock (`renderTimeMs()`, `renderTick()`, never backwards). Prefer
  `ClientOptions::adaptiveDelay = true` on mobile links (follows `jitterMs()` and `snapshotLoss()`; 4g ≈ 180 ms)
  and a fixed `interpolationDelayMs` (100) on LAN. Watch `ClientStats::underruns` / `underrunMs`.

## Build and verify

```sh
cmake -S . -B build/.temp/replica -G Ninja -DCMAKE_BUILD_TYPE=Release   # Mac: + -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/.temp/replica --target aiverse_replica_tests
build/.temp/replica/aiverse_replica_tests          # or: ctest --test-dir build/.temp/replica -R aiverse_replica_tests
build/.temp/replica/aiverse_replica_tests --bench
```

- Test the game's host like `tests/replica/ReplicaTest.h` (`LoopWorld`: server + N loopback clients on a virtual
  clock) for logic, and like `tests/replica/replica_net_tests.cpp` (`SimWorld`: secure UDP over
  `engine/net/SimNet.h` presets `4g` / `bad`) for loss, jitter, late join. Check convergence (every client field
  equals the server's quantized value after a quiet period), owned state vs `lastAppliedInput`, bytes per client,
  hostile input (random / truncated packets must never crash).
- A change to `engine/net/replica` must keep `aiverse_replica_tests` and `aiverse_place_tests` (needs
  `-DAIVERSE_ENABLE_PHYSICS=ON`) green, with `-Wall -Wextra` clean, and once under ASan + UBSan.
- Report what you measured (tick ms, bytes per client, underruns) and what you did not run (real devices,
  WebSocket path, web build: `SecureUdp` and listeners are native).
