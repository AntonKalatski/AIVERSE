# Bunker Wars backend adapter (`engine/meta/bw`)

Bunker Wars keeps its meta (profile, economy, heroes, clans, leagues, battle pass, chat...) on its
own servers (`BunkerWars-Server` Java, `BunkerWars-Server-CS` .NET). The engine talks to them through
this adapter; battles run on the engine net stack (`engine/net`, `apps/server`). Host code only:
nothing here runs inside the simulation.

| Piece | File | What |
|---|---|---|
| MessagePack | `engine/meta/MsgPack.h` | values, encoder (2013 spec, or `legacyRaw` = msgpack-java 0.6), bounded decoder, JSON bridge |
| Encodings | `engine/meta/Encoding.h` | base64/base64url, hex, SHA-256, HMAC-SHA256 |
| Wire formats | `bw/BwWire.h` | TCP frames `[u16 len][func[,idx]][u16 len][msg]`, chat packets, battle `NetworkPackage`, battle tickets |
| Meta API client | `bw/BwApi.h` | `POST {host}/{path}` `{"i":-1,"data":…}`, `token` header, 500 = handled error, 304/E-Tag cache, chunked replies, host failover, silent re-auth |
| Backend | `bw/BwBackend.h`, `bw/BwRoutes.cpp` | `meta::Backend` for `meta::Client`/UI: `service.action` → BW path, BW error → `meta::Code`, `meta/view` → engine view projection |
| Matchmaking | `bw/BwMatch.h` | matchmaker TCP session (`Connect`, `Match`, `Match.onReady` → `BattleHandoff`) |
| Battle node | `bw/BwNode.h`, `bw/BwHttp.h` | `aiverse_server --bw-node node.json`: internal HTTP API, tickets, `GameReport` to the meta |
| Default HTTPS | `bw/BwPlatformHttp.h` | `bw::platformTransport()` on `engine/pack/http/HttpRequest.h` (libcurl, NSURLSession, HttpURLConnection, WinHTTP, fetch) |
| TCP links | `bw/BwTcp.h` | `MatchConnection` (host group, connect token, GamePing, reconnect), `ChatConnection`, `HostProbe` (game/chat host checks) |

CMake: `aiverse_meta_bw` (pure, no sockets/threads, also in the standalone `engine/meta` build),
`aiverse_bw_http` (default transport, all platforms, links `aiverse_pack`) and `aiverse_bw_node`
(native only: TCP links, probes, plain HTTP, battle node, links `aiverse_net_server`).
Tests: `aiverse_bw_meta_tests`.

## UI binding

```cpp
bw::ApiClient api(bw::platformTransport(), {"https://meta.example.com"});
api.entryPoint(info, …);                               // chunks flag + auth/data hosts
api.auth(bw::ApiClient::guestAuth(info, guid, duid), …); // token, user id, game/chat hosts
bw::BwBackend backend(api);
meta::Client client(backend, persistOutbox);
client.begin(meta::requests::view());                  // v/profile/get -> view
client.begin({"heroes", "buy", opId, args});            // v/hero/buyHero, args = BuyHeroTO
client.pump();                                          // every frame, UI thread
```

Arguments are the endpoint's transfer object with the Unity client's field names (`*TO.cs`); the
reply's `data` is the endpoint's `data` object (scalars under `value`, `ResponseData.offers` under
`offers`). On failure `data.error` = `{code,key,text,obj,loc}` where `loc` is the client's
localization key (`Errors|shop|not_enough_money`).

`meta/view` projection: `source`, `player` (BW user id), `revision`, `profile.balances`
(`gold`, `gems`, `runes`, `spareParts`, `currency.<clientId>[.<subtype>]`), `profile.attributes`
(name, exp, league, elo, avatar, ...), `offers`, `heroes`, `skills`, `inventory` (things), `storage`,
`cargoes`, `clan`, `challenges`, `quests`, `forge`, `premium`, `passive`, `stat`, `drops`,
`levelUps`, `raw` (the whole ProfileResponseTO).

Services (full list: `bw::routes()`, ~190 entries): `auth`, `profile`, `inventory`, `heroes`,
`chests`, `craft`, `shop`, `purchase` (receipts validated by the BW server), `clan`, `leagues`,
`board`, `trophyroad`, `battlepass`, `quests`, `metaquest`, `liveops`, `challenges`, `tasks`,
`friends` (+ deeplink invites), `code`, `news`, `notifications`, `history` (incl. `replay` =
`v/history/game`), `campaign`, `match`, `statics.<table>` (= `v/db/<table>`, chunked).
Chat and push-style notifications (friend requests, clan events) arrive over the chat TCP
connection: `bw::encodeChatAuth`, `bw::ChatReader`, `bw::ChatType`.

