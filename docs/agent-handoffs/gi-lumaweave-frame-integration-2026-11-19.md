# GI + LumaWeave integration — agent handoff (2026-11-19)

Branch: `feature/gi-lumaweave-frame-integration-20261119` (forked from `feature/lumaweave-massive-lighting` at `4a560575417022b4e78fc4c1b677eccce99f6f8f`). Never modify `main` from this stage. Indirect GI and direct LumaWeave remain separate contributions; no double light.

## Stage 1 — geometry-free mobile fallback (SOURCE COMMITTED; NOT NATIVE TESTED)
Source commit: `ca77cb89ee6f51df805b069f852b208d7ee33a96`.
Changed: `engine/render/LumaWeaveNativeCoordinator.h`.

- Add `prepareFrame(const GiGpuBvh* geometry, uint64_t revision, ...)` with a nullable geometry/revision-zero pair. The original `const GiGpuBvh&` overload stays source compatible and forwards to this overload.
- The Metal ray-traced path requires BOTH non-null geometry and nonzero geometry revision. Without geometry it cannot enter Metal, instead the existing explicitly selected mobile/legacy raster path can receive the same validated LumaWeave registry without a dummy BVH. Strictly reject mismatched pointer/revision pairs.
- Neither ECS, LightSystem, RHI, nor the GI probe runtime was modified. Legacy-vs-GPU exclusivity and overflow source-loss diagnostics remain unchanged.

Actual checks: GitHub file read and GitHub source update acknowledged. **NOT RUN:** native C++ compiler, CMake, CTest, Metal/GLSL compilation, GPU screenshots/perf, benchmarks. Source has not been validated by a native build.

Environment/rebase warning: GitHub comparison on 2026-11-19 showed the source feature branch 467 commits behind `main`, with substantial concurrent renderer changes. Do not pretend it is rebased/merged. Preserve main's renderer, camera, texture, FX changes when eventually integrating; resolve a real three-way merge on a native checkout. No branch merge has been attempted.

Next: wire an OPTIONAL GiGpuCascadeRuntime into native per-view driver, with actual fence polling, explicit bounce-source sync, per-view GI overlay reset (Renderer3D::beginScene does not clear it), and fail-closed presentation when source sync is deferred. Then add targeted CPU/source regression fixtures, update AGENTS.md and acceptance runner, and perform ONE native validation pass after code freeze. Record exact test SHA/results, not historic 33/33.

## Stage 2 — native optional bounced GI, source synchronization and compile regression (SOURCE COMMITTED; NOT NATIVE TESTED)

Implementation commits:
- `a8894d64a6416cea9a0538730c8a2ea9fb866b36` — `engine/render/LumaWeaveNativeDriver.h`, nullable direct ray BVH, optional `GiGpuCascadeRuntime*`, bounded GI probe budget, opt-in `synchronizeGiPointSources`, GI result diagnostics, fence-aware source synchronization and per-view scoped GI overlay clear.
- `395afb9c745c261020a58aef90d7f18573aea4aa` — correct native driver GPU completion type name: `LumaWeaveScreenMetal::Completion`.
- `03d5ce6b82a47d818fd0a1baa2c6576c6a0e78b1` — same preexisting typo correction in `engine/render/LumaWeaveWorlds.h`.
- `0088a5095f8676cddf67f7c448c2c4608f87e784` — new `tests/render/lumaweave_gi_frame_contract.cpp`, source-type checks plus fake GPU bounce synchronization cases (ready, pending, revisions, 16-source limit, spot rejection, clear, reset).
- `d16d4528a212e8c0b399d3be4e1ba6d6e14304d3` — CMake CTest target `lumaweave_gi_frame_contract`.
- `5533101c4c938fd9532691f78098c4c20b3c2748` — `AGENTS.md` ownership and safety contract.

API: `LumaNativeViewInput.indirectGi` optionally provides a host-owned, already-configured cascade GI runtime, and `giProbeBudget` controls total cascade probe jobs. Set `synchronizeGiPointSources=true` only if the GI runtime must be fed from the current LumaWeave source registry; this explicitly rejects unsupported spot emitters and >16 moving point bounce sources. On a deferred/busy publication, frame refuses to bind stale bounced GI. `giRequested`, `giBound`, `giDeferred`, `giSourceSync` and `giFrame` distinguish the actual result from default lighting. The RAII scoped borrowed atlas clear prevents one viewport's GI volume leaking to another; it is invoked after renderer beginScene and before rendering and again on exit, even during exceptions. No authoritative ECS or simulation mutation.

Actual validation: GitHub read/update/create commit operations acknowledged, CMake target registered. **NOT RUN:** C++ compilation, Clang Debug/Release, CTest, Metal MSL, GPU frame capture, visual parity, mobile backend tests, full acceptance script. Earlier 33/33 GI tests were owner-reported against stage7, NOT this source SHA. No claims of pass.

Known limitations and next: verify header-only native driver code builds (its include graph is broader than earlier tests); ensure runtime replacement does not reuse the old `LumaWeaveGiSync` revision, preserve GI fence-poll diagnostics, and accept the isolated branch name in the one-shot validation script. Then native Mac test only after code freeze. Current branch still inherits the 467-main-commit divergence from its LumaWeave parent.
