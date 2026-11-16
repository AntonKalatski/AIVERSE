---
name: aiverse-city-server
description: Run an AIVERSE top-down city as one persistent server world for many players - snapshots of what is near each hero, tickets and seats, accounts between sessions in a file or the meta service, the client mirror with prediction - and measure it with the aiverse_city tool.
---

# city-server

Behaviour: `docs/city-server.md`. Code: `engine/net/city/` (`CityNet.h`, `CityMeta.h`, `CityMirror.h`), tool `apps/city`, meta module `engine/meta/ServiceWorld.cpp`, client `apps/client/ClientCity.cpp`. Tests: `aiverse_tests city_server`, `aiverse_tests city_accounts`, `aiverse_meta_service_tests`.

1. Not lockstep and not the FPS session: the server alone simulates; clients get entities within `radius` (changed ones, standing ones once), incidents, simulation events, premises and the account.
2. Rules need `"commerce": {"accounts": "external"}`; traffic limits are per player and scale with `trafficPerPlayer` up to `trafficMaxScale`.
3. Measure before deploying: `aiverse_city load --project DIR --map maps/city.json --players 100 --seconds 20` (one JSON line: step time, bytes per client, entities per snapshot, memory).
4. Real processes: `aiverse_city serve ... --seats N --port P [--accounts FILE | --meta-config ... --meta-state ...]`, then `aiverse_city bots ... --connect 127.0.0.1:P`. Tickets: server key (`Options::key`), `aiverse_city ticket --key HEX64 --player ID --seconds 3600`; without a key everyone is a guest.
5. Client: `AIVERSE_CITY=host:port` (+ `AIVERSE_CITY_SEATS`, `AIVERSE_CITY_PLAYER`, `AIVERSE_CITY_TICKET`, `AIVERSE_CITY_PREDICT=0`). The project's scripts and native modules do not run on the client mirror.
6. Report honestly: at most 250 seats, plain UDP (not encrypted), no delta compression, client never run on a screen unless you did it.

Runtime library: none (engine network module). Session admission policies: `skills/network/aiverse-session-admission`.