Errors: NOT_ENOUGH_MONEY → Insufficient, NOT_AVAILABLE/MAINTENANCE/transport → Unavailable,
ALREADY_HAVE → Conflict, NOT_FOUND → Missing, auth/ban → Denied, NOT_IMPLEMENTED/NEED_CLIENT_UPGRADE →
Unsupported, others → Invalid.

Retries: the BW API has no operation ids. A mutation of a `@ChunkedMethod` controller is sent through
the chunk protocol with a request id derived from the meta operation id, so a retry gets the server's
cached reply (within the chunk cache TTL). Other mutations that fail in transport are terminal
(`Unavailable`, `data.refresh = true`): refresh the view instead of resending.

Transport: `bw::platformTransport()` works out of the box on every platform: it is the engine's
per-platform HTTP stack (`pack::httpRequest`, the same one that downloads pack chunks), with any
method, headers, body, timeout and the reply headers (E-Tag). Completions come from a worker thread
and are delivered by `ApiClient::pump()`. A host may still plug its own `bw::HttpTransport`;
`bw::HttpClient::transport()` is plain `http://` only (internal cluster). Hosts without a scheme get
`https://`. Like the Unity client (`AcceptAllCertificates…` handler) there is no certificate pinning;
unlike it, the platform stacks verify certificates normally.

## TCP: matchmaker, chat, host checks (TcpConnector parity)

Checked against `Connector.cs`, `TcpConnector.cs`, `NetworkManager.cs`, `MultiplayerManager.cs`:

- Frames `[u16 BE len][func or "func,idx"][u16 BE len][msg]`, UTF-8; strings of 32768+ bytes are
  refused (the client throws "Net packet is too big"); the client parses the whole channel index.
- Right after the TCP connect: `Connect` with the meta token (or the BW battle ticket), nothing
  when the token is empty (`MsgConnect`).
- `GamePing` `{"ms":<ms>,"tick":<tick>}` (`MultiplayerManager.Ping`), raw `"<ms>"` (`TcpConnector.MsgPing`);
  replies `onGamePing`/`onPing`/`onPingT` are `Pong` events.
- Drop: an `Int.onDisconnect` frame reaches the session; when armed with `reconnect(roomId, lastTick)`
  the connection comes back after 0.5 s on the same host group and sends `Connect` +
  `MatchReconnect {"roomId","lastTick"}` (`TryReconnect`/`OnReconnect`).
- Host groups: hosts are tried in order, the next after a failed connect (`HostGroup.Request`).
- Host checks (`NetworkManager.CheckServer`): all hosts at once, 1 s connect timeout; game hosts get
  `Ping` + token and must answer `onPing`, chat hosts get the chat ping `[0,2][0,1][0]` and must answer
  anything; first answer within 3 s wins (`bw::HostProbe`).
