---
name: aiverse-place-server
description: Host an AIVERSE server-authoritative place (replica + physics, aiverse_place serve) for players over the Internet - tickets and the server key, encrypted UDP, WebSocket behind TLS, reconnect to the kept character, load tests with bots and SimNet, and what to verify before shipping.
---

# Place server

Use when a game world is simulated by one server process and clients only send input and draw what the server
replicates (physics sandbox, social hub, small persistent room): the `aiverse_place` host on `net::replica`.
Not for lockstep RTS matches (`skills/network/aiverse-online-battles`) or the city mirror
(`skills/network/aiverse-city-server`). How the replication itself works (schemas, interest, prediction):
`skills/network/aiverse-replica`. Generic admission policies for other hosts: `skills/network/aiverse-session-admission`.

Reference: `docs/replica.md` § Place hosting (flags, security model, numbers, limits) and § First milestone (scene
format, how the sandbox plays). Code: `apps/place/PlaceAdmission.h` (tickets, `PlaceGate`), `apps/place/PlaceServer.h`
(`attach(conn, player)`, `setReconnectSeconds`, `admits`), `apps/place/PlaceLink.h` (client link with reconnect),
`apps/place/main.cpp`. Transport: `engine/net/SecureUdp.h`, `engine/net/MatchTicket.h`, `engine/net/WebSocket.h`.

## Build

```sh
cmake -S . -B build/.temp/place -G Ninja -DCMAKE_BUILD_TYPE=Release -DAIVERSE_ENABLE_PHYSICS=ON   # Mac: + -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/.temp/place --target aiverse_place_app aiverse_place_tests aiverse_place_secure_tests
ctest --test-dir build/.temp/place -R '^place' --output-on-failure      # place + place_secure
```

## Host

1. Key: `aiverse_place keygen > place.key && chmod 600 place.key`. The login service that issues tickets shares it;
   never ship it in a client, never pass it on a command line in production (`--key-file`).
2. Serve: `aiverse_place serve --scene SCENE.json --key-file place.key --port 27300 [--ws-port 27301]
   [--reconnect-seconds 30]`. With a key every player needs a ticket and UDP is `SecureUdp` (encrypted,
   authenticated, NAT rebinding). `--insecure` (plain UDP, guests when there is no key) is for a LAN or a laptop only;
   `serve` refuses to start with neither.
3. Tickets: one per player, `placeTicket(key, playerId, seconds, "host:port")` in the issuer
   (`aiverse_place ticket --key-file place.key --player ID --seconds 3600 --server host:27300` for tests). The
   expiry also bounds reconnects: issue it for the session length. A ticket is a secret of that player.
4. Browsers / UDP-blocked networks: `--ws-port`, and a TLS proxy in front (nginx / Caddy: `wss://place.example.com`
   -> `ws://127.0.0.1:27301`). The PlaceGate checks the ticket and a proof of its key; the stream is only private
   through TLS.
5. Same player twice: newest connection wins; the older client is told and stops; a ticket fighting over two
   devices is refused for 60 s after 3 takeovers.
6. Reconnect: a dropped player's character stays `--reconnect-seconds` (stands, no input) and the same player id
   gets it back (same replica object, `setOwner` to the new slot). Clients use `PlaceLink` (backoff 0.5..8 s,
   4 s without snapshots = lost, `suspend()` / `resume()` on mobile focus events).
7. Embedding in your own host: accept `SecureListener` connections with `admit = matchId == kPlaceMatch &&
   server.admits(playerId)`, feed WebSocket / plain connections through `PlaceGate`, then
   `server.attach(conn, playerId)`; never attach without an identity you verified.

## Load and failure tests

- In process over SimNet (virtual clock, link profiles `lan`, `wifi`, `4g`, `3g`, `bad`):
  `aiverse_place load --clients 64 --seconds 20 --profile 4g --late 15` (tick time, bytes per client, convergence,
  late join). Tests in `tests/place/place_secure_tests.cpp` show the pattern for blackouts (`SimNetwork::setSuspended`),
  ticket reuse and suspend / resume with `PlaceLink`.
- Real processes: `aiverse_place bots --connect HOST:27300 --key-file place.key --count 8 --seconds 120 --drop-at 30
  --drop-count 2` (bots issue their own tickets: test servers only; `--tickets FILE` for issued ones), one more over
  `--connect ws://HOST:27301`, `kill -9` a bots process and start it again within the window: the server logs
  `back on slot N, same character`.

## Verify before shipping

- `place` and `place_secure` pass; `serve` runs with `--key-file`, no `--insecure` anywhere public.
- Bots over SecureUdp and over the TLS proxy: 20 snapshots/s per client, server tick avg well under the tick
  (16.7 ms at 60 Hz) with the expected player count, bytes per client within your mobile budget.
- A forged, an expired and a missing ticket are refused (server line `bad ticket N`, `rejected tickets N`).
- Drop and restart a client: same character, position kept; after the window it is removed.
- The graphical client under the target frame rate still shows the snapshot rate (network runs on its own thread).
- Report what is not there: orchestration (allocation, health, draining, restarts), persistence of the world or
  kept characters across a restart, a login service, TLS inside the engine, web clients.

Runtime library: none (the `aiverse_place` library / executable under `apps/place`, CMake target
`aiverse_place_app`; requires `AIVERSE_ENABLE_PHYSICS`).
