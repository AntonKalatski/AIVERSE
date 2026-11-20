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
