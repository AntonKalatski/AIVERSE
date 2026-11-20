# Studio HTML5 — asset references, Inspector lock, global UI zoom (2026-11-20)

## Branch and status
- Repository: chromealex/AIVERSE
- Branch: `feature/studio-html5`.
- Starting point: existing Studio HTML5 asset browser with project-relative `content/`, native asset catalog, GUIDs, and `application/x-aiverse-content-path` drag MIME.
- Last implementation commit at handoff: `714723e0c610c19d48f67ccd19ebdd4085781000` (before this documentation commit).
- This stage was edited through the GitHub connector. **No local npm/CMake/CTest build was executed.** Do not label it verified until local checks pass.

## Implemented
1. `apps/studio-web/src/components/AssetReferencePicker.tsx` and `.css`
   - Reusable modal over `studio.assets`, with path/name/GUID search, optional asset-kind filtering, clear, Escape and overlay-close.
   - Accepts drag of project/content asset from `AssetsPanel` using the existing MIME; rejects OS drops and unsafe paths.
   - Selects GUID when catalog entry provides it; otherwise writes content-relative path. API: `<AssetReferencePicker value onChange label kinds? disabled? allowClear? />`.
   - Asset catalog is native authoritative; no browser filesystem IO or duplicated asset registry.
2. `propertyLogic.ts` / `PropertyField.tsx`
   - Added `FieldDescriptor.type='asset'` and optional `assetKinds`. Inspector fields explicitly marked asset use the picker. Existing text/number/boolean behavior unchanged.
3. `App.tsx` / `StudioPanelRenderer.tsx`
   - Inspector lock snapshots selected entity and its property values, independent of later global scene selection changes.
   - Writes target pinned entity ID; pinned displayed values update on successful commits. Unlock returns to live selection.
   - Editor settings control in titlebar: global `uiScale` range 75–200%, step 5%, persists in localStorage; document-root CSS zoom scales dock, panels, dialogs and content. Fires resize to sync native viewport surface.
4. `ui/editor-shell.css`: settings UI styling.

## Important integration work still pending
- **Native schema:** Current native `studio.snapshot` may not emit fields with type `asset`; register all actual asset-valued object-reference properties at source and verify `studio.setProperty` accepts GUID/path representation for each. This feature does not magically convert arbitrary text keys into asset fields.
- **Every reference site:** Audit materials, shader graphs, scene components, animation, VFX, lighting and future migrated panels. Use the shared picker for all explicit asset references, not handcrafted text inputs. Current materials authoring exposes PBR scalar values only; do not pretend texture slots exist before backend support.
- **Drag-to-GUID:** Current direct drag can fall back to a path when the picker has not loaded the catalog. If native schema requires GUID, resolve dropped asset via native `studio.assetByGuid`/catalog lookup and reject unresolved drops instead of writing a path.
- **Lock semantics:** Inspector is locked against external selection, but the snapshot of pinned properties is not automatically refreshed from native mutations made by another panel. A future native property-by-ID fetch method would allow precise refresh.
- **Other dock panels:** Add lock toggles only where external selection should not mutate panel context (preview, shader graph, material editor as needed). Do not lock panes that never follow selection.
- **Zoom QA:** Verify macOS WKWebView composited Metal viewport alignment, native hit-testing, drag boundaries, scrollbars, modal positions, high-DPI, 75%/100%/150%/200% and persistence across restart. CSS zoom must not scale gameplay world-space itself.


