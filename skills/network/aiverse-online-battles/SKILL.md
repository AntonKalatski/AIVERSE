---
name: aiverse-online-battles
description: Configure and verify AIVERSE online battles (direct-control PvP and PvE on phones) - rollback window, tick rate, simulated bad mobile links, reconnect and late join, local network acceptance.
---

# Online battles

References: `docs/battle-net.md` (rollback stack, secure UDP, tickets, link simulator), `docs/local-network.md` (loopback acceptance stand), `docs/grpc.md`.

1. Settings: tick rate, rollback window, interpolation of corrections, compatibility key (splits incompatible builds), per-player startup payload.
2. Simulate first: `battle_net_simulate {profile: bad, tickRate, rollbackWindow}` - loss, bursts, latency, jitter, duplicates, NAT rebinding, app suspend. Read desyncs, rollback depth and correction size.
3. Determinism is the base: any checksum mismatch between peers is a simulation bug (`skills/verification/aiverse-determinism-triage`), not a network setting.
4. Reconnect and late join: world catch-up must reach the same hash as staying peers.
5. Local acceptance: the loopback stand in `docs/local-network.md` (meta service + authority + transports) - a local test, not a production service.
6. Never send game-specific protocol code into the engine; report what ran on real devices versus the simulator.

Runtime library: none. Workflow over the engine battle network tools.