- Chat: `[i16 type][i16 len][json]`, auth packet type 1 `{"token"}`, chunked server packets.
- TLS: none. The Unity client opens plain `TcpClient` connections; `battlePortSsl` (C# server) is never
  used by it. `bw::TcpLink` is plain TCP as well.

## Web

Browsers cannot open raw TCP. BW's own WebGL target only had default defines (PUN, post-processing):
its `TcpConnector` (System.Net.Sockets + threads) and chat `NetClient` do not run in WebGL and there is
no WebSocket fallback in `Managers/Network`, so BW had no working WebGL matchmaking or chat; only the
meta (UnityWebRequest) would work there. Here the same holds: the meta API works on the web through
`fetch()` (the BW server must send CORS headers allowing the `token` and `E-Tag` request headers),
`bw/BwTcp.h` and the battle node are native-only. Battles themselves run on the engine net stack,
which has WebSocket on the web. Gap: a WebSocket front for the BW matchmaker/chat.

## Matchmaking handoff

1. Client: `match.choose` (`v/game/choose`) → matchmaker host; TCP `MatchSession`:
   `connect(token)`, `match(MatchParams)`; send `takeOutgoing()`, `feed()` what arrives.
2. `Match.onReady` (C# matchmaker) carries `battleHost`, `battlePort`, `battleTicket`
   → `session.handoff()`.
3. Client: `net::NetClient` to `handoff.address()`, `joinQueue(handoff.queueId(), describeGameMaps(game))`.
   The engine server seats it by ticket and starts when all human seats are taken.
4. Node → meta: `POST {metaBaseUrl}games/{id}/started` and `/result` (`GameReport`, 3 attempts);
   node → matchmaker: `nodes/heartbeat`, `games/{id}/events` (`started`/`closed`).

Engine server API (generic, any external matchmaker): `net::Server::reserve(Reservation)`,
queue id `"ticket:<ticket>"`, `ServerConfig::ticketCheck`, `onExternalMatchStarted`,
`onExternalMatchEnd(ExternalMatchEnd)`. External matches are not rated by the engine (the backend
rates). A seat not taken within `joinTimeoutSec` cancels the match (`closedFrom: "PROGRESS"`).

`node.json` (all keys optional except the secrets and maps):

```json
{"nodeId": "eu-1", "region": "eu", "publicHost": "battle1.example.com", "clientPort": 27015,
 "internalUrl": "http://10.0.0.5:8090", "internalPort": 8090, "capacity": 200,
 "ticketSecret": "<shared with the matchmaker, >= 32 bytes>", "internalToken": "<X-Internal-Token>",
 "metaBaseUrl": "http://meta:8080/internal/battle/", "matchmakerUrl": "http://matchmaker:8081/internal/",
 "gameId": "com.example.bunkerwars", "maps": {"101": "maps/bw_101.json"}, "joinTimeoutSec": 30,
 "tickRate": 20, "inputDelay": 2, "rollbackWindow": 8}
```

## Limits / not done

- No live server was reachable from the build VM (prod/stand meta hosts refused through the proxy);
  tests use mock replies shaped after the client DTOs (`tests/data/bw_meta`).
- The Java matchmaker needs the changes of "Java servers" below to hand battles to engine nodes.
- `history` holds the engine replay (base64 `.rpl`), which BW's replay viewer cannot play; some of the
  71 statistic values are derived or 0 (see "Battle statistics").
- Native HTTP stacks: see "Native HTTP stacks: what was compiled" (NSURLSession not compiled).
  Web builds have no TCP (see Web).

## Battle statistics (GameReport.battleResult.matchStatistic)

`aiverse_server --bw-node` re-simulates every finished external match on a worker thread
(`apps/server/BwStats.h`, like the server verdict) and fills `values` with BW `PlayerStatistic.Key`
order (71 numbers). Exact: produced, captures (all / enemy / neutral / by house, tower, smithy),
points lost, converts and upgrades by role, skill slot uses, max units on the field, points, energy
received / spent, multiplayer win, morale stars and king of the hill score (when the rules turn match
modes on). Derived from the engine's model (garrisons are counts, not units): `UnitsDeadByAttack` /
`UnitsKilledByPlayerAttack` = garrison units lost to / killed in assaults, `UnitsDeadByDefense` /
`UnitsKilledByPlayerDefense` = attackers that walked into an enemy garrison, `DeadByUnit` / `KilledByUnit`
= unit-vs-unit deaths, `UnitsDeadByTower` / `KilledByPlayerTower` = killed by a structure, skills = killed
by anything that is not a unit; attacks = arrivals at one enemy point with less than 2 s between
units (success = the point was captured). Always 0: boss kills, colour skills (31-34), skill-star
morale (37, 38), campaign stars, emotions, affected units per skill slot (54-58). A re-simulation
that does not reach the players' checksum sends no values and `strErrors: "stats ..."`.

## Map ids

`tools/meta/bw_map_ids.py --game <game>` writes `<game>/bw/map_ids.json` (BW `MapHeader.mapId` ->
engine map, 119 ids; campaign split-test variants under `variants`). Battle node config:
`"maps": "<game>/bw/map_ids.json"`. Re-run it after the BW importer.

## Spectators

Spectator tickets (`BattleTicketRole.Spectator`, payload `R: 1`, `N: name`) are accepted by the node:
before the start the watcher waits with the reservation and gets the match with the players, later
it joins as a late spectator (history, then `spectatorDelayTicks` behind). Client side:
`MatchSession::spectate(roomId, custom)` -> `onSpectateJoin` -> `handoff()` with `spectator = true`.

## Chat

`BwBackend` chat services over a `ChatConnection` (`bindChat`): `chat.send {room, msg[, messageType]}`,
`chat.history {room[, count]}` (reply = OnHistory), `chat.rooms`, `chat.state {whereIs, state}`,
`chat.watch {senderIds}`, `chat.notifyRead | notifyAccept | notifyDecline | notifyRemove {id[, reqId]}`.
Incoming messages, members, notifications (friend / game / spectator / clan) and events are kept in
`BwBackend::chat()` and in `meta/view` `chat`.

## Meta tutorials

`tools/meta/bw_tutorials.py --unity <bunker-wars>/Client/Assets --game <game>` writes `<game>/ui/bw_tutorials.json`
(217 TutorialData of `_TutorialMeta` with guids; map / campaign / feature references resolved to ids).
`bw::MetaTutorials` (engine/meta/bw/BwTutorials.h) reproduces UI.Windows `TutorialSystem`: window events
start the tutorials of that window (BW class, short class name or engine screen id) whose conditions
hold; actions run in order; `Wait`, `WaitForClick`, `WaitButtonClick` and `SetKey(save)` suspend.
Conditions on the profile, binData keys, counters and client state are built in (HasKey, ExistKey,
HasKeyTimer, HasCounter, SplitTest, ProfileCurrentHero, IsOfflineMode, VersionAvailableCheck,
HasCountry, ProfileRegistrationDate, ProfileActiveDays, HasProfileContent heroes / skills / rating /
currencies, HasProfileHeroSlotIsNull); the client-screen ones (craft / card screen selection, campaign
progress, trophy road, boosters, clan screen mode, profile features) go to `TutorialHost::condition`
and count as false without it (`unsupported()`).

## Client screens (the game's `ui/`)

`apps/client/ClientMeta.cpp` gives UI scripts the `backend` table (login, request, view, cached, chat,
match, spectate, cancel, connected, tutorial*, events `game.on("backend")`: match / handoff /
tutorial / chat / notAuthorized). Configuration: game.json `"backend"` or `ui/backend.json`
(`type`, `hosts`, `client`, `tutorials`). On `handoff` the client connects its NetClient to the engine
battle server and joins the ticket queue by itself; the match then runs like any network match.
The Bunker Wars game (outside this repo, `AIVERSE_BW_GAME_DIR`) has in `ui/`: `bw_login` (guest sign-in, guid kept in `storage`), `bw_home` (profile,
league, balances; play / heroes / shop / clan), `bw_heroes` (select hero), `bw_shop` (offers, market),
`bw_clan` (own clan, list, join request), `bw_queue` (matchmaking until the handoff), `bw_results`
(after every match); texts in `ui/bw_meta_lang.json`. Screens send BW window events to the tutorial
runner (`bw_home` = MainScreen + PlayComponents + HeroAnimationComponent ...); tutorial tags are element
ids, LockElement shows the hint panel. These are functional screens on the engine's default styles,
not the BW art. Screens converted from the BW client by `tools/importers/bunkerwars/client.py` are used when
present: `victory` / `defeat` (local) and `endmatch` (online) replace `bw_results` after a match, and
`popupmenuscreenmobile` replaces the engine pause menu (`game.on("screen")`). `game.start(i, {ruleSets = {...}})`
switches named rules.json `ruleSets` on for a local match; the lobby fills them from `challenges.customRules`
(`custom<clientId>`; ids the rules do not define are dropped). Online matches on engine nodes cannot carry
per-match rule sets yet: every player must load the same map bytes (data hash), and GameSpec has no field for them.

## Native HTTP stacks: what was compiled

| Stack | File | Checked |
|---|---|---|
| libcurl (Linux) | `HttpCurl.cpp` | built and run in tests (`bw_platform_https_transport_out_of_the_box`) |
| WinHTTP (Windows) | `HttpWin.cpp` | compiled for `x86_64-windows-gnu` (zig 0.13 clang + mingw-w64 headers), no warnings; not run |
| fetch() (web) | `HttpWeb.cpp` | compiled for wasm32 against emscripten 3.1.64 headers (zig clang, `__EMSCRIPTEN__`); not linked or run |
| HttpURLConnection (Android) | `HttpAndroid.cpp` | compiled for aarch64 with an NDK-style jni.h; not run on a device |
| NSURLSession (macOS / iOS) | `HttpApple.mm` | not compiled: needs Xcode on the Mac; the session's shell is a Linux VM and the editor's `native_build` builds the main checkout, not this branch |

All `engine/meta/*.cpp`, `engine/meta/bw/*.cpp` also compile warning-free for Windows with clang.

## Java servers: changes needed for engine battle nodes

BunkerWars-Server (Java) is read-only for this port. To play BW battles on `aiverse_server --bw-node`
with the Java matchmaker, the backend team applies the following (the C# BunkerWars-Server-CS already
does 1-4 on its side; these are the same contracts).

1. **Ticket secret and node registry** (new settings, `SettingsDAO` / properties):
   `battle.ticketSecret` (>= 32 bytes, shared with the nodes), `battle.internalToken`, and the node list
   (from heartbeats, below, or a static `battle.nodes = [{"nodeId","region","publicHost","tcpPort",
   "internalUrl","capacity"}]`).
2. **Heartbeat endpoint** in the battle / matchmaker process: `POST /internal/nodes/heartbeat`
   (header `X-Internal-Token`) with `NodeHeartbeat {nodeId, region, publicHost, tcpPort, tcpPortSsl,
   internalUrl, games, players, capacity, draining}`, every 5 s; a node silent for 15 s is dead.
3. **Placement at Match.Ready** — `mw2-server-battle/.../processors/match/MatchReadyProcessor.ready()`,
   right after `game.setPrepared()` and before `game.cbMatchReady(...)`:
   - pick a node (lowest `games / capacity` among non-draining live nodes; region by worst player
     ping, then GeoIP, then config order);
   - `POST {node.internalUrl}/internal/games` with `X-Internal-Token` and the **GameSpec** (Gson,
     camelCase, nulls omitted):
     `{gameId, customId, matcher, mapId, mode, winMode, conditions, players: [PlayerConnectionInfo in map
     slot order, bots included: {user: {id, name, leagueId, eloRating, ...}, info: {color, team}, isBot,
     botConfigId}], potentialCheaters, fake, botsAllow, customRoom, winter, ownerId, tourneyId, exact,
     clientVersion, specialParameters, customGameConfig, customTournamentConfig,
     useCustomTournamentConfig, matchContext}`. `matchContext` = anything the meta needs back to compute
     results (the `MapResult`, league rating, group); the node returns it untouched.
     Replies: `201 {id, roomId}`, `400` invalid spec, `409` already hosted, `503` draining (try the next
     node, up to 3).
   - per human player issue a ticket `v1.<base64url(json{"G":gameId,"U":userId,"Exp":unixSeconds})>.
     <base64url(HMAC-SHA256(ticketSecret, "v1.<payload>"))>` (no padding; expiry = now + 10 min);
   - add to `Matching.Result` (mw2-server-api `Matching.java`, class `Result`) the fields
     `public String battleHost; public Integer battlePort; public Integer battlePortSsl; public String battleTicket;`
     and fill them per player before `cbMatchReady` (the ticket differs per player, so send one
     `Match.onReady` per player link instead of one to `playersLink()`).
4. **Spectators**: `Matching.SpectateJoinTO` gets the same four fields; for a game hosted on a node the
   spectator ticket payload is `{"G":gameId,"U":0,"Exp":..,"R":1,"N":"<name>"}`. `Matching.SyncComplete`
   gets them too, so `MatchReconnect` sends a reconnecting player to the node (`battleTicket` = a fresh
   player ticket).
5. **Results**: implement the meta internal API of BunkerWars-Server-CS `docs/meta-battle-api.md`:
   `POST /internal/battle/games/{gameId}/started` `{gameId, gameStartTime, spec}` -> `resultService.matchCycleUpdate(game)`
   for rating games and `matchChallengeService.useTokensForBattle`; `POST /internal/battle/games/{gameId}/result`
   `GameReport {gameId, closedFrom: "RESULT"|"PROGRESS", spec, gameStartTime, battleResult: {localStatus,
   matchStatistic: [{userId, playerInfo: {color, team}, winner, experience, values: [71 numbers]}],
   startBattleTime, endBattleTime}, players: [{userId, isBot, status: "Victory"|"Defeat"|"Draw"|"None", ts,
   win, softDefeat}], strErrors, strStates, history: [base64]}` -> build `Game` from `spec` + `players` and
   call `resultService.saveResultThrowable(game)`; reply `{historyItem, drops: {userId: BattleDropTO}}`.
   Idempotent per `gameId` (the node retries 3 times). `history` holds the engine replay (`.rpl`,
   base64), not BW `NetworkPackage`s: store it in `game_replay` but do not feed it to the BW replay viewer.
6. **Match events**: `POST /internal/games/{gameId}/events` `{gameId, customId, type: "started"|"closed",
   wasStarted, hasResult}` on the matchmaker (tourneys, custom rooms, release of the players).
7. **Clients**: BW Unity clients from the current branch already switch to `battleHost` with the ticket
   (`MultiplayerManager.SwitchToBattleNode`), but an engine node speaks the engine protocol (UDP / WebSocket,
   `engine/net/Protocol.h`), not BW TCP frames: only engine clients (`AiverseGame` with
   the Bunker Wars game) can play on it. Mixed BW-client / engine-client matches are not possible; route
   engine clients by `clientVersion` / a feature flag to engine nodes and BW clients to the Java battle server.
8. **Map ids**: the engine maps exist for the ids in `<game>/bw/map_ids.json`; a placement
   for any other `mapId` gets `400 invalid game spec` and must fall back.