## Follow-up implementation stage (2026-11-20)
- Native `apps/studio-native/StudioSession.cpp`: removed the global-selection-equality guard for `studio.setProperty`. The write still requires an explicit nonempty entity ID, validates map IDs through `indexFor`, and validates FPS node IDs through the document node enumeration. This makes the Inspector lock's edit operation functional when scene selection moves.
- `apps/studio-web/src/App.tsx`: pins are reset upon an actual project open and when snapshot project path changes. No cross-project pin carryover.
- `apps/studio-web/src/components/AssetReferencePicker.tsx`: a dropped path is resolved against a fresh native `studio.assets` catalog before it may be accepted. Unknown paths and incompatible kinds produce an error; the picker never writes arbitrary drag text. GUID takes precedence when present.
- `tools/studio/test_reference_pin_zoom.py`: added source-level contract tests for picker schema, shared MIME, native pinned writes, zoom and native viewport resize hook.
- New implementation SHAs: `cc1f95ecb0072fb3fe8a417f0941303bd8a8a2bd`, `64656d0cb64e80270b3f86b9c919b1a690874011`, `7e1490a8eecd12f4ae84ecf718652fb111a4e382`, `a7d320fc4c4fe4d1e0dd01a1f5111f51b917d8cf`, `8be9c8aa9288de2af83f047e80ec51235b0c9263`.
- **Actual checks:** inspected current native `studio.setProperty`, `studio.select`, `studio.assets`, React picker, native viewport code and source-level test definitions via GitHub connector. **Not run:** Python unit tests, Node typecheck, Vite bundle, macOS CMake/CTest, manual GPU compositor validation; remote sandbox lacks network checkout/build environment. Do not claim pass.
- **Additional risk:** a pinned Inspector currently retains its properties as a UI snapshot and only applies its own successful writes. Undo/external changes do not refresh those pinned fields; implement native `studio.propertiesFor` with document ID and project-scoped validation to refresh without stealing selection.
- **Schema gap:** existing native `studio.snapshot` emits `text`/`number` properties (entity type, transforms, owner); there is no current native `asset` property in this snapshot. The shared picker is ready for future typed asset-bearing schemas but cannot be claimed to cover every actual editor reference. Native material authoring currently exposes scalar PBR keys rather than texture binding fields. Enumerate actual persisted asset-reference fields before extending the backend, rather than inferring references from property names.
- **Testing next:** run `python3 -m unittest tools.studio.test_reference_pin_zoom` or discover mode plus npm commands above, then add native tests verifying editing object A while selecting B, invalid/deleted entity rejection, and project switch.

## Validation required on developer Mac
```sh
cd apps/studio-web
npm ci
npm run typecheck
npm run test:web
npm run build
cd ../..
python3 -m unittest discover -s tools/studio -p 'test_*.py'
```
Manual QA: select an asset by search, drag image/model from Assets, clear asset, reject unmatched kind; lock Inspector A then select B in Hierarchy, edit A and unlock; set UI zoom to each boundary and verify native Scene frame and pointer alignment.

## Next step for an agent
Run typecheck and resolve failures, then implement native explicit asset reference descriptors and adopt the picker panel-by-panel. Document each stage with actual checks, affected files and an immutable SHA. Do not merge to main before native QA.

## Stage 3: refresh pinned properties by ID
- Added `StudioSession::propertiesFor(entityId)` and `studio.propertiesFor` bridge API: read-only, validates the entity against current map/FPS nodes, returns schema fields without mutating global selection.
- `snapshot()` now shares `propertiesFor` as its property field source, eliminating drift between pinned/unpinned schemas.
- Added typed `studio.propertiesFor` request/response to `apps/studio-web/src/types.ts`.
- `App.tsx` re-reads pinned values after native snapshot changes (including undo and other editor updates) and unpins if target was deleted. Project switches still clear pins.
- Browser demo mock now supports per-entity property data and the read API, so pinned inspector doesn't disappear on demo refresh and edits target the pinned entity.
- Files: `apps/studio-native/StudioSession.cpp`, `apps/studio-native/StudioSession.h`, `apps/studio-web/src/App.tsx`, `apps/studio-web/src/types.ts`, `apps/studio-web/src/mock.ts`, `tools/studio/test_reference_pin_zoom.py`.
- Implementation commits: `ff2b2888f10d102b5387b07f0c4c4091239da659`, `645401bd960601eb02f897f5dae65eac48f1c54a`, `dc3aed3348737eb8986f86c940e5fa3230bd6462`, `04d2091a162333ecd282d018e62e93290fa0a24d`, `c06c55db74700cfb38c37d04e026f7a17837dafb`, `269630cec650391dbc868a1b57b3f8d1bab53c5b`.
- Actual validation remains **source inspection only**, not compilation/test execution. GitHub connector can read/write but container DNS cannot resolve GitHub for checkout; no local AIVERSE repository is mounted.
- Follow-up: verify native build and pin refresh behavior with undo/redo; consider explicit selection-independent target ownership policies for multi-user/remote editing before exposing these native APIs outside trusted Studio bridge.

