# PHYSIS — required agent instructions

Scope: `engine/physis/`, `tests/physis_*.cpp`, `docs/agent-handoffs/physis-*.md`.

1. **Genre independence is non-negotiable.** PHYSIS engine files must not include anything from `modules/fps`, `modules/rts`, `libraries/include/aiverselib/genre`, or any game-specific types. Native projects use the common C++ API and ECS components, not forks per genre.
2. Long-lived state lives in `aiverse::World` components/resources, authoritative math uses `Fixed`. Renderer/FX/audio may consume snapshots and emit presentation events but must not own climate state.
3. Preserve deterministic ordering, fixed step scheduling, seed/replay, rollback/COW. No wall clock, platform distributions or float/double in simulation.
4. Integrate Terra, Water, Foliage, Physics, VFX via clean interfaces and opt-in adapters. No hard link to a rendering backend or platform.
5. After **EVERY** implementation stage write an explicit handoff in `docs/agent-handoffs/` with branch, implementation SHA, changed paths, architecture/API, tests ACTUALLY RUN and their results, known gaps, and next concrete task; commit it separately from implementation. Never mark unexecuted tests as passed.
6. Existing gameplay/genre modules must remain unaffected unless their integration is explicitly tested and reviewed. Respect concurrent branches; never force push.

Latest Stage 13: `docs/agent-handoffs/physis-sampling-persistence-2026-11-18.md`; Stage 12: `docs/agent-handoffs/physis-numerical-ci-2026-11-18.md`. Stage 11: `docs/agent-handoffs/physis-forces-box3d-2026-11-18.md`. Stages 8–10: `docs/agent-handoffs/physis-{wind,gravity,storm}-2026-11-18.md`; Stage 7: `docs/agent-handoffs/physis-flood-authority-2026-11-18.md`. Stage 6 role/ownership contract: `docs/agent-handoffs/physis-ownership-terra-water-2026-11-18.md`. Stages 1–5 remain preserved; no stage claims passed native tests.

## PHYSIS / NEREID / TerraWeave coordination (2026-11-18)