## Stage 4: actual HTML5 reference audit, asset settings lock, zoom-safe graph drag
- Inspected shipped HTML5 panel implementations `MaterialsPanel`, `ShaderGraphPanel`, `LightingPanel`, `FacialRigPanel`, `AssetImportSettings` and schemas. The actual current native schemas contain no persisted asset-reference writable property. Facial `node`, `target`, `bone` denote imported model subnode/parameter names, NOT an asset GUID; Shader Graph node IDs refer to graph-local links; Material PBR panel currently edits scalar parameters. **Do not automatically replace these strings with AssetReferencePicker.** Extend a specific native writable DTO first, and explicitly mark its field `type:'asset'` only when it represents a content asset.
- `AssetImportSettings.tsx`: added Lock/Unlock button with aria-pressed and labels.
- `AssetsPanel.tsx`: retained `pinnedAssetPath` independent of UI asset selection. Browsing another folder or selecting another asset cannot overwrite the pinned settings target; unpin follows the current selection. The panel resolves pinned path against the current asset catalog and clears stale pin/project switch; saving retains GUID metadata API.
- `ShaderGraphPanel.tsx`: pointer delta is divided by document-root CSS zoom, avoiding excessive node movement when UI scale is 75%, 150%, 200%. Native GPU viewport continues using screen-space client coordinates and is NOT altered by this math.
- `tools/studio/test_reference_pin_zoom.py`: added source contract tests for asset import pin/close/project reset and zoom-correct graph movement.
- Commits: `bd56c98eaac3d2becd0aa33006a096cfda9aa096`, `0de7402c9b85c57986516ccde5c740ca82f91fa8`, `27f53a184d4c72ea65e9de2dc898ee42e47f7659`, `b1f1fdcbd39c3062ccc7c436a62657ab1fc51615`.
- **Validation:** inspected source and wrote source-level regression contracts. No JS/TS/Python test executed, no native CMake compile and no manual macOS UI/GPU verification. Run listed commands and exercise two inspector locks + UI zoom before merging.
- Next step: identify genuine persisted references in C++ native DTOs (texture binding, particle asset, prefab, animation asset), expose them over typed Studio bridge with per-kind validity checks and GUID resolution, then adopt shared picker. No fake data or unsupported write paths.

## Stage 5: real Material texture binding authoring (2026-11-20)
### Verified existing engine data shape
- `apps/editor/core/MaterialOps.cpp`: material library supports six texture path keys: `texture`, `normalMap`, `emissiveMap`, `teamMask`, `metallicRoughnessMap`, `occlusionMap`. Engine persists strings as paths relative to the project root; it does not consume GUID directly.
- `StudioAssetCatalog`: .meta GUID is authoritative; `byGuid()` resolves to content-relative path and importer. GUID must resolve to `texture` importer and non-directory.
### Changes made
- `apps/studio-native/StudioMaterialValidation.h`: `studioMaterialTextureKey` explicitly allowlists six texture fields.
- `apps/studio-native/StudioSession.cpp`: `studio.materialPatch` accepts bounded texture GUID string values, validates GUID through native catalog with `rebuild`, validates importer and file, and converts to `content/<relative path>` before delegating to existing engine `material_set`. Empty string clears a slot. Rejects all arbitrary non-GUID paths sent by browser. Existing safe numeric/boolean/text handling preserved; patch field cap raised from 15 to 24.
- `apps/studio-web/src/panels/materialSchema.ts`: exposes six texture-backed fields as `type:'asset',assetKinds:['image']` in Material Library inspector, with default blank strings. Existing material path values remain legible until replaced.
- `AssetReferencePicker.tsx`: only accepts GUID-backed catalog entries; for image-only picks honors native importer `texture`, rejects stale/missing GUID, highlights existing material path values alongside GUID.
- Tests: `apps/studio-web/tests/materialSchema.test.mjs` now reflects safe allowlist of texture slots and checks asset field types/defaults; `tools/studio/test_reference_pin_zoom.py` checks source-level native validation and conversion.
- Commits: `ffa22d920923587275c49062a2158fdea07466ec`, `6e721c837ed282f00aee43a23f1efd51d54b4198`, `2923a92a251e24d674ccc5e8c0d17135c75986a2`, `eece5f2487056703c0e306a64c0f5de0b83cdd03`, `eeb9b6536dfa335e29fbb2a2d4799c954b1b0666`, `706c1b6ca88766846ed40a30accdaf0e12c14313`, `8d21a6014fc90c4f7fbcb89d47e00d473263651d`.
### Important limitations
- Native material files **continue storing paths**, not GUIDs; asset moves can break existing references unless migration/rewriting is added. The GUID resolves at the authoring boundary only. Do not promise automatic reference repair after moves.
- Asset Picker currently filters by `assetKind` as well as optional native importer; some texture formats known to the native importer but not `assetTree.ts` may be hidden until the extension lists are aligned.
- No VFX/animation/model reference panel is integrated as they currently do not expose an appropriate persisted native reference write API.
- If material text field holds an old `content/...` path, it can be replaced by selecting the catalog entry; the browser does not send the old path back in a new patch unless the user edits it.
### Checks
- **Actual:** verified GitHub source references, pushed commits, added source/test files.
- **Not run:** `npm run typecheck`, `npm run test:web`, `npm run build`, Python tests, native CMake/CTest, Metal screenshot QA. Container's git network fails DNS; cannot honestly mark green.
### Next step
- Typecheck and compile on Mac; test successful texture selection/apply, unsupported GUID, dropped non-texture, clear field, cross-project GUID, absent .meta, undo limitations. Then implement a durable GUID reference format or safe remapping/rewrite when assets move, preserving compatibility with the runtime's project-relative path loader. Only then extend actual model/VFX reference schemas.

## Stage 6 — repair path-backed material references on asset move (2026-11-20)

### Implementation
- Existing `StudioTransferPlan::apply` preserves .meta GUIDs when files and directories move, but runtime material JSON persists project-relative strings. A stable GUID alone did not protect those references after move.
- `apps/studio-native/StudioSession.cpp` now snapshots native catalog `GUID -> content-relative path` immediately before `studio.assetTransferCommit`, applies the existing validated transfer, rebuilds catalog after, and derives old->new path mappings only for GUIDs still present at a different location. This includes descendants of moved directories.
- For each library material, only six known texture fields are remapped when their exact old path matches. All other fields stay unchanged. Changes are accumulated before mutating the JSON object, avoiding invalidated member iterators. The existing engine `saveMaterialLibrary` path persists the rewritten library; it also notifies dependent views.
- `StudioAssetTransferResult.repairedMaterialReferences` (optional) reports how many texture fields changed. Assets UI includes this number after commit. Failure repairing `materials.json` is surfaced in `issues`; asset transfers are already applied and cannot be treated as rolled back.
- `tools/studio/test_reference_pin_zoom.py` adds a static source-contract regression case for the GUID mapping, exact texture field rewrites, and visible feedback.

### Files and commits
- `apps/studio-native/StudioSession.cpp` — `c85dd3351a4ce28a049eb0ebbbe4f6bcdbf8b03c`, `3972e2a6387d423b41176a45a7bffb333033783d`.
- `apps/studio-web/src/types.ts` — `baf064525544d674e3e6d3c4b395858d5ba301f3`.
- `apps/studio-web/src/components/AssetsPanel.tsx` — `6e262510ad0c3edcc6346285b1f2578fb3e30c93`.
- `tools/studio/test_reference_pin_zoom.py` — `154472ac730078f3ef21b5b9296617539a965e92`.

### QA status and remaining limitations
- **Performed:** inspected GitHub source for the asset transfer mechanics, existing asset catalog GUID preservation, `loadMaterialLibrary`/`saveMaterialLibrary`, Studio bridge and TS result model. Submitted remote source changes and a source contract test.
- **Not performed:** actual C++ compilation, Python/JS tests execution, move/folder/keep-both/replace integration tests, WebKit + Metal QA. These are mandatory before declaring feature production-ready.
- This stage repairs **library material texture fields only**, not arbitrary model paths, rules `view` inline materials, VFX, animation, saved shader nodes, or externally modified files. The rewrite occurs *after* a transfer and is not atomic with it. For rigorous atomicity: build a move-impact plan, stage material JSON rewrites and rollback/repair operations together, and reject preflight on validation/IO failure.
- Do not assume a stable GUID reference is natively persisted: materials still store paths, which the move process repairs. External raw filesystem moves bypass Studio and are not automatically repaired.