Read `docs/physis-nereid-terraweave-integration.md` (root `AGENTS.md` ownership matrix) before editing flood/water/terrain integrations.
- PHYSIS agent owns weather, dynamic precipitation, surface wetness/snow/ice, fire/quake and independent nature ECS. NEREID Water agent (PR #23, `feature/nereid-water-core`) exclusively owns canonical lake/river/ocean ECS water, water-body geometry, flow, water sampling, buoyancy and water surface pipeline. TerraWeave agent (`feature/terraweave`) owns terrain heights/biomes/foliage and later consumes both through **optional** adapters; PHYSIS PR #25 is `feature/physis-nature-simulation`.
- The local `HazardCell::waterDepth` shallow flood is **experimental standalone gameplay simulation**. For NEREID-managed water bodies, opt in to `FloodMode::ExternalAuthority` via `InstallOptions::floodMode`: local depth is cleared and flood calculations bypassed; PHYSIS fire/quake still run. This switch and tests are authored but native verification is NOT RUN. Never invent competing water sources or integrate normalized PHYSIS rain intensity directly into NEREID metres.
- PHYSIS core must NOT include NEREID or TerraWeave headers; TerraWeave core must not depend on PHYSIS/NEREID. Host-level integration owns conversions: Terra signed centimetres -> common signed Fixed metres, X/Z horizontal and Y vertical; floor division for negative world positions, elevation and shoreline coherence, explicit scheduler and persistence ownership.
- PHYSIS read-only `EnvironmentalResponse` is the input for Terra terrain material, wetness/snow masks and foliage wind; NEREID `WaterSample`/water bodies are the source for Terra waterline, exclusion/shoreline and buoyancy. No presentation feedback into authoritative state.
- On each subsequent implementation stage: code + unit/cross-system tests in a code commit, **separate** handoff documentation commit, scope/owner coordination and verified native build status. Do not edit the Water agent's or Terra agent's active branches and do not merge their PRs automatically.

## Stage 14 — diagnostics ownership (2026-11-18)

- `PhysisPresentation.h` and `PhysisDiagnostics.h` are **read-only**, renderer-independent consumer APIs. They return copies/arrays for external editor/VFX/physics/Terra host code. Do not add ImGui/RHI/Box3D/genre includes here, or treat a PHYSIS heatmap as a visual renderer integration.
- `DiagnosticRaster::rawValues` are `Fixed::raw` and `valid[i]` marks active sampled cells. Do not infer real NEREID water levels from `LocalFloodMeters`. `diagnosticNormalized` is presentation-only; it must not influence authoritative simulation.
- Add post-stage handoffs; Stage 14 `docs/agent-handoffs/physis-diagnostics-2026-11-18.md`. The tests and CI are authored but native status is **NOT RUN**.

## Stage 15–16 spatial field reuse (2026-11-18)

- `PhysisDiagnostics.h` now samples basic scalar environment/hazard layers without full per-cell 3D field evaluation. New `PhysisSpatialIndex.h` builds a transient `World` COW snapshot, 2D candidate bins, and global fallback for huge volumes/uniform gravity; `sampleDiagnosticRasterIndexed` uses the same source snapshot.
- Do not change fixed-point math, Earth/Box3D gravity authority, and wind/storm candidate order in the unindexed reference. Existing `windAt`, `gravityAt`, `stormsAt`, and `sampleNatureField` remain supported. The indexed API invokes their **shared policy** through ordered visitors to make parity testable.
- The index is a derived read-only **snapshot**, not a mutable ECS mirror; rebuild for a new authoritative tick or after edits/rollback/load. It does not accelerate WeatherFront/Quake simulation, terrain/water, or external physics. Avoid retained historical snapshots.
- Handoff Stage 15: `docs/agent-handoffs/physis-diagnostic-fastpath-2026-11-18.md`; Stage 16: `docs/agent-handoffs/physis-spatial-index-2026-11-18.md`. All tests added, native compile/CTest/CI **NOT RUN** in connector environment.

## Stage 17 — editor debug view integration

- `engine/physis/PhysisDebugPalette.h` supplies only read-only display colour helpers. The editor PHYSIS window is `apps/editor/windows/PhysisWindow.cpp`, using the existing window plugin registrar; **do not** put ImGui or editor includes into `engine/physis`.
- Editor live PHYSIS is strictly read-only. Demo simulation is a fresh private `aiverse::World`, stepped only with explicit controls; never use it as a surrogate for actual game integration. The 2D heatmap outlines storm/wind/gravity volumes but is not a 3D Scene overlay or editable Inspector.
- Stage 17 test `physis_editor_view` / CMake / CI: authored, **NOT RUN**. Full editor build and macOS visual QA still mandatory. Handoff: `docs/agent-handoffs/physis-editor-debug-view-2026-11-18.md`.

## Stage 18 — Scene/Inspector and generic map config handoff

- `PhysisSceneGeometry.h` outputs reusable Fixed-position diagnostics only. The graphical projection and controls live in `apps/editor/PhysisSceneOverlay.h`, `apps/editor/windows/PhysisWindow.cpp`, and the shared `InspectorWindow.cpp`. Keep all ImGui/RHI/Box3D/editor includes OUT of PHYSIS core.
- `PhysisProjectConfig.h` may parse a map-level `physis` JSON block and call `installFromMap` explicitly from any native `GameModule::setup`. No per-genre automatic registration or direct renderer state. Inspector authoring uses `MapDoc` snapshots and validations and must not mutate live `Simulation::world()`.
- `physis_scene_geometry`, `physis_project_config`, and (when `aiverse_editor_core` exists) `physis_authoring` CTests are authored and registered, **NOT RUN**. The Scene GUI still requires native compilation and macOS visual/click tests before calling it functional.
- Merge prerequisite from main: `810feba70f01cdc35003ac80ea8486f39dc16049`, handoff `docs/agent-handoffs/physis-main-merge-2026-11-18.md`. Stage 18 handoff: `docs/agent-handoffs/physis-editor-scene-inspector-2026-11-18.md`. Continue to document every implementation stage in a separate commit with real test results.

## Stage 19 — field asset importer and editor owned gizmos

`PhysisFieldAssets.h` is the canonical typed parser of authored `windZones`, `storms`, `gravityFields`. Validate stable IDs and fixed-point bounds before `installFromMap` modifies World; sort IDs across types before allocating entities. Keep it independent from all genres, editors, renderer, NEREID, TerraWeave, Box3D. Opt-in `enableWindZones`, `enableStorms`, `enableGravityFields` must match nonempty collections to preserve persistent component schemas.

The editor `apps/editor/PhysisFieldEditor.h` owns map-authored markers, click/drag and selected properties in shared MapProps Inspector, using `PhysisAuthoring.h` (same native field parser for validation). One drag opens exactly one MapDoc Undo snapshot; never write directly into running `Simulation::world()` from Inspector/Gizmo. Authored IDs are document keys, not replicated/persisted ECS IDs.

Tests `physis_field_assets`, `physis_field_authoring` and extended `physis_project_config` have source + CTest + CI registration, **NOT RUN**. Stage handoff: `docs/agent-handoffs/physis-field-assets-gizmos-2026-11-18.md`. Run real editor/Box3D/PHYSIS tests before PR #25 is ready.

## Stage 20 — resize and snapping authoring invariants
- The editor offers opt-in Move/Radius gizmos and metre-grid snap; only `apps/editor/PhysisFieldEditor.h` uses ImGui, camera projection and mouse input. Keep `engine/physis` renderer/editor independent.
- `PhysisAuthoring.h::resizeField` edits outer storm radius or wind/gravity radius after the shared native parser validates the complete map. Move/resize drags call `MapDoc::beginEdit` only for the first effective edit, `MapDoc::touched` thereafter; preserve one Undo per drag. Never mutate the live World.
- Snap is a transient editor-only preference. Do not serialize it into PHYSIS ECS components or map assets. Existing map/save schemas and authored IDs remain unchanged.
- Handoff: `docs/agent-handoffs/physis-editor-gizmo-snapping-resize-2026-11-18.md`. New test fixtures were authored, **NOT RUN**; build/CTest/macOS Scene QA still required.


## Stage 21 — multi-field authoring guardrails
- apps/editor/PhysisAuthoring.h moveFields, duplicateFields and removeFields
  are editor MapDoc operations, never a live ECS write. Batch validate all
  field types before ONE Undo transaction. IDs belong to the authored map;
  they are not persisted ECS entity IDs.
- apps/editor/PhysisFieldEditor.h uses transient sorted multi-selection and
  a frozen drag-origin snapshot; one delta is applied to all fields and
  an anchor-grid snap must preserve relative offsets. Radius changes the
  active field alone. Switching Shift during group drag must not reset
  already moved axes.
- Duplication preserves unknown JSON properties while generating unique
  ASCII IDs under the 64-character limit. Enforce native parser shape,
  coordinates, 2048-field cap and per-category persistence enable flags.
- Preserve the genre-independent PHYSIS/NEREID/TerraWeave/Box3D boundaries.
  Source tests are authored but NOT RUN. Stage handoff:
  docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md.


### Stage 21 grouped yaw/scale extension
- Use editor-only physis_authoring::transformFields for atomic group Y-axis
  yaw and uniform geometry scale around the active member, one Undo.
- Scale zone radius (and storm eye/outer radius/height), group-relative
  positions in XYZ, NOT wind velocity, storm strength or gravity forces.
  Validate all outputs against canonical parser BEFORE writing MapDoc.
- C++ fixture testGroupRotationAndScale has been authored but NOT RUN.
  No physics/runtime edit, new ECS component or save schema was introduced.


## Stage 22 — PHYSIS editor ownership and gizmo safeguards

- Pure scene math: apps/editor/PhysisGizmoGeometry.h. Mouse and ImGui:
  apps/editor/PhysisFieldEditor.h. Do not include either in engine/physis.
- Five pivot modes use physis_authoring::computeFieldPivot with selected
  authored IDs. Pivots and selection are session-only, never ECS schemas.
- World-axis X/Y/Z translation relies on RTSCamera::screenRay and exact
  ray-axis closest point; skip near-parallel rays. Marquee captures only
  opt-in empty-scene drag, not normal scene selection by default.
- Rotation ring Y changes field centre POSITIONS, not wind/gravity force
  vectors. Uniform scale adjusts source radii and storm heights as well as
  spacing, NOT intensities. Do not claim full 3D quaternion orientation.
- Dragging rotation/scale must pass initial JSON baseline to transformFields
  with firstEdit=false after the initial edit, preserving one MapDoc Undo.
  Validate the entire new authored config before writing; cancel on outside
  doc revision changes. Never write live PHYSIS World from Scene.
- A separate agent added pure PhysisFieldChangePlan.h. It is NOT an
  authoritative ECS hot-reload implementation; keep concerns separate.
- Stage handoff: docs/agent-handoffs/physis-editor-marquee-3d-gizmos-2026-11-18.md.
  Source tests authored, native C++/CTest/macOS GUI NOT RUN.


## Stage 23 — opt-in live ECS field reconciliation

- New engine/physis/PhysisFieldRuntime.h is the ONLY generic runtime
  reconciler for stable authored source IDs; it composes the pure
  PhysisFieldChangePlan.h and existing PhysisFieldAssets.h parser.
- installEditableFromMap must be selected explicitly during GameModule
  setup. It installs all Wind/Storm/Gravity component save schemas
  and physis.field_runtime v1 resource. The normal installFromMap
  retains its legacy save manifest. Do not enable new schemas by default
  or claim automatic migration of existing saves.
- FieldRuntimeState lives entirely inside World; IDs map to generation-
  checked Entity handles, kind and canonical binary authored fingerprints.
  World snapshot/save/restore must include this resource. Updating a field
  of the SAME kind preserves the Entity, and unchanged storm sources retain
  their runtime drift. Retypes are Remove+Add, not in-place type swaps.
- A trusted GameModule must reserve a command type, authorize player=0
  on its network ingress and call applyFieldEditCommand ONLY inside
  Simulation::applyCommand at a committed tick boundary. Payload uses
  existing Command.args and is limited to 4096 64-bit words (32760 bytes
  of payload). A queued command is not an applied/acknowledged edit.
- Stale expected revision rejects a batch. All full desired typed fields
  must validate before any World mutation; stage an independent World copy,
  apply plan, then commit. Never mutate World directly from Scene or
  skip authoritative parser-range checks for network payloads.
- Field-only map submission rejects changed climate grid parameters.
  Flood/disaster policy and other PHYSIS settings are not live-migrated,
  nor are NEREID water/TerraWeave terrain/Box3D physics authorities.
- CTest physis_field_runtime is authored and registered, but native
  compile/CTest/editor integration/network service QA NOT RUN.
  Handoff docs/agent-handoffs/physis-field-runtime-reconciliation-2026-11-18.md.


## Stage 24 — authority and confirmation guardrails

- Stage 24 adds the non-ImGui apps/editor/PhysisLiveEdit.h local route and
  apps/editor/windows/PhysisWindow.cpp UI. A GameModule must opt into the
  FieldEditHost interface, explicitly allow trusted LOCAL editing, use
  installEditableFromMap and dispatch its own command type at the tick.
- Queued is only submission. Read World FieldRuntimeState revision and
  authored fingerprints to verify Applied; report conflicts, missing
  handler, rollback and superseded state instead of inventing ACKs.
- A network CommandSink makes the local editor route unavailable.
  For remote use, an authenticated server must call
  physis::admitAuthorizedFieldEdit with a trusted connection principal
  and its own ACL callback. The helper rejects untrusted player=0,
  validates revision and reencodes a new system command for submission.
  Never trust packet player or a client-provided permission boolean.
- No public admin transport, remote ACK, default genre integration,
  live climate migration, water/Terra/Box3D edit rights or new default
  ECS save schemas were added. A dedicated server endpoint must be
  integrated by a native project before claiming remote live editing.
- CTest physis_live_edit has source fixtures and CI registration,
  but real native compile, CTest, macOS GUI and remote network QA
  have NOT RUN. Stage handoff:
  docs/agent-handoffs/physis-editor-live-edit-2026-11-18.md.


## Stage 25 — remote delivery and acknowledgement invariants

- PHYSIS remote admin protocol belongs to PhysisRemoteProtocol.h,
  PhysisRemoteClient.h and PhysisRemoteServer.h, over a dedicated
  authenticated net::Conn, NEVER the normal player InputFrame.
- ServerGateway accepts a PRINCIPAL from the host-authenticated
  connection. Require ACL callbacks. Command.player and client instance
  IDs are NOT authentication; trusted player=0 Commands are created
  by the server after admission.
- Cache receipt deduplication by (authenticated principal, client
  instance, request ID) with byte-exact payload comparison. Reuse
  with different payload rejects. Serialize new admissions until
  the previous PHYSIS edit resolves; Queued does NOT mean Applied.
- Applied requires scheduled Simulation tick completion and authoritative
  FieldRuntimeState revision plus typed fingerprints. On reconnect
  Query-before-resend, preserve request IDs and recheck Applied against
  rollback. Server cache is volatile; expectedRevision prevents
  duplicate effects but cannot guarantee old ACK after server restart.
- EditorLink under apps/editor/PhysisRemoteEdit.h is opt-in; the host
  attaches an ALREADY AUTHENTICATED net::Conn. No anonymous default
  PHYSIS endpoint, no FPS/RTS automatic admin permissions.
- Another agent's RuntimeFieldRegistry/TrackedFromMap is a separate
  opt-in store, NOT interchangeable with Stage 23 FieldRuntimeState.
  Do not enable both without explicit migration.
- No changes to NEREID water, TerraWeave, Box3D or default PHYSIS
  save schema. CTest physis_remote_edit authored but NOT RUN.
  Handoff docs/agent-handoffs/physis-remote-edit-reliable-2026-11-19.md.


## Stage 26 — durable receipts and admin-only network authority

- Prefer canonical installEditableFromMap/FieldRuntimeState and
  applyFieldEditCommand for the editor and remote admin pipeline.
  The parallel RuntimeFieldRegistry / FieldCommandHost from other
  agents is a different opt-in authority. Never install both.
- Stage 26 adds PhysisRemoteJournal.h. The host MUST reload a matching
  Simulation checkpoint BEFORE FileJournal::attach, and attach the
  journal BEFORE any privileged connection can submit requests.
  Journal save errors FAIL CLOSED before admission; unknown original
  intents are persisted prior to enqueue. Never silently ignore a
  corrupted journal or overwrite it from another process.
- LoopbackAdminEndpoint (PhysisAdminEndpoint.h) binds strictly to
  loopback WsListener, requires a nonzero reserved PHYSIS command
  type, and authenticates a session via keyed BLAKE2b proof with
  fresh OS entropy. Server key lookup and ACL are host-owned.
  Never use Command.player, request ID, session token or user-provided
  roles to authorize. Do not alter regular player InputFrame.
- Native WebSocket currently lacks WSS/TLS. Do NOT expose ordinary
  ws:// admin service on a public interface. External administration
  requires a separate encrypted tunnel or actual native TLS.
- ServerGateway persists exact ID/bytes, typed fingerprints, Queued/
  Applied/NACK status and re-checks ECS after restart/rollback. The
  separate file journal and Simulation save are NOT an atomic
  transaction; Unknown/Conflict on ambiguous crash recovery is
  intentional. No crash-durable exactly-once promise until a shared
  checkpoint/journal transaction exists.
- Stage 26 test source and CTest physis_admin_journal are registered
  in CMake and PHYSIS/Studio workflows, but C++ compilation/CTest,
  real GUI/network/TLS QA are NOT RUN. Handoff:
  docs/agent-handoffs/physis-admin-durable-journal-2026-11-19.md.


### Stage 26 correction — restore queued sidecar entries as unresolved

- ServerGateway::restoreJournal normalizes persisted Queued to
  Unknown and clears old scheduledTick. Never infer that a pending
  Simulation command survived a server-process restart.
- After restoring the corresponding World checkpoint, call
  observe() to verify revision+fingerprints. If not applied and
  revision still matches, ONLY a byte-identical original Submit
  from the same authenticated principal may re-admit through ACL.
  Keep request instance/ID and dedupe cache stable.
- The existing FileJournal and Simulation save are separate files:
  this recovery is NOT a crash-atomic transaction; ambiguous
  results remain Unknown/Conflict. Do not promise durable
  exactly-once ACK without coordinated checkpoints.
- Tests queuedReceiptBeforeCrash and
  queuedReceiptAlreadyAppliedBeforeCrash were added to
  tests/physis_admin_journal_test.cpp, but NOT RUN.
- Handoff docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md.


### Stage 26 hardening — queued receipt recovery after server restart
- When restoring the Stage 26 durable receipt journal, `ServerGateway::restoreJournal` converts any pre-crash `Queued` entry into unresolved `Unknown`: a persisted queue receipt does not prove the transient Simulation command survived process restart.
- Call `observe` on the restored authoritative World before serving admin requests. Confirm `Applied` only if `FieldRuntimeState` expected revision and all authored fingerprints match; otherwise retain `Unknown` or return `Conflict`.
- Only the identical authenticated original Submit with matching expected revision may requeue an unresolved intent. Do not promise cross-file crash atomicity between journal and World snapshot.
- Tests `queuedReceiptBeforeCrash` and `queuedReceiptAlreadyAppliedBeforeCrash` are authored in `tests/physis_admin_journal_test.cpp` but native compilation and CTest have NOT RUN.
- Handoff: `docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md`.

### Stage 40 — real process lifetime and recovery testing (2026-11-19)

- `LoopbackAdminEndpoint` MUST retain its `JournalProcessLock`
  for the whole listener/peers lifetime. The lock member is declared
  before the listener, so C++ reverse-order destruction releases it
  only after the listener and gateway are destroyed. Never unlink the
  `<journal>.lock` inode while any endpoint is alive.
- POSIX regression tests in `physis_admin_journal` now fork a second OS
  process, verify nonblocking exclusion, kill the owner with SIGKILL,
  and check that a new process can acquire the same path. They also
  restore persisted Queued as Unknown after a killed child endpoint,
  then re-admit the exact authorized original and confirm durable
  Applied after the authoritative ECS tick.
- Windows has the existing same-process lock test, but no equivalent
  child-process SIGKILL fixture. Locking remains advisory and does not
  provide cross-host distributed fencing or joint World/journal commit.
- An isolated Linux GCC 14.2 C++20 header smoke was compiled with
  -Wall -Wextra -Werror -pedantic and PASSED the process-kill/reacquire
  scenario. This is NOT a full AIVERSE CMake/CTest PASS; native repository
  build, macOS/Windows QA and the new integrated test are NOT RUN.
- Stage handoff: `docs/agent-handoffs/physis-stage40-cross-process-journal-2026-11-19.md`.

### Stage 41 — CI queue control and post-crash lock cleanup (2026-11-19)

- In the `killedEndpointRecoversQueuedJournal` fixture, destroy the final
  `LoopbackAdminEndpoint` before removing the temporary journal directory.
  Deleting an active lock inode can silently defeat advisory protection:
  never call `remove_all` on a live journal+lock directory.
- `.github/workflows/physis.yml` now uses branch/ref-scoped concurrency
  with `cancel-in-progress: true` to cancel superseded PHYSIS runs. C++ build
  output is retained as `physis-build.log` through the existing artifact
  upload even when the build fails. `set -o pipefail` preserves failure status.
- At inspection time, 149 PHYSIS branch runs were queued, none had passed.
  New CI runs for both macOS and Ubuntu were still queued. Do not claim
  compilation, CTest or platform QA PASS until verified from job logs.
- Handoff: `docs/agent-handoffs/physis-stage41-ci-validation-2026-11-19.md`.

### Stage 42 — do not acknowledge unpersisted admin receipts (2026-11-19)

- When `ServerGateway::persistJournal` fails (including in
  `observe()` after a committed ECS tick or after command scheduling),
  `journalHealthy_` becomes false. A queued or applied receipt held
  only in process memory must be externally returned as
  `Result::Unknown`, never a confirmed `Queued` or `Applied`.
- New edit submissions fail closed. Unknown request IDs while the
  journal is unhealthy must NOT be called safe to retry; the server
  cannot guarantee deduplication until journal recovery/restart.
- In-memory ECS `FieldRuntimeState` remains authoritative for current
  simulation state, but the sidecar is not a joint transaction with
  World checkpoints. Preserve this uncertainty in remote UI.
- `tests/physis_admin_journal_test.cpp` contains writer-injection
  regressions for failed Queued/Applied persistence and denied new
  edits. Source authored, native CMake/CTest NOT RUN at this stage.
- Handoff: `docs/agent-handoffs/physis-stage42-durable-receipts-2026-11-19.md`.

### Stage 43 — suspend client retry when journal is unavailable (2026-11-19)

- PHYSIS V1 Receipt binary layout remains unchanged. Protocol constants
  `kJournalDurabilityUnavailable` and `kJournalRetrySuspended`
  identify `Result::Unknown` responses that MUST NOT trigger automatic
  `RemoteClient` retransmission. These strings are a V1 compatibility
  convention, not a new wire enum or a terminal applied result.
- `RemoteClient::poll` now transitions such responses to
  `ClientStatus::Uncertain` and stops automatic `Submit/Query` retries.
  Preserve the original pending request for explicit user retry only
  after the host's durable journal has recovered.
- Ordinary `Result::Unknown` without a journal-unavailable diagnostic
  continues the same-ID idempotent recovery path. Never change this into
  a new request ID. Do not treat Uncertain as success.
- New `unhealthyJournalStopsAutomaticResubmit` source fixture is in
  `tests/physis_remote_edit_test.cpp`; it checks that no application
  Submit/Query packets are emitted after a journal-unavailable response.
- Code and tests committed, but native C++ build, CTest and macOS/Windows
  runtime QA are NOT RUN. PHYSIS Actions had queued/cancelled jobs, no
  verified PASS. Handoff:
  `docs/agent-handoffs/physis-stage43-client-journal-backoff-2026-11-19.md`.

### Stage 44 — sticky journal uncertainty, reconnect and explicit retry (2026-11-19)

- `RemoteClient` MUST keep `ClientStatus::Uncertain` caused by a
  journal-unavailable Unknown across `disconnected()` and reconnect.
  Automatic Submit/Query is forbidden until explicit `retry()`.
- A delayed pre-failure Queued/Applied receipt must not clear this
  suspension. `suspendedForJournal_` is reset only when starting or
  restoring a distinct request or on explicit user retry; it is not
  an ECS-authoritative acknowledgment.
- Explicit `retry()` retains the original instance/request ID and
  probes the server first, so a recovered gateway can confirm Applied
  from journal + authoritative World without reapplying the edit.
  Ordinary Unknown without a journal-unavailable diagnostic retains
  existing idempotent same-ID recovery.
- New source fixtures `manualRetryAfterJournalRecovery` and
  `ordinaryUnknownStillRetriesOriginal` are in
  `tests/physis_remote_edit_test.cpp`; the former also injects a
  late Queued receipt after Uncertain and verifies it is ignored.
- Full native CMake/CTest, platform CI and editor UI execution are
  **NOT VERIFIED**; GitHub Actions remains queued/cancelled as inspected.
  Handoff: `docs/agent-handoffs/physis-stage44-manual-retry-2026-11-19.md`.

### Stage 45 — never block on a malicious POSIX journal lock file (2026-11-19)

- `JournalProcessLock::acquire` MUST open the POSIX lock path with
  `O_NONBLOCK` before `fstat` and `flock`. A user-controlled FIFO at
  `<journal>.lock` could otherwise block privileged admin startup before
  the inode-type guard had a chance to reject it.
- Continue requiring a regular single-link, current effective UID-owned
  lock file without group/other permission bits. The lock and its parent
  must be in a trusted server-owned directory. This is advisory
  single-host locking; it is NOT distributed fencing.
- `unsafeJournalLockInodesRejected` in `physis_admin_journal` now
  creates a malicious FIFO and asserts immediate refusal.
- Isolated Linux GCC 14.2 C++20 harness: PASS. It compiled the EXACT
  fetched PHYSIS header with Git blob SHA
  `835cfb9630e66d7918ed91ef06f97f590c2007b5`, and checked
  FIFO refusal, mode/hardlink denial, inter-process exclusion and
  SIGKILL lock release. Full AIVERSE CMake/CTest was NOT RUN.
- Handoff: `docs/agent-handoffs/physis-stage45-fifo-lock-hardening-2026-11-19.md`.