### Next
1. Execute `npm ci && npm run typecheck && npm run test:web && npm run build` in `apps/studio-web`; run `python3 -m unittest discover -s tools/studio -p 'test_*.py'`; compile Studio native and CTest on Mac.
2. Add live integration tests for moving single texture, directory subtree, keep-both and replace collision, unrelated assets, corrupt `materials.json`, missing GUID, and partial move with issue handling.
3. Extend the reference graph beyond material library into `rules.json` and actual model/VFX/animation asset fields with explicit schema; build a preflight report and durable atomic move/repair transaction. Do not merge until tests pass.

## Stage 7 — shared native asset reference inventory (2026-11-20)
### Implemented
- Added header-only `apps/studio-native/StudioAssetReferences.h` with typed `StudioAssetReference`, one allowlisted table of six material texture slots, a catalog-based GUID lookup, and two pure functions: `studioCollectAssetReferences(materials,rules,assets)` and `studioRemapMaterialReferences(materials,relocated)`.
- Discovery covers real persisted material texture paths plus `rules.types[*].view.model` strings, arrays of strings and `{file:...}` entries. Matches under `content/` get GUIDs from authoritative asset metadata; legacy/unresolved paths are still visible with empty GUID. Model references are explicitly `writable=false` because the rules tree may be composed across multiple files and must not be blindly rewritten.
- `StudioSession.cpp` uses the shared material remapper (replacing duplicated six-key loops) and exposes read-only `studio.assetReferences`. Catalog is rebuilt on demand for the inventory. Model references are NOT automatically repaired yet.
- TypeScript `StudioAssetReference` and `studio.assetReferences` typed bridge added in `apps/studio-web/src/types.ts`; browser demo returns empty list rather than fake dependencies.
- `AssetImportSettings.tsx` displays a `Used by` list for selected asset (filtered by GUID or exact project-relative path) with document, owner, field, writable/read-only. Styling added in `AssetsPanel.css`.
- Updated `tools/studio/test_reference_pin_zoom.py` to replace stale checks for removed inline remapping and added source-level contracts for registry and usage UI.

### Source commits
- `892be4bae289e4e9bdb4795c8ace01484ccd9f79`: shared registry.
- `75ab8eea26ef7b2c82f22bc4b9a8931c4fb1e473`: native bridge + material remap reuse.
- `946651beeba8cfbe655c835221dd6c39f43c2c1d`: typed TS method.
- `61f15c76d5f34f1b36b876173f353c08feafde88`: mock.
- `eff852b1d1df19d9ae1d157e4ceb0096033842e7`: updated test contracts.
- `43214454d6910c59071937678b7213d53c478037`: visible asset usage panel.
- `4be92521945401272f3bd71fc1b4818465bfc07f`: style.
- `704d639ce624f87a5b2efe05a03b389de0acf586`: registry source test.

### Validation and boundaries
- **Actual:** connected-source review of `StudioAssetTransfers`, `StudioAssetCatalog`, `MaterialOps`, `Workspace`, current HTML5/StudioSession files. Committed source and test changes through GitHub.
- **NOT EXECUTED:** C++ compiler, npm typecheck/build, web tests, Python tests, native UI/Metal runtime tests. GitHub connector does not provide local worktree. Do not merge to main or declare CI success.
- **Not atomic yet:** asset transfer and repair still occur in separate writes; failure of material save after move is reported as a partial transfer, not rolled back.
- **Scope:** broad dependency inventory is a foundation, NOT complete graph for VFX, animation, audio, shader graph and arbitrary project files. No speculative string-key parsing and no blind rewrite of composed `rules.json`.
- **Next:** build & fix actual compiler/type errors; add integration tests for move/collisions/folder/undo and preview impact on registered references. Implement a safe transactional rules project writer, including origins/includes and a rollback journal, before repairing model references. Extend owner adapters only after native DTOs are confirmed.


### Stage 7 follow-up: empty texture clearing guard
- Real `material_set` in `apps/editor/core/MaterialOps.cpp` previously called `gameFile(..., "")` while collecting missing texture warnings. `gameFile` intentionally rejects empty paths, so clearing a material texture slot via the HTML5 picker would throw. The warning check now skips empty texture paths; empty string remains valid for clearing slots.
- Commit `d57b8d3b768e23ff751b9db078371ee0612db8ad`, regression source contract `22f76a14d88836385bac7161b2fce62bd1f7b386`.
- Native runtime behavior remains untested; exercise empty texture slots on Mac native QA.

## Stage 8 — dependency-aware move preview and safe blocker (2026-11-20)
### Implemented on feature/studio-html5
- `apps/studio-native/StudioAssetReferences.h`: `studioMoveReferenceImpact(plan,refs)` maps documented reference paths against selected internal file or folder moves. Matches exact source or descendants with path-segment boundary; reports transfer item ID, document, owner, field, previous and prospective new path, and whether native currently knows how to rewrite the reference.
- `apps/studio-native/StudioSession.cpp`: `studio.assetMovePreview` emits `references` alongside conflict items. On `studio.assetTransferCommit`, native re-derives the impact graph and checks the *actual submitted decisions*, preventing clients from bypassing the read-only rules model warning. An item with affected `rules.json` model reference may only be skipped, rather than silently moved. This handles folder descendants as well.
- `apps/studio-web/src/types.ts`: typed optional `references` array on `StudioAssetTransferPlan`.
- `apps/studio-web/src/components/AssetsPanel.tsx`: transfer confirmation displays impacted material fields and read-only rules model references per item. Apply is disabled while any non-skipped item has read-only dependencies.
- `tools/studio/test_reference_pin_zoom.py`: source-level contract guards added for preview schema, native commit-side enforcement, and UI blocking.
### Commits
- `c9b9a663c3185d0bcb01a674a14ede09045e8015` impact mapper.
- `a20990c057dc1d6636c0d1ad6dce1c972d25e57f` native preview and commit safety enforcement.
- `333a194087e7ce317d9af13dad7c730945040e0d` typed response.
- `c6f91d7f5d6862badce241a7e2ff7874a460efe8` UI.
- `db0820109d0195541c4566f05c18e5a9f4b10b0c` test source contracts.
### Verified scope and limitations
- Reviewed source of `StudioTransferPlan::describe/apply`, GUID catalogs, rules origin-aware `Project::saveRules()`, material persistence and HTML5 transfer UI.
- Actual source edits have been pushed through GitHub connector. **No local TS/Python/C++ build or tests executed**. Regression tests are written but must be run on developer machine.
- This stage deliberately **does not claim transactional move/rollback or automatic rules rewriting**. `Project::saveRules()` uses split composed rules and multiple writes, not an atomic unit; naive JSON rewrites would corrupt included/inherited sources. Blocking a known read-only model dependency is safer until a verified multi-file journal/rollback is built.
- Existing material repair after move remains best-effort and may partially fail. External imports and destructive replace collision handling also remain nontransactional.
- Impact mapper currently covers the explicit registered material texture fields and `view.model` only; other reference schemas are NOT indexed or protected. Legacy model paths not under `content/` will not match until declared path mapping is supported.
- Keep-both destinations may differ from the plan's original `to` field; the projected `to` in preview should not be interpreted as the final actual path in those cases.
### Next
1. Run `cd apps/studio-web && npm ci && npm run typecheck && npm run test:web && npm run build`; `python3 -m unittest discover -s tools/studio -p 'test_*.py'`; native macOS Studio CMake build/CTest.
2. Add native integration tests for selected vs skipped impacted moves, folder children, same-prefix nonmatches, stale catalog, and failed material repair.
3. Implement a durable preflight + staged journal that covers assets/.meta plus material/rules source documents with restoration semantics and crash recovery. Extend rules source-aware edits only after testing composed-file provenance and rollback.

## Stage 9 — reversible internal transfers and material commit hook (2026-11-20)

### Changes
- `apps/studio-native/StudioAssetTransfers.h/.cpp`: internal moves now use a preflighted in-memory reversal journal of file and sidecar renames. Validate source, destination, parent containment, collision, metadata safety, duplicate IDs and ancestor/descendant selection *before any mutation*. On move, catalog rebuild or dependent-document callback failure, reverse all completed renames and rebuild the catalog. Throw instead of reporting partially completed internal moves. External imports retain their previous best-effort semantics.
- Internal `replace` and `new-guid` are explicitly rejected; only `import`, `keep-both`, `skip` are allowed. The HTML5 conflict selector no longer offers destructive internal replacement. This is deliberate until replace carries a reliable backup/restore protocol.
- `apps/studio-native/StudioSession.cpp`: material JSON is parsed before moving. The callback remaps texture paths by old/new GUID catalog snapshots, then saves materials before transfer completion. Exceptions from this callback propagate to the internal move journal, which attempts reverse renames. Read-only rules dependencies remain blocked.
- `tools/studio/test_reference_pin_zoom.py`: removed obsolete partial-failure assumption and added source-contract checks for reversible operations and safe UI decisions.

### Commits
- `7fc6fca8f2b996cdf2f8c46928c6446aee6c077e`: callback API.
- `54a9f4e5d5efa29ca624e9b485e6b9abac3e6e5d`: reversible internal transfer.
- `36ead825c79863a10693c4653234bf3a215f53ef`, `25e471f2738c3ece33855d6e4b91148db5cf06c4`: native material save within callback.
- `ad02ff43972a1611f9098984faf5a6625c2a94a9`: disable internal replace in UI.
- `aaa8f9db7097f14206452c5a7f9d075ece01c4dc`: static source-contract tests.

### What this is NOT
- **Not crash-safe**: the reversal journal exists in process memory; SIGKILL, power loss, or OS crash between file renames can still leave incomplete state. There is no persisted operation journal, startup replay/recovery, fsync protocol, or atomic multi-file transaction.
- **Not guaranteed rollback**: reverse filesystem rename may itself fail, in which case native throws a conspicuous `rollback is incomplete` error; manual recovery required.
- `material_set` is outside this path; existing engine material writer is used. The callback expects its material write to either succeed or throw before installing final state. External import still follows old per-item best effort.
- `rules.json` includes/origins are not rewritten; model-referenced internal moves are blocked.
- No test execution / C++ compilation was performed. Source was inspected and GitHub commits were applied. Local repository is unavailable and terminal git access fails DNS. This is *not* a green-build claim.

### Mandatory next step
Run full macOS native build, `npm run typecheck`, `npm run test:web`, Python source tests and CTest. Add runtime fault injection for second file rename, sidecar rename, catalog rebuild, and failed materials save; verify source+meta+material byte equivalence after rollback and look for unexpected leftover files. Then implement durable journal with startup recovery for crashes and an atomic rules origin edit flow, including external imports and backed-up replace semantics.

## Stage 10 — persistent move-recovery journal + editor Scene camera (2026-11-20)

### Native transaction recovery
- Added `apps/studio-native/StudioMoveJournal.h` with project-local `.aiverse-cache/studio-move-journal/` (outside `content/`). Before file movement, it snapshots the original `materials.json` when present and publishes an atomic `pending.json` manifest of relative source/destination paths. A successful completion renames `pending.json` to `committed.json` before best-effort cleanup.
- `StudioAssetTransfers.cpp` now calls `journal.begin` before the first file/.meta rename, executes existing catalog rebuild and material callback, then `journal.commit`. If a step fails, it calls `journal.restoreOrCleanup` and rebuilds the catalog. Restore processes recorded moves in reverse order, refuses unexpected source/destination conflicts and restores material bytes from the backup. The journal is retained if recovery fails.
- `StudioSession.cpp` calls `StudioMoveJournal::recover` before `ws_.openGame` in both CLI startup and native Open Project paths, to avoid opening with half-relocated assets. Completed markers are cleaned without rollback.
- Safety boundary: only **internal**, non-destructive `import/keep-both/skip` moves use the journal. External imports and destructive replace remain outside these guarantees. This is persistent crash-recovery intent, **not yet power-loss durability proof**: manifest/material copy and parent directory currently lack fsync/F_FULLFSYNC, there is no multi-process lock or recovery fault-injection validation. Never claim unconditional atomicity.

### Independent Scene camera
- `StudioMetalScene.h/.cpp`: editor-owned Scene camera gained free pitch (including below horizon), pan in view axes, six-axis navigation relative to yaw, focus on picked document proxy bounds, and reset. The Scene renderer's own camera is separate from the game view camera.
- `Main.mm`: native-validated `studio.viewportInput` commands `pan`, `move`, `focus`, `reset` routed to the Metal scene preview. Native non-composited overlay supports middle-drag pan, WASD/QE move, F focus and Home reset.
- `ViewportPlaceholder.tsx`: HTML-composited Scene supports left/right drag orbit, middle or Shift+left drag pan, mousewheel zoom, WASD/QE move, F focus selected, Home reset. Keyboard focus is restricted to viewport, avoiding input/textarea interaction. Navigation hint added.
- `StudioPanelRenderer.tsx`: Focus and Reset camera buttons in Scene toolbar. `types.ts` updated for typed viewport commands, `ui/editor-shell.css` styles the non-intercepting shortcut hint.
- **Known gap:** keyboard movement currently follows discrete keydown/OS-repeat rather than frame-time continuous navigation, and non-composite native mode gesture parity requires manual QA. Selection focus uses available scene renderable proxies only; no promise to focus an unsupported node.
- Tests updated: `tools/studio/test_reference_pin_zoom.py` source contracts cover persistent journal/restore and editor camera commands; tests themselves were not executed.

### Implementation commits
- Camera API `d1990b47358ba17f3c30bcfc67ce6586d894189d`, implementation `2a2e039c9b08191ceabaf3f4f23190bd912523b1`, native command routing `8e826d0382b9d7ddb02bc246ac398c29f55f9dde`, typed inputs `9e09998ac66e6b9ea962723019c9819688fc23a1`, HTML5 interaction `9bac667974dac7491c0ddf35c0ed474fbd47d258`, native overlay keys `61d5e27f8ec3e2e10e69598fcf5c2565b77a107b`, UI buttons `0bb931bd1a3f0dc7089507ccd1b49e57f7882e8d`, navigation styling `9ff07a00351c65f0a1893d64257dfee3daaf3055`.
- Recovery journal `a5c18ee7b5f3c244e559e39b598cb3c6d29c8472`, transfer wiring `d8bfc425c041afbf3064e810dac82e1870f22ee4`, project open recovery `9f68dc5b25db31e575699806da7ce1af914a2177`, updated source guards `657d9b719e6bbf6dc84152cbaeabc458650be214`.

### Actual validation / next
- **Actual:** reviewed GitHub sources for existing transfer semantics, native Studio session startup, scene viewport and camera math; source commits applied. No executable C++/TS/Python tests, Metal visual QA or power-fail injection were performed.
- **Next steps:** compile on Mac; add fixture-based native tests for crash after first rename/after sidecar/after material save/before committed marker; verify byte-for-byte restoration of material file and all GUID identities; test symlink and malformed manifest refusal, completed marker idempotence. Improve durability with fsync/F_FULLFSYNC for journal and backup files/parent directory and explicit single-writer lock, then revisit fully composed rules transaction. Verify free-camera controls in both GPU overlay modes at UI zoom 75/100/150/200%; do not merge until green.

### Stage 10 follow-up: true right-button free look
- `StudioMetalScene::look(dx,dy)` rotates yaw/pitch while preserving the camera's world-space eye, unlike `orbit` which rotates around the scene pivot. `StudioMetalScene.h` exposes the new method.
- `Main.mm` adds validated `kind=="look"` to the WKWebView bridge and routes native `rightMouseDragged:` to this method. `ViewportPlaceholder.tsx` uses right-button free look and left-button orbit; middle/Shift+left remains pan. The typed bridge union includes `look`.
- `tools/studio/test_reference_pin_zoom.py` now checks for the distinct look path and preserved eye computation. Source changes committed but **not compiled or tested**.
- Commits: `e8fdeb7fa1a8d5ae269df6ce494b38999fd32320`, `7352c8e3209a112a23216b863ee5ee71ccf17c7e`, `39d32a674a39f6c3c558265734b291bb33ab608c`, `cb519f9710c50dd3ee7d143a6cfa8d40ca6a1602`, `8f604bbaa39850f845e441d477d617b3203b22c1`, `7d420f215cfd370ff0ae355a800d2f23579f9cf5`.
